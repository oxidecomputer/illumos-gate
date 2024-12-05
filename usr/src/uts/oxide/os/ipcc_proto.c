/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

/*
 * ipcc - interprocessor control channel
 *
 * The IPCC is a general communication channel between the Host and the
 * Service Processor (SP) supporting a unidirectional RPC interface in which
 * SP software provides the server and the Host acts as a client. The Host and
 * SP communicate using a dedicated async serial channel operating at 3,000,000
 * bits/s and employing hardware flow control. There are also a pair of
 * interrupt lines between the Host and SP which are used for out-of-band
 * signalling, although only the SP-to-Host interrupt is currently used in
 * this implementation.
 *
 * The SP is a device with constrained resources and in general when there is a
 * trade-off to be made, things are structured to make its life easier if
 * possible. The SP's firmware is written in the Rust programming language
 * which has a bearing on some of the choices made.
 *
 * Terminology
 * ===========
 *
 * Message:	Encoded data sent between the Host and the SP in either
 *		direction.
 *
 * Packet:	A message which has been framed for transmission over the
 *		channel. There is a 1:1 relationship between a message and
 *		a packet, with the packet being the COBS-encoded form of the
 *		message.
 *
 * hubpack:	A predictable serialisation algorithm, implemented for the Rust
 *		programming language in a crate -
 *		https://github.com/cbiffle/hubpack/
 *
 * COBS:	Consistent Overhead Byte Stuffing. A framing technique which
 *		removes all occurences of a particular byte in data without
 *		significantly increasing the data size, allowing that byte
 *		to be used unambiguously as a terminator.
 *
 * Protocol
 * ========
 *
 * The protocol used over this channel is deliberately fairly simple. It has
 * the following key characteristics:
 *
 * 1. The host only ever initiates requests by sending data on the channel, and
 *    the SP only ever replies to requests.
 *
 * 2. Only one request may be outstanding at a time, there is no pipelining.
 *
 * 3. Messages are structured as a set of fixed length fields, followed by zero
 *    or more bytes of additional variable length data, followed by a checksum.
 *
 * 4. The fixed length field portion of a message is encoded in a format
 *    compatible with the hubpack. This allows the SP software to easily
 *    deserialise the data into a native struct and then access the following
 *    variable length data directly, without having to copy it.
 *
 * 5. Messages are transformed into packets suitable for sending over the
 *    channel using COBS, using a zero byte as the frame terminator.
 *
 * Provision for SP-initiated data transfer
 * ========================================
 *
 * Since the protocol requires that all requests are initiated by the host, an
 * additional mechanism is required if the SP has any reason to notify the host
 * of an event. This is achieved through the use of a GPIO line connected
 * between the SP and the Host. This line is used as a level-triggered
 * active-low interrupt. This line is usually high from the host's perspective,
 * but if the SP needs to notify the host that an event has occured or that
 * other data of interest is available, it will drive it low. The host can then
 * make requests to enumerate the set of pending events and then retrieve the
 * associated data or to clear the events.
 *
 * XXX - at present, the only event of interest here is an indication that the
 *	 SP task handling the protocol has (re)started which may require a
 *	 resynchronisation between the host and the SP. As such the host
 *	 currently only polls the interrupt while it is actively communicating
 *	 with the SP; re-synchronisation is covered below.
 *	 In the future, once the kernel GPIO framework gains support for
 *	 handling interrupts and there are other events of interest, the
 *	 interrupt should be serviced promptly regardless of whether
 *	 communication is active.
 *
 * Message encoding
 * ================
 *
 * Messages are structured as:
 *
 *    header | fixed length fields | optional variable length data | crc
 *
 * with the header being itself a sequence of fixed length fields:
 *
 *    magic(u32) | version(u32) | sequence(u64) | command(u8)
 *
 * encoded in a hubpack-compatible format. The fields that appear in all
 * messages are:
 *
 *	magic		A fixed magic number (IPCC_MAGIC).
 *	version		Protocol version number.
 *	sequence	A sequence number which increments for each Host->SP
 *			message. The SP uses the value from a request when
 *			responding, but also sets the top bit.
 *	command		Requested action (for requests) or a response code
 *			(for replies). This is a u8 because it is likely to be
 *			deserialised into an enum on the SP side, and hubpack
 *			represents enums as a u8.
 *	crc		A Fletcher-16 checksum calculated over the entire
 *			message up to the end of data'.
 *
 * Generally the SP deserialises the header and the fixed length fields
 * associated with a particular message, leaving any variable data in the
 * original buffer and accessing it there directly without having to copy it
 * around and use more memory.
 *
 * Framing and synchronisation
 * ===========================
 *
 * Using COBS to frame packets on the wire has a number of benefits. Either end
 * is able to unambiguously identify the end of a packet and either end can
 * terminate a partial packet sent by just writing a frame terminator. There
 * are, however, some situations that can cause the two ends of the channel get
 * out of sync and the protocol implementation has to be able to deal with
 * this.
 *
 * First, it is conceivable that corruption could occur during transmission.
 * This has not been seen in extensive testing on two separate servers (where
 * gigabytes of data have been transferred over this channel) but it's worth
 * thinking through what would happen if it did. Assuming the corruption is
 * within the body of the packet, then it may be detected by checksum at the
 * end of the frame; that checksum is using the Fletcher-16 algorithm which is
 * cheap for the SP to calculate. Assuming the checksum appears correct, then
 * the magic and version fields will be checked and, on the Host side, the
 * sequence number in the response packet will be checked against the expected
 * value.
 *
 * If corruption is detected by the SP, then it will reply with a special
 * message that indicates it is unable to decode the request, and the host will
 * re-send. Similarly, if the host detects corruption in a reply, it will
 * discard it and re-send the request.
 *
 * A special case is if there is corruption in the frame terminator itself.
 * Without anything being done to guard against this the channel would
 * become permanently wedged. Implementing a timeout here was considered but
 * discarded as an option because there is no guaranteed response time for any
 * message sent to the SP. Some messages are likely to take a while and the SP
 * is not a hard real-time OS, so selecting an appropriate timeout value is
 * difficult. The solution implemented here is for each side to follow up a
 * packet with periodic additional frame terminators, while waiting for a
 * reply, possibly filling up the Tx FIFO. When read by the other side of the
 * channel, this just appears as an empty packet and is discarded. The code
 * here sends one of these extra terminators around every 0.1 seconds while a
 * reply is outstanding (the period is not critical).
 *
 * Another way that synchronisation can be lost is if the SP task
 * panics/restarts after the host has sent a command. In that case it will come
 * back up without the command to process and the host will still be waiting.
 * To address this, the SP maintains a 64-bit status register and whenever it
 * is non-zero, it asserts the out-of-band interrupt. Whenever the SP task
 * starts or restarts, it sets a bit in that register to indicate that, which
 * has the side effect of asserting the interrupt. The host notices this
 * and gives up sending/waiting for the active command, and issues a new
 * request to retrieve the status register. It then processes the bits which
 * are set there, clearing them by retrieving data from the SP or send commands
 * to acknowledge the event. Once the register is clear (and the interrupt
 * de-asserted), the original command is sent again.
 *
 * Whilst this next part is implemented on the SP side, it's worth mentioning
 * what happens if the reverse occurs. One of the messages that the host can
 * send to the SP is a notification of a panic. If a panic occurs while
 * processing a different message, there is a situation where the SP can be
 * blocked writing a response to the host and the host is blocked writing the
 * panic message to the SP. This is handled in the SP by it continuing to read
 * from the host even while it is sending a response. Usually it just sees the
 * empty frames mentioned above, but if it sees a new command then it throws
 * away what it is trying to send and processes that.
 *
 * Finally, in testing we've seen a situation where the host and the SP are out
 * of step. The host is transmitting requests and the SP is returning replies,
 * but the SP reply is a response to an old request. In this case, when an SP
 * reply is valid in all aspects apart from having a bad sequence number, the
 * host will discard the reply and listen again, without re-sending.
 *
 * Sequence number
 * ==============
 *
 * Each message contains a 64-bit sequence number in the header which is used
 * to uniquely identify a particular request (wraparound aside). When a message
 * must be re-transmitted for any reason, those retransmissions will carry the
 * same sequence number as the original. In particular this allows the receiver
 * to detect a retransmitted message so that it can reply with the same data
 * rather than assuming that its last response was successfully received.
 * This is especially important for things such as alert messages where a
 * message would otherwise be lost. Note that sequence numbers may not always
 * be used in order. For example, if message X is delayed because the SP has
 * asserted its interrupt line, then additional messages X + 1 .. X + n will be
 * sent to process the cause of this, before message X is finally sent.
 *
 * Phases of boot
 * ==============
 *
 * The host needs to be able to send requests to the SP at various times. First
 * it must be able to retrieve information very early in boot in order to
 * configure boot properties and system debugging options (for example, whether
 * to load the kmdb debugger). It must also be able to communicate later in
 * boot, once the virtual memory subsystem is initialised but before the device
 * tree is available or the STREAMS subsystem is available, and lastly it must
 * be able to operate once the system is fully up and in multi-user mode, and
 * it must also provide an interface that authorised userland applications can
 * use in order to communicate with the SP.
 *
 * Early in boot when there is only 'unix' - no kernel modules have yet been
 * loaded - and the kernel virtual memory subsystem is not available, the UART
 * that provides the control channel must be driven directly by accessing
 * registers via MMIO. The virtual address backing for that MMIO region is
 * necessarily allocated from boot pages and similarly for the MMIO region used
 * for reading the GPIO to determine the interrupt status.
 *
 * These boot pages are torn down during boot, shortly after KVM is available.
 * In the small window while both are usable, new MMIO VA mappings are obtained
 * from the device arena.
 *
 * Once the system is up, the UART is accessed via an instance of the dwu
 * driver via its /devices node, and the GPIO is checked via a DPIO node under
 * /dev. These are both accessed via LDI.
 *
 * This file implements the core IPCC protocol and does not need to know these
 * details, it just requires that consumers provide an ops vector containing
 * routines to access the hardware. The required routines are described in more
 * detail below.
 *
 * However, the necessity to work across the different boot phases does impose
 * some requirements, and lend itself quite neatly to some things:
 *
 * The early boot phase requires that this code live in 'unix' and that it not
 * use any functions from modules such as 'genunix' until they are loaded. To
 * achieve this it assumes it is in a single-threaded world until
 * ipcc_begin_multithreaded() is called, and does not use mutex_enter/exit
 * until that time. It also uses bcopy() instead of memcpy() since there is a
 * stub version of that available, and avoids string functions which are not
 * guaranteed to be present, see ipcc_loghex() for an example of this.
 *
 * The protocol needs regions of memory for constructing messages and packets.
 * While callers could pass in buffers for this, allocated from whatever memory
 * is available to them depending on the boot phase, only one transaction can
 * be in progress at a time. Therefore this file defines two global static
 * buffers for this. To use the channel, a caller must use
 * ipcc_channel_acquire() to gain exclusive access, and call the corresponding
 * ipcc_channel_release() when finished, including being finished with any
 * pointers into these global buffers. There is more about this in the block
 * comment above the ipcc_command_locked() function.
 *
 * Ops Vector
 * ==========
 *
 * As mentioned already, the protocol implementation in this file needs to be
 * able to access the hardware - both the UART and the GPIO - in any of the
 * boot phases. In order to abstract that, the exposed APIs require that
 * callers pass in an ops vector that provides the following entry points.
 * There is also provision for an additional opaque parameter which is used as
 * the first argument when invoking a callback. The callbacks are shown below;
 * any which are mandatory are prefixed with a '+', others may be left as NULL
 * if not required and they will not be called.
 *
 *	 io_open	Open the channel.
 *	 io_close	Close the channel.
 *	 io_flush	As far as is possible, flush the buffers of the
 *			communications channel. This should as a minimum
 *			discard any data queued in any inbound or outbound
 *			buffer, although the SP may still have data to
 *			transmit and will do so once the CTS signal is
 *			re-asserted.
 *	+io_poll	Block until either:
 *			 1. The SP asserts its SP->Host interrupt signal;
 *			 2. One of the requested events occurs on the channel;
 *			 3. The (optional) provided timeout is exceeded.
 *			Return ETIMEDOUT (for 3), EINTR if interrupted,
 *			otherwise 0.
 *	+io_readintr	Return true/false depending on whether the SP is
 *			currently asserting the SP->Host out-of-band interrupt
 *			signal.
 *	+io_read	Read data from the channel.
 *	+io_write	Send data to the channel.
 *	 io_log		Receive a log message.
 *
 * If not NULL, the first of these to be called for a given transaction is
 * 'io_open', and the last is 'io_close'. The flow for an IPCC transaction
 * looks something like:
 *
 * -> entry point, ipcc_XXX(vector, arg, params...)
 *   -> ipcc_channel_acquire()
 *     -> io_open()
 *       -> io_readintr()
 *       -> io_poll(POLLOUT)
 *       -> io_write()
 *       -> io_poll(POLLIN)
 *       -> io_read()
 *     -> io_close()
 *   -> ipcc_channel_release()
 *
 * Retransmissions
 * ===============
 *
 * As may have become apparent from what's above, there are some cases when the
 * host will automatically resend a message during a transaction. This can
 * occur when:
 *
 *  - the SP asserts its interrupt while the host is sending or waiting for
 *    a response;
 *  - the SP replies to a request with 'Decode Failure';
 *  - the host has read IPCC_MAX_PACKET_SIZE bytes from the SP without finding
 *    a frame terminator;
 *  - the host cannot decode the COBS frame received from the SP;
 *  - the decoded packet from the SP is shorter than IPCC_MIN_PACKET_SIZE;
 *  - the reply message checksum does not match;
 *  - the magic number in the reply message is incorrect;
 *  - the version number in the reply message is incorrect;
 *  - a request sequence number was found in the reply.
 *
 * XXX - the implementation currently applies an arbitrary limit for the number
 *       of retransmissions that are attempted before giving up. This is the
 *       IPCC_MAX_ATTEMPTS macro below. Any loss of synchronisation on the
 *       channel should be resolved well before this limit is reached.
 *       XXX - panic instead?
 *
 * Consumers
 * =========
 *
 * There are currently two separate consumers of this protocol code in the
 * tree. One for the early stages of boot, and one for when the system is up
 * and multi-user. The first of these is kernel_ipcc which issues requests
 * to ipcc_proto and handles communicating with the underlying UART and GPIO on
 * its behalf.
 *
 *                         +--------------+
 *                         |              |
 *                         |  ipcc_proto  |                .-------.
 *                         |              |               ( Kernel  )
 *                         +--------------+                `-------'
 *                                 ^                           |
 *                                 |                           |
 *                                 v                           |
 *                        +--------------------+               |
 *                        |                    |               |
 *                        |    kernel_ipcc     |<--------------+
 *                        |                    |
 *                        +--------------------+
 *                             ^             ^
 *                             |             |
 *                             v             |
 *         +----------------------+    +----------+
 *         |         UART         |    |   GPIO   |
 *         +----------------------+    +----------+
 *                    ^                     ^
 *                    |                     |
 *                    v                     |
 *  +-----------------------------------------------------+
 *  |                  Service Processor                  |
 *  +-----------------------------------------------------+
 *
 * The second consumer is the 'ipcc' kernel module which provides callbacks that
 * talk to the UART and DPIO/GPIO via LDI. That kernel module provides a device
 * node and an ioctl() interface that can be used by processes in userland.
 * This path can also be used by the kernel using LDI to issue an ioctl() to the
 * same ipcc module.
 *
 *     .-----------.   .-------.
 *    (  Userland   ) ( Kernel  )
 *     `-----------'   `-------'
 *            ^            ^
 *            |   +-LDI----+
 *            v   v
 *         +------------+                  +----------+
 *         |    ipcc    |<-------LDI-------|   DPIO   |
 *         |   module   |<---------+       +----------+
 *         +------------+          |             ^
 *                ^                |             |
 *                |                |             |
 *               LDI               |             |
 *                |                |             |
 *                v                v             |
 *       +------------+    +--------------+      |
 *       |    dwu     |    |              |      |
 *       |   driver   |    |  ipcc_proto  |      |
 *       |            |    |              |      |
 *       +------------+    +--------------+      |
 *              ^                                |
 *              |                                |
 *              v                                |
 *          +----------------------+    +--------+-+
 *          |         UART         |    |   GPIO   |
 *          +----------------------+    +----------+
 *                     ^                     ^
 *                     |                     |
 *                     v                     |
 *   +-----------------------------------------------------+
 *   |                  Service Processor                  |
 *   +-----------------------------------------------------+
 *
 * As touched on above, once the system is multi-user, the kernel can use two
 * different method to communicate with the service processor. The expected path
 * for most requests is to use the same method as userland - that is to open the
 * ipcc module device node and issue ioctl()s via LDI. However the more direct
 * route that is used in early boot is still used for issuing reboot, powerdown,
 * bootfail and panic messages in order to minimise what is necessary to support
 * these. In the case of reboot, for example, all CPUs but one should be stopped
 * and interrupts are no longer being delivered at the point that the call is
 * made.
 */

#include <sys/byteorder.h>
#include <sys/ddi.h>
#include <sys/errno.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/platform_detect.h>
#include <sys/hexdump.h>

#include <sys/ipcc.h>
#include <sys/ipcc_proto.h>

#ifndef _LITTLE_ENDIAN
/*
 * ipcc_{encode,decode}_bytes() rely on little-endian byte order (which is
 * the ordering used for the hubpack protocol).
 */
#error ipcc needs porting for big-endian platforms
#endif

/* See "Retransmissions" above */
#define	IPCC_MAX_ATTEMPTS	10

/*
 * Global message and packet buffers.
 * For outbound messages, the message is constructed in ipcc_msg and then COBS
 * encoded into ipcc_pkt. For inbound messages the packet is received into
 * ipcc_pkt and then decoded into ipcc_msg.
 */
static uint8_t ipcc_msg[IPCC_MAX_MESSAGE_SIZE];
static uint8_t ipcc_pkt[IPCC_MAX_PACKET_SIZE];

/*
 * As well as indicating that we should expect to be called from multiple
 * threads, this also means that we are far enough through boot that genunix
 * is loaded, krtld has done its work, and functions like mutex_enter/exit are
 * available.
 */
static bool ipcc_multithreaded;

static kmutex_t ipcc_mutex;
static kcondvar_t ipcc_cv;
static bool ipcc_channel_active = false;
static kthread_t *ipcc_channel_owner;

static int ipcc_sp_interrupt(const ipcc_ops_t *, void *);

/*
 * This is an error code return by ipcc_pkt_{send,recv} when the SP-to-host
 * interrupt line is found asserted.
 */
#define	ESPINTR		(-2)

void
ipcc_begin_multithreaded(void)
{
	extern int ncpus;

	/*
	 * The system must still be single-threaded when this is called.
	 * XXX - this doesn't directly test that, is there something better?
	 */
	VERIFY3S(ncpus, ==, 1);
	VERIFY(!ipcc_multithreaded);
	VERIFY(!ipcc_channel_active);

	mutex_init(&ipcc_mutex, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ipcc_cv, NULL, CV_DRIVER, NULL);
	ipcc_multithreaded = true;
}

bool
ipcc_channel_held(void)
{
	return (ipcc_channel_active &&
	    (!ipcc_multithreaded || ipcc_channel_owner == curthread));
}

void
ipcc_release_channel(const ipcc_ops_t *ops, void *arg, bool doclose)
{
	if (!ipcc_multithreaded) {
		VERIFY(ipcc_channel_held());
		ipcc_channel_active = false;
	} else {
		mutex_enter(&ipcc_mutex);
		VERIFY(ipcc_channel_held());
		ipcc_channel_active = false;
		ipcc_channel_owner = NULL;
		cv_broadcast(&ipcc_cv);
		mutex_exit(&ipcc_mutex);
	}

	if (doclose && ops->io_close != NULL)
		ops->io_close(arg);
}

int
ipcc_acquire_channel(const ipcc_ops_t *ops, void *arg)
{
	int ret = 0;

	if (!ipcc_multithreaded) {
		VERIFY(!ipcc_channel_held());
		ipcc_channel_active = true;
	} else {
		mutex_enter(&ipcc_mutex);
		while (ipcc_channel_active) {
			if (cv_wait_sig(&ipcc_cv, &ipcc_mutex) == 0) {
				mutex_exit(&ipcc_mutex);
				return (EINTR);
			}
		}
		VERIFY(!ipcc_channel_held());
		ipcc_channel_active = true;
		ipcc_channel_owner = curthread;
		mutex_exit(&ipcc_mutex);
	}

	if (ops->io_open != NULL) {
		ret = ops->io_open(arg);
		if (ret != 0)
			ipcc_release_channel(ops, arg, false);
	}

	return (ret);
}

static uint16_t
ipcc_fletcher16(const uint8_t *buf, size_t len)
{
	uint16_t s1 = 0, s2 = 0;

	for (size_t i = 0; i < len; i++) {
		s1 = (s1 + buf[i]) % 0xff;
		s2 = (s2 + s1) % 0xff;
	}

	return ((s2 << 8) | s1);
}

static bool
ipcc_cobs_encode(const uint8_t *ibuf, size_t inl, uint8_t *obuf, size_t outl,
    size_t *decoded)
{
	size_t in = 0;
	size_t out = 1;
	size_t code_out = 0;
	uint8_t code = 1;

	for (in = 0; in < inl; in++) {
		if (out >= outl)
			return (false);

		/*
		 * If the next input byte is not a zero, append to the existing
		 * sequence.
		 */
		if (ibuf[in] != 0) {
			obuf[out++] = ibuf[in];

			/* If the sequence is not full, carry on. */
			if (++code != 0xff)
				continue;
		}

		/* Terminate the sequence and start a new one. */
		if (out >= outl)
			return (false);
		obuf[code_out] = code;
		code = 1;
		code_out = out++;
	}

	obuf[code_out] = code;

	*decoded = out;
	return (true);
}

static bool
ipcc_cobs_decode(const uint8_t *ibuf, size_t inl, uint8_t *obuf, size_t outl,
    size_t *encoded)
{
	size_t in = 0;
	size_t out = 0;
	uint8_t code;

	for (in = 0; in < inl; ) {
		code = ibuf[in];

		/*
		 * A code of 1 is valid as the last character in the input
		 * buffer, it just results in a 0 being written to the output
		 * and we're done.
		 */
		if (in + code > inl && code != 1)
			return (false);

		in++;

		for (uint8_t i = 1; i < code; i++) {
			if (out >= outl)
				return (false);
			obuf[out++] = ibuf[in++];
		}

		if (code != 0xFF && in != inl) {
			if (out >= outl)
				return (false);
			obuf[out++] = '\0';
		}
	}

	*encoded = out;
	return (true);
}

static void
ipcc_encode_bytes(const uint8_t *val, size_t cnt, uint8_t *buf, size_t *off)
{
	bcopy(val, &buf[*off], cnt);
	*off += cnt;
}

static void
ipcc_decode_bytes(uint8_t *val, size_t cnt, const uint8_t *buf, size_t *off)
{
	bcopy(&buf[*off], val, cnt);
	*off += cnt;
}

static const char *
ipcc_failure_str(uint8_t reason)
{
	switch (reason) {
	case IPCC_DECODEFAIL_COBS:
		return ("COBS");
	case IPCC_DECODEFAIL_CRC:
		return ("CRC");
	case IPCC_DECODEFAIL_DESERIALIZE:
		return ("DESERIALIZE");
	case IPCC_DECODEFAIL_MAGIC:
		return ("MAGIC");
	case IPCC_DECODEFAIL_VERSION:
		return ("VERSION");
	case IPCC_DECODEFAIL_SEQUENCE:
		return ("SEQUENCE");
	case IPCC_DECODEFAIL_DATALEN:
		return ("DATALEN");
	default:
		return ("UNKNOWN");
	}
}

static int
ipcc_msg_init(uint8_t *buf, size_t len, uint64_t seq, size_t *off,
    ipcc_hss_cmd_t cmd)
{
	uint32_t ver = IPCC_PROTOCOL_VERSION;
	uint32_t magic = IPCC_MAGIC;

	VERIFY(ipcc_channel_held());

	if (len - *off < IPCC_MIN_PACKET_SIZE)
		return (ENOBUFS);

	ipcc_encode_bytes((uint8_t *)&magic, sizeof (magic), buf, off);
	ipcc_encode_bytes((uint8_t *)&ver, sizeof (ver), buf, off);
	ipcc_encode_bytes((uint8_t *)&seq, sizeof (seq), buf, off);
	ipcc_encode_bytes((uint8_t *)&cmd, sizeof (uint8_t), buf, off);

	return (0);
}

static int
ipcc_msg_fini(uint8_t *buf, size_t len, size_t *off)
{
	uint16_t crc;

	if (len - *off < sizeof (uint16_t))
		return (ENOBUFS);

	crc = ipcc_fletcher16(buf, *off);
	ipcc_encode_bytes((uint8_t *)&crc, sizeof (uint16_t), buf, off);

	return (0);
}

static int
ipcc_pkt_send(uint8_t *pkt, size_t len, const ipcc_ops_t *ops, void *arg)
{
	ipcc_pollevent_t ev;

	if (ops->io_flush != NULL)
		ops->io_flush(arg);

	ev = IPCC_POLLOUT;
	if (ops->io_readintr != NULL)
		ev |= IPCC_INTR;

	while (len > 0) {
		ipcc_pollevent_t rev;
		size_t n;
		int err;

		if ((err = ops->io_poll(arg, ev, &rev, 0)) != 0)
			return (err);
		if ((rev & IPCC_INTR) != 0)
			return (ESPINTR);
		ASSERT((rev & IPCC_POLLOUT) != 0);

		n = len;
		err = ops->io_write(arg, pkt, &n);
		if (err != 0)
			return (err);

		VERIFY3U(n, <=, len);

		pkt += n;
		len -= n;
	}

	return (0);
}

static int
ipcc_pkt_recv(uint8_t *pkt, size_t len, uint8_t **endp,
    const ipcc_ops_t *ops, void *arg)
{
	ipcc_pollevent_t ev;

	*endp = NULL;

	ev = IPCC_POLLIN;
	if (ops->io_readintr != NULL)
		ev |= IPCC_INTR;

	do {
		ipcc_pollevent_t rev;
		size_t n;
		int err;

		while ((err = ops->io_poll(arg, ev, &rev, 100)) != 0) {
			if (err != ETIMEDOUT)
				return (err);
			/*
			 * Send periodic frame terminators in case the real one
			 * was corrupted or lost. The SP will just discard
			 * empty frames.
			 */
			uint8_t ka = 0;
			n = 1;
			(void) ops->io_write(arg, &ka, &n);
		}
		if ((rev & IPCC_INTR) != 0)
			return (ESPINTR);
		ASSERT((rev & IPCC_POLLIN) != 0);

		n = 1;
		err = ops->io_read(arg, pkt, &n);
		if (err != 0)
			return (err);

		if (n == 0)
			continue;

		VERIFY3U(n, ==, 1);

		if (*pkt == 0) {
			*endp = pkt;
			return (0);
		}

		pkt += n;
		len -= n;
	} while (len > 0);

	return (ENOBUFS);
}

typedef struct {
	const ipcc_ops_t	*lhcb_ops;
	const char		*lhcb_tag;
	void			*lhcb_arg;
} loghex_cb_t;

static int
ipcc_loghex_cb(void *arg, uint64_t addr, const char *str,
    size_t len __unused)
{
	const loghex_cb_t *cb = arg;

	cb->lhcb_ops->io_log(cb->lhcb_arg, IPCC_LOG_HEX, "%s  %s\n",
	    cb->lhcb_tag, str);
	return (0);
}

static void
ipcc_loghex(const char *tag, const uint8_t *buf, size_t bufl,
    const ipcc_ops_t *ops, void *arg)
{
	loghex_cb_t cb = {
		.lhcb_ops = ops,
		.lhcb_tag = tag,
		.lhcb_arg = arg
	};
	/*
	 * A line of hexdump output with the default width of 16 bytes per line
	 * and a grouping of 4, in conjunction with the address and ascii
	 * options will not exceed 80 characters, even if the address becomes
	 * large enough to use additional columns.
	 */
	uint8_t scratchbuf[80];
	hexdump_t h;

	hexdump_init(&h);
	hexdump_set_grouping(&h, 4);
	hexdump_set_buf(&h, scratchbuf, sizeof (scratchbuf));

	(void) hexdumph(&h, buf, bufl, HDF_ADDRESS | HDF_ASCII,
	    ipcc_loghex_cb, (void *)&cb);

	hexdump_fini(&h);
}

#define	LOG(...) if (ops->io_log != NULL) \
	ops->io_log(arg, IPCC_LOG_DEBUG, __VA_ARGS__)
#define	LOGHEX(tag, buf, len) \
	if (ops->io_log != NULL) ipcc_loghex((tag), (buf), (len), ops, arg)

/*
 * This is the main interface for sending a command to the SP via the IPCC.
 *
 * Callers must acquire exclusive access to the channel prior to calling this
 * function.
 *
 * The parameters are:
 *
 *	ops		- A set of callbacks to use, see above.
 *	arg		- An opaque argument to be passed to callback functions.
 *	cmd		- The command.
 *	expected_rcmd	- The expected response command.
 *	dataout		- A pointer to a sequence of bytes which is to be
 *			  included to the outgoing command. NULL if there is
 *			  no additional data to be sent.
 *	dataoutl	- The number of bytes pointed to by dataout.
 *	datain		- The address of a pointer which will be set to the
 *			  start of any data payload in the reply message from
 *			  from the SP. Can be NULL if no data is expected, in
 *			  which case the receipt of additional data is an error
 *			  and EINVAL will be returned.
 *	datainl		- A pointer to a variable where the length of the
 *			  received data will be stored. This variable may be
 *			  set to a value prior to calling in which case it is
 *			  an error if the length of the received data is
 *			  different. If the variable is zero on entry, no such
 *			  check is done; this supports replies with a variable
 *			  length payload.
 *
 * On return, 'datain' will be pointing to a global buffer and so consumers
 * must continue to hold the channel until they have finished processing the
 * returned data.
 *
 * This function can return:
 *
 *	0		- Success.
 *	EINTR		- The request was interrupted by a signal.
 *	ETIMEDOUT	- Despite a number of retries, communication was
 *			  unsuccessful. The caller should consider this a fatal
 *			  problem with the channel.
 *	ENOBUFS		- Out of buffer space; too much payload data was
 *			  provided.
 *	EINVAL		- Additional payload data was received and datain is
 *			  NULL.
 *	EIO		- The amount of data received does not match the input
 *			  value of datainl.
 *	E*		- Any error returned by the io_read and io_write
 *			  callbacks.
 *
 * ipcc_command() is a simpler wrapper around ipcc_command_locked() for the
 * case where no reply data is expected. It also takes care of acquiring and
 * releasing the channel.
 */
static int
ipcc_command_locked(const ipcc_ops_t *ops, void *arg,
    ipcc_hss_cmd_t cmd, ipcc_sp_cmd_t expected_rcmd,
    const uint8_t *dataout, size_t dataoutl,
    uint8_t **datain, size_t *datainl)
{
	/* Sequence number for requests */
	static uint64_t ipcc_seq;
	size_t off, pktl, rcvd_datal;
	uint64_t send_seq, rcvd_seq;
	uint32_t rcvd_magic, rcvd_version;
	uint16_t rcvd_crc, crc;
	uint8_t rcvd_cmd, *end;
	uint8_t attempt = 0;
	int err = 0;

	if (oxide_board_data->obd_ipccmode == IPCC_MODE_DISABLED)
		return (EIO);

	VERIFY(ipcc_channel_held());

	if ((err = ipcc_sp_interrupt(ops, arg)) != 0)
		return (err);

	/* Wrap if we have got to the reply namespace (top bit set) */
	if ((++ipcc_seq & IPCC_SEQ_REPLY) != 0)
		ipcc_seq = 1;
	send_seq = ipcc_seq;

resend:

	if (++attempt > IPCC_MAX_ATTEMPTS) {
		LOG("Maximum attempts exceeded\n");
		return (ETIMEDOUT);
	}

	LOG("\n-----------> Sending IPCC command 0x%x, attempt %u/%u\n",
	    cmd, attempt, IPCC_MAX_ATTEMPTS);

	off = 0;
	err = ipcc_msg_init(ipcc_msg, sizeof (ipcc_msg), send_seq, &off, cmd);
	if (err != 0)
		return (err);

	if (dataout != NULL && dataoutl > 0) {
		if (sizeof (ipcc_msg) - off < dataoutl)
			return (ENOBUFS);
		ipcc_encode_bytes(dataout, dataoutl, ipcc_msg, &off);
		LOG("Additional data length: 0x%lx\n", dataoutl);
		LOGHEX("DATA OUT", dataout, dataoutl);
	}

	if ((err = ipcc_msg_fini(ipcc_msg, sizeof (ipcc_msg), &off)) != 0)
		return (err);

	if (IPCC_COBS_SIZE(off) > sizeof (ipcc_pkt) - 1)
		return (ENOBUFS);

	LOGHEX("     OUT", ipcc_msg, off);
	if (!ipcc_cobs_encode(ipcc_msg, off,
	    ipcc_pkt, sizeof (ipcc_pkt), &pktl)) {
		/*
		 * This should never happen since ipcc_pkt is sized based on
		 * ipcc_msg, accounting for the maximum COBS overhead.
		 */
		return (ENOBUFS);
	}
	LOGHEX("COBS OUT", ipcc_pkt, pktl);
	/* Add frame terminator. */
	ipcc_pkt[pktl++] = 0;

	err = ipcc_pkt_send(ipcc_pkt, pktl, ops, arg);

	if (err == ESPINTR) {
		/* The SP-to-host interrupt line was asserted. */
		if ((err = ipcc_sp_interrupt(ops, arg)) != 0)
			return (err);
		goto resend;
	}

	if (err != 0)
		return (err);

	if (expected_rcmd == IPCC_SP_NONE) {
		/* No response expected. */
		return (0);
	}

reread:

	err = ipcc_pkt_recv(ipcc_pkt, sizeof (ipcc_pkt), &end, ops, arg);

	if (err == ESPINTR) {
		/* The SP-to-host interrupt line was asserted. */
		if ((err = ipcc_sp_interrupt(ops, arg)) != 0)
			return (err);
		goto resend;
	}

	if (err == ENOBUFS || end == NULL) {
		LOG("Could not find frame terminator\n");
		goto resend;
	}

	if (err != 0)
		return (err);

	if (end == ipcc_pkt) {
		LOG("Received empty frame\n");
		goto reread;
	}

	/* Decode the frame */
	LOGHEX(" COBS IN", ipcc_pkt, end - ipcc_pkt);
	if (!ipcc_cobs_decode(ipcc_pkt, end - ipcc_pkt,
	    ipcc_msg, sizeof (ipcc_msg), &pktl)) {
		LOG("Error decoding COBS frame\n");
		goto resend;
	}
	LOGHEX("      IN", ipcc_msg, pktl);
	if (pktl < IPCC_MIN_MESSAGE_SIZE) {
		LOG("Short message received - 0x%lx byte(s)\n", pktl);
		goto resend;
	}

	rcvd_datal = pktl - IPCC_MIN_MESSAGE_SIZE;
	LOG("Additional data length: 0x%lx\n", rcvd_datal);

	/* Validate checksum */
	off = pktl - 2;
	crc = ipcc_fletcher16(ipcc_msg, off);
	ipcc_decode_bytes((uint8_t *)&rcvd_crc, sizeof (rcvd_crc),
	    ipcc_msg, &off);

	if (crc != rcvd_crc) {
		LOG("Checksum mismatch got 0x%x calculated 0x%x\n",
		    rcvd_crc, crc);
		goto resend;
	}

	off = 0;
	ipcc_decode_bytes((uint8_t *)&rcvd_magic, sizeof (rcvd_magic),
	    ipcc_msg, &off);
	ipcc_decode_bytes((uint8_t *)&rcvd_version, sizeof (rcvd_version),
	    ipcc_msg, &off);
	ipcc_decode_bytes((uint8_t *)&rcvd_seq, sizeof (rcvd_seq),
	    ipcc_msg, &off);
	ipcc_decode_bytes((uint8_t *)&rcvd_cmd, sizeof (rcvd_cmd),
	    ipcc_msg, &off);

	if (rcvd_magic != IPCC_MAGIC) {
		LOG("Invalid magic number in response, 0x%x\n", rcvd_magic);
		goto resend;
	}
	if (rcvd_version != IPCC_PROTOCOL_VERSION) {
		LOG("Invalid version field in response, 0x%x\n", rcvd_version);
		goto resend;
	}
	if (!(rcvd_seq & IPCC_SEQ_REPLY)) {
		LOG("Response not a reply (sequence 0x%016lx)\n", rcvd_seq);
		goto resend;
	}
	if (rcvd_cmd == IPCC_SP_DECODEFAIL && rcvd_seq == 0xffffffffffffffff) {
		LOG("Decode failed, sequence ignored.\n");
	} else {
		rcvd_seq &= IPCC_SEQ_MASK;
		if (rcvd_seq != send_seq) {
			LOG("Incorrect sequence in response "
			    "(0x%lx) vs expected (0x%lx)\n",
			    rcvd_seq, send_seq);
			/*
			 * If we've received the wrong sequence number from
			 * the SP in an otherwise valid packet, then we are
			 * out of sync. Discard and read again.
			 */
			goto reread;
		}
	}
	if (rcvd_cmd == IPCC_SP_DECODEFAIL) {
		if (rcvd_datal != 1) {
			LOG("SP failed to decode packet (no reason sent)\n");
		} else {
			uint8_t dfreason;

			ipcc_decode_bytes(&dfreason, sizeof (dfreason),
			    ipcc_msg, &off);

			LOG("SP failed to decode packet (reason 0x%x - %s)\n",
			    dfreason, ipcc_failure_str(dfreason));
		}
		goto resend;
	}
	if (rcvd_cmd != expected_rcmd) {
		LOG("Incorrect reply cmd: got 0x%x, expected 0x%x\n",
		    rcvd_cmd, expected_rcmd);
		goto resend;
	}

	if (datainl != NULL && *datainl > 0 && *datainl != rcvd_datal) {
		LOG("Incorrect data length in reply - "
		    "got 0x%lx expected 0x%lx\n",
		    rcvd_datal, *datainl);
		/*
		 * Given all of the other checks have passed, and this looks
		 * like a valid message, there is no benefit in re-attempting
		 * the request...
		 */
		return (EIO);
	}

	if (rcvd_datal > 0) {
		LOGHEX(" DATA IN", ipcc_msg + off, rcvd_datal);

		if (datain == NULL || datainl == NULL) {
			LOG("No storage provided for incoming data - "
			    "received 0x%lx byte(s)\n", rcvd_datal);
			return (EINVAL);
		}

		*datain = ipcc_msg + off;
		*datainl = rcvd_datal;
	} else {
		if (datain != NULL)
			*datain = NULL;
		if (datainl != NULL)
			*datainl = 0;
	}

	return (err);
}

static int
ipcc_status_locked(const ipcc_ops_t *ops, void *arg, uint64_t *status)
{
	uint8_t *data;
	size_t datal = IPCC_STATUS_DATALEN;
	size_t off;
	int err = 0;

	err = ipcc_command_locked(ops, arg, IPCC_HSS_STATUS, IPCC_SP_STATUS,
	    NULL, 0, &data, &datal);

	if (err != 0)
		return (err);

	off = 0;
	ipcc_decode_bytes((uint8_t *)status, sizeof (*status), data, &off);

	return (0);
}

static int
ipcc_handle_alerts(const ipcc_ops_t *ops, void *arg)
{
	int err = 0;

	for (;;) {
		uint8_t *data;
		size_t datal = 0;
		size_t off;
		uint8_t action;
		int err = 0;

		err = ipcc_command_locked(ops, arg,
		    IPCC_HSS_ALERT, IPCC_SP_ALERT, NULL, 0, &data, &datal);

		if (err != 0)
			break;

		off = 0;
		ipcc_decode_bytes((uint8_t *)&action, sizeof (action),
		    data, &off);
		datal -= sizeof (action);

		if (action == 0)
			break;	/* No more alerts */

		/*
		 * XXX - no alerts are currently defined by the SP.
		 *	 Once they are, it may make sense to add an additional
		 *	 callback vector to the ops array rather than just
		 *	 calling cmn_err().
		 *	 Possible future actions here could include asking for
		 *	 an alert message to be delivered to sled agent in some
		 *	 way.
		 */
		LOG("ALERT %u '%-.*s\n", action, (int)datal, data);
		/*
		 * For now, use cmn_err to display/log any alerts received.
		 */
		cmn_err(CE_NOTE, "SP ALERT %u '%-.*s", action,
		    (int)datal, data);
	}

	return (err);
}

static int
ipcc_process_status(const ipcc_ops_t *ops, void *arg)
{
	uint64_t status;
	int err = 0;

	for (;;) {
		bool act = false;

		err = ipcc_status_locked(ops, arg, &status);
		if (err != 0)
			break;

		LOG("SP status register is %lx\n", status);

		if (status == 0)
			break;

		if ((status & IPCC_STATUS_STARTED) != 0) {
			LOG("SP task has (re)started\n");
			err = ipcc_command_locked(ops, arg, IPCC_HSS_ACKSTART,
			    IPCC_SP_ACK, NULL, 0, NULL, NULL);
			if (err != 0)
				break;
			act = true;
		}

		if ((status & IPCC_STATUS_ALERT) != 0) {
			LOG("SP alerts available\n");
			err = ipcc_handle_alerts(ops, arg);
			if (err != 0)
				break;
			act = true;
		}

		if (!act) {
			panic("ipcc: unknown bits set in "
			    "SP status register %lx", status);
		}
	}

	return (err);
}

static int
ipcc_sp_interrupt(const ipcc_ops_t *ops, void *arg)
{
	ipcc_ops_t nops = { 0, };
	int err = 0;

	VERIFY(ipcc_channel_held());

	/* Return if the interrupt is not currently asserted. */
	if (ops->io_readintr == NULL || !ops->io_readintr(arg))
		return (0);

	LOG("SP interrupt received\n");

	/*
	 * The SP's interrupt has been asserted. Attempt to process the
	 * status register, which will implicitly flush the FIFOs, but first
	 * disable the interrupt read operation so we do not end up back here.
	 */
	nops = *ops;
	nops.io_readintr = NULL;

	err = ipcc_process_status(&nops, arg);
	if (err != 0)
		return (err);

	return (0);
}

static int
ipcc_command(const ipcc_ops_t *ops, void *arg,
    ipcc_hss_cmd_t cmd, ipcc_sp_cmd_t expected_rcmd,
    uint8_t *dataout, size_t dataoutl)
{
	int err;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);
	err = ipcc_command_locked(ops, arg, cmd, expected_rcmd,
	    dataout, dataoutl, NULL, NULL);
	ipcc_release_channel(ops, arg, true);

	return (err);
}

int
ipcc_reboot(const ipcc_ops_t *ops, void *arg)
{
	/*
	 * There is a wrinkle here. We can be called from a number of contexts
	 * that want to effect a reboot. This includes being called as a result
	 * of panic or from the kernel debugger - via kdi_reboot(). In some of
	 * those contexts it is possible that an IPCC command is in already in
	 * progress, or at least that locks are held that will prevent us from
	 * issuing the reboot command. We're on our way down to reboot, no
	 * other thread will run again, disable locking before proceeding. The
	 * reboot request may still fail, but the SP should see the new message
	 * arrive even if it is still working on another, and reset state.
	 */
	ipcc_multithreaded = false;
	ipcc_channel_active = false;
	return (ipcc_command(ops, arg, IPCC_HSS_REBOOT, IPCC_SP_NONE, NULL, 0));
}

int
ipcc_poweroff(const ipcc_ops_t *ops, void *arg)
{
	return (ipcc_command(ops, arg, IPCC_HSS_POWEROFF, IPCC_SP_NONE,
	    NULL, 0));
}

int
ipcc_panic(const ipcc_ops_t *ops, void *arg, uint8_t *data, size_t datal)
{
	/*
	 * Like reboot above, if we're panicking then it is possible that the
	 * channel is already held. We are now the only thread that will call
	 * in here, override any existing owner.
	 * This command requires a response. Sending a panic message is not
	 * immediately terminal, since we still have to perform a system dump
	 * if configured to do so.
	 */
	ipcc_multithreaded = false;
	ipcc_channel_active = false;
	return (ipcc_command(ops, arg, IPCC_HSS_PANIC, IPCC_SP_ACK,
	    data, datal));
}

int
ipcc_ackstart(const ipcc_ops_t *ops, void *arg)
{
	return (ipcc_command(ops, arg, IPCC_HSS_ACKSTART, IPCC_SP_ACK,
	    NULL, 0));
}

int
ipcc_bsu(const ipcc_ops_t *ops, void *arg, uint8_t *bsu)
{
	uint8_t *data;
	size_t datal = IPCC_BSU_DATALEN;
	int err = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_BSU, IPCC_SP_BSU,
	    NULL, 0, &data, &datal);
	if (err != 0)
		goto out;

	*bsu = *data;

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

int
ipcc_ident(const ipcc_ops_t *ops, void *arg, ipcc_ident_t *ident)
{
	uint8_t *data;
	size_t datal = IPCC_IDENT_DATALEN;
	size_t off;
	int err = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_IDENT, IPCC_SP_IDENT,
	    NULL, 0, &data, &datal);
	if (err != 0)
		goto out;

	bzero(ident, sizeof (*ident));
	off = 0;
	ipcc_decode_bytes((uint8_t *)&ident->ii_model, sizeof (ident->ii_model),
	    data, &off);
	ipcc_decode_bytes((uint8_t *)&ident->ii_rev, sizeof (ident->ii_rev),
	    data, &off);
	ipcc_decode_bytes((uint8_t *)&ident->ii_serial,
	    sizeof (ident->ii_serial), data, &off);

	/* The SP should nul terminate this but make sure. */
	ident->ii_model[sizeof (ident->ii_model) - 1] = '\0';
	ident->ii_serial[sizeof (ident->ii_serial) - 1] = '\0';

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

int
ipcc_macs(const ipcc_ops_t *ops, void *arg, ipcc_mac_t *mac)
{
	uint8_t *data;
	size_t datal = IPCC_MAC_DATALEN;
	size_t off;
	int err = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_MACS, IPCC_SP_MACS,
	    NULL, 0, &data, &datal);
	if (err != 0)
		goto out;

	off = 0;
	ipcc_decode_bytes((uint8_t *)&mac->im_base, sizeof (mac->im_base),
	    data, &off);
	ipcc_decode_bytes((uint8_t *)&mac->im_count, sizeof (mac->im_count),
	    data, &off);
	ipcc_decode_bytes((uint8_t *)&mac->im_stride, sizeof (mac->im_stride),
	    data, &off);

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

int
ipcc_keylookup(const ipcc_ops_t *ops, void *arg, ipcc_keylookup_t *klookup,
    uint8_t *response)
{
	uint8_t buf[sizeof (uint8_t) + sizeof (uint16_t)];
	size_t off, datal = 0;
	uint8_t *data;
	int err = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	off = 0;
	ipcc_encode_bytes((uint8_t *)&klookup->ik_key,
	    sizeof (klookup->ik_key), buf, &off);
	ipcc_encode_bytes((uint8_t *)&klookup->ik_buflen,
	    sizeof (klookup->ik_buflen), buf, &off);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_KEYLOOKUP,
	    IPCC_SP_KEYLOOKUP, buf, off, &data, &datal);
	if (err != 0)
		goto out;

	if (datal < sizeof (klookup->ik_result)) {
		LOG("Short keylookup reply - got 0x%lx bytes\n", datal);
		err = EIO;
		goto out;
	}

	off = 0;
	ipcc_decode_bytes((uint8_t *)&klookup->ik_result,
	    sizeof (klookup->ik_result), data, &off);

	if (datal - off > klookup->ik_buflen) {
		LOG("Too much data in keylookup response - "
		    "got 0x%lx bytes (buffer 0x%lx)\n", datal,
		    klookup->ik_buflen);
		err = EOVERFLOW;
		goto out;
	}

	klookup->ik_datalen = datal - off;
	bcopy(data + off, response, datal - off);

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

int
ipcc_keyset(const ipcc_ops_t *ops, void *arg, ipcc_keyset_t *kset)
{
	size_t inputl, outputl, off;
	uint8_t *input, *output;
	int err = 0;

	inputl = sizeof (kset->iks_key) + kset->iks_datalen;
	if (inputl > IPCC_MAX_DATA_SIZE)
		return (EINVAL);

	input = kmem_alloc(inputl, KM_SLEEP);
	off = 0;
	ipcc_encode_bytes(&kset->iks_key, sizeof (kset->iks_key),
	    input, &off);
	ipcc_encode_bytes(kset->iks_data, kset->iks_datalen,
	    input, &off);

	if ((err = ipcc_acquire_channel(ops, arg)) != 0) {
		kmem_free(input, inputl);
		return (err);
	}

	outputl = IPCC_KEYSET_DATALEN;
	err = ipcc_command_locked(ops, arg, IPCC_HSS_KEYSET,
	    IPCC_SP_KEYSET, input, off, &output, &outputl);
	if (err != 0)
		goto out;

	off = 0;
	ipcc_decode_bytes(&kset->iks_result, sizeof (kset->iks_result),
	    output, &off);

out:
	ipcc_release_channel(ops, arg, true);

	kmem_free(input, inputl);
	return (err);
}

int
ipcc_rot(const ipcc_ops_t *ops, void *arg, ipcc_rot_t *rot)
{
	int err = 0;
	uint8_t *data;
	size_t datal = 0;

	if (rot->ir_len == 0 || rot->ir_len > sizeof (rot->ir_data)) {
		LOG("Invalid RoT request length %zu; "
		    "must be in range (0, %zu]",
		    rot->ir_len, sizeof (rot->ir_data));
		return (EINVAL);
	}

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_ROT, IPCC_SP_ROT,
	    rot->ir_data, rot->ir_len, &data, &datal);
	if (err != 0)
		goto out;

	if (datal > sizeof (rot->ir_data)) {
		LOG("Too much data in RoT response - got 0x%lx bytes\n", datal);
		err = EOVERFLOW;
		goto out;
	}

	rot->ir_len = datal;
	bcopy(data, rot->ir_data, datal);

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

int
ipcc_bootfail(const ipcc_ops_t *ops, void *arg, ipcc_host_boot_failure_t type,
    const uint8_t *msg, size_t len)
{
	size_t datal;
	uint8_t *data;
	int err = 0;

	datal = MIN(len, IPCC_BOOTFAIL_MAX_PAYLOAD);
	datal += sizeof (uint8_t);

	data = kmem_alloc(datal, KM_SLEEP);

	*data = (uint8_t)type;
	bcopy(msg, data + sizeof (uint8_t), len);

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		goto out;
	err = ipcc_command_locked(ops, arg, IPCC_HSS_BOOTFAIL, IPCC_SP_ACK,
	    data, datal, NULL, NULL);
	ipcc_release_channel(ops, arg, true);

out:
	kmem_free(data, datal);
	return (err);
}

int
ipcc_status(const ipcc_ops_t *ops, void *arg, uint64_t *status, uint64_t *debug)
{
	uint8_t *data;
	size_t datal = IPCC_STATUS_DATALEN;
	size_t off;
	int err = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	err = ipcc_command_locked(ops, arg, IPCC_HSS_STATUS, IPCC_SP_STATUS,
	    NULL, 0, &data, &datal);
	if (err != 0)
		goto out;

	off = 0;
	ipcc_decode_bytes((uint8_t *)status, sizeof (*status), data, &off);
	ipcc_decode_bytes((uint8_t *)debug, sizeof (*debug), data, &off);

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}

/*
 * Retrieving a phase 2 image from the SP involves transferring a number of
 * data blocks over a period of time. Rather than copy data unecessarily,
 * the boot module holds the channel throughout so that it can safely access
 * data in the global static packet buffer.
 * The start parameter indicates the byte offset of the image at which the SP
 * should start the response block; the size of the response is variable up to
 * MAX_MESSAGE_SIZE.
 */
int
ipcc_imageblock(const ipcc_ops_t *ops, void *arg, uint8_t *hash,
    uint64_t start, uint8_t **data, size_t *datal)
{
	uint8_t buf[sizeof (start) + IPCC_IMAGE_HASHLEN];
	size_t off;

	VERIFY(ipcc_channel_held());

	off = 0;
	ipcc_encode_bytes(hash, IPCC_IMAGE_HASHLEN, buf, &off);
	ipcc_encode_bytes((uint8_t *)&start, sizeof (start), buf, &off);

	*datal = 0;
	return (ipcc_command_locked(ops, arg, IPCC_HSS_IMAGEBLOCK,
	    IPCC_SP_IMAGEBLOCK, buf, off, data, datal));
}

/*
 * Read inventory data about a specific inventory index.
 *
 * The minimum response that we are guaranteed is that where the result
 * indicates an invalid index, in which case the only thing that'll be
 * valid is the basic uint8_t result data. If we get any kind of
 * communication failure, then we're also guaranteed that the 32-byte
 * name field will be plugged in so we know what it was that failed.
 *
 * Only if we get a successful return value (IPCC_INVENTORY_SUCCESS) will we
 * then be able to fill in the type field. Any remaining data becomes the actual
 * data field.
 */
int
ipcc_inventory(const ipcc_ops_t *ops, void *arg, ipcc_inventory_t *inv)
{
	size_t off, datal = 0;
	uint8_t *data;
	int err = 0;
	bool do_full = false;

	const size_t min = sizeof (inv->iinv_res);
	const size_t min_name = min + sizeof (inv->iinv_name);
	const size_t min_success = min_name + sizeof (inv->iinv_type);

	bzero(inv->iinv_name, sizeof (inv->iinv_name));
	bzero(inv->iinv_data, sizeof (inv->iinv_data_len));
	inv->iinv_type = 0;
	inv->iinv_data_len = 0;

	if ((err = ipcc_acquire_channel(ops, arg)) != 0)
		return (err);

	off = sizeof (inv->iinv_idx);
	err = ipcc_command_locked(ops, arg, IPCC_HSS_INVENTORY,
	    IPCC_SP_INVENTORY, (uint8_t *)&inv->iinv_idx, off, &data, &datal);
	if (err != 0)
		goto out;

	if (datal < min) {
		LOG("Short inventory initial reply - got 0x%lx bytes\n", datal);
		err = EIO;
		goto out;
	}

	off = 0;
	ipcc_decode_bytes(&inv->iinv_res, sizeof (inv->iinv_res), data, &off);
	switch (inv->iinv_res) {
	case IPCC_INVENTORY_SUCCESS:
		do_full = true;
		break;
	case IPCC_INVENTORY_IO_DEV_MISSING:
	case IPCC_INVENTORY_IO_ERROR:
		break;
	case IPCC_INVENTORY_INVALID_INDEX:
	default:
		goto out;
	}

	if (datal < min_name) {
		LOG("Short inventory, missing name - got 0x%lx bytes\n", datal);
		err = EIO;
		goto out;
	}

	ipcc_decode_bytes(inv->iinv_name, sizeof (inv->iinv_name), data, &off);
	inv->iinv_name[IPCC_INVENTORY_NAMELEN - 1] = '\0';
	if (!do_full) {
		goto out;
	}

	if (datal < min_success) {
		LOG("Short inventory, missing type - got 0x%lx bytes\n", datal);
		err = EIO;
		goto out;
	}

	ipcc_decode_bytes(&inv->iinv_type, sizeof (inv->iinv_type), data, &off);
	if (datal - off > sizeof (inv->iinv_data)) {
		LOG("inventory data payload would overflow data buffer - got "
		    "0x%lx bytes\n", datal);
		err = EOVERFLOW;
		goto out;
	}

	inv->iinv_data_len = datal - off;
	if (inv->iinv_data_len == 0)
		goto out;
	ipcc_decode_bytes(inv->iinv_data, inv->iinv_data_len, data, &off);

out:
	ipcc_release_channel(ops, arg, true);
	return (err);
}
