/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2022 Oxide Computer Company
 */

/*
 * Oxide Image Boot: Network image source.  Fetches an appropriate ramdisk
 * image from a local boot server over Ethernet.
 *
 * PROTOCOL OVERVIEW
 *
 * There are two distinct systems in the protocol: the boot server, which has
 * the images; and the client, the system that is trying to boot.  This is an
 * Ethernet-level protocol, using the custom ethertype 0x1DE0.  Because it is
 * not IP, without a repeater of some kind it only works locally on a single
 * network segment.
 *
 * I. Boot Server Discovery
 *
 * When the system comes up, it does not have any prior information about where
 * to get the boot image.  In order to locate a remote system from which to
 * obtain the image, the client first sends a HELLO frame as an Ethernet
 * broadcast.  HELLO frames are sent every four seconds until a boot server
 * responds to the client.
 *
 * When a boot server wants to offer an image, it will reply to the HELLO
 * broadcast with a unicast OFFER frame directed at the client.  The OFFER
 * contains metadata about the image the server is trying to provide, such as
 * the SHA-256 checksum of the image contents and the size of the image.  If
 * the checksum in the OFFER matches the one in the system boot archive, the
 * client can proceed with boot.  Otherwise, the client ignores the OFFER and
 * waits for one with appropriate properties; this eases the use of multiple
 * boot servers with different images on the same network segment.
 *
 * II. Image Transfer
 *
 * Once an OFFER has been accepted, the client then reads the ramdisk from the
 * boot server.  The client will sweep from the beginning of the image (at
 * offset 0) up to the end, requesting 1024 byte chunks from the boot server
 * through unicast READ requests.  The server will send the data to the client
 * through unicast DATA responses, which include both the offset for the data
 * and the data itself.
 *
 * In order to cut down on packets sent to the boot server, the client can
 * bundle up to 128 starting offsets for 1024 byte chunks into a single READ
 * frame and the boot server will send each of them to us in turn.  The client
 * is responsible for tracking which reads are outstanding and when to request
 * retransmission of potentially dropped messages.  In the current
 * implementation, the client assumes read requests that have not been serviced
 * within a second have been dropped, and sends another request for the same
 * offset.  To avoid entering a permanent stall due to congestion, the client
 * presently waits for a full batch of 128 offsets to be serviced before
 * starting a new batch of 128.
 *
 * III. Reporting Completion
 *
 * To ease automated control of systems in the lab and during manufacturing,
 * when the image has been completely read by the client it sends a final
 * FINISHED frame to the boot server.  The boot server can use this signal to
 * move on to other stages of processing.
 *
 * IV. Reset On Errors
 *
 * If the client asks for something unexpected, the boot server is able to
 * interrupt the client and restart the entire process by sending a unicast
 * RESET frame.  The client presently panics on receipt of such a message, but
 * could be enhanced to simply tear down and try again.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <net/if.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/ddi.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/ddidevmap.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/ramdisk.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>
#include <sys/time.h>
#include <sys/sysmacros.h>

#include "oxide_boot.h"

/*
 * Ethernet boot protocol definitions.
 *
 * These are shared with boot server software from the Oxide "boot-image-tools"
 * repository.
 */
#define	OXBOOT_NET_TYPE_HELLO		0x9001
#define	OXBOOT_NET_TYPE_OFFER		0x9102
#define	OXBOOT_NET_TYPE_READ		0x9003
#define	OXBOOT_NET_TYPE_DATA		0x9104
#define	OXBOOT_NET_TYPE_FINISHED	0x9005
#define	OXBOOT_NET_TYPE_RESET		0x9106

#define	OXBOOT_NET_ETHERTYPE		0x1DE0
#define	OXBOOT_NET_MAGIC		0x1DE12345

#define	OXBOOT_NET_READ_SZ		1024

#define	OXBOOT_NET_DATASET_SZ		128
#define	OXBOOT_NET_NOFFSETS		128

typedef struct oxide_boot_net_frame_header {
	struct ether_header ofh_ether;
	uint32_t ofh_magic;
	uint32_t ofh_type;
	uint32_t ofh_len;
} __packed oxide_boot_net_frame_header_t;

typedef struct oxide_boot_net_frame_offer {
	oxide_boot_net_frame_header_t ofo_header;
	uint64_t ofo_ramdisk_size;
	uint64_t ofo_ramdisk_data_size;
	uint8_t ofo_sha256[OXBOOT_CSUMLEN_SHA256];
	char ofo_dataset[OXBOOT_NET_DATASET_SZ];
} __packed oxide_boot_net_frame_offer_t;

#define	OXBOOT_NET_LEN_OFFER	(sizeof (oxide_boot_net_frame_offer_t) - \
				sizeof (oxide_boot_net_frame_header_t))

typedef struct oxide_boot_net_frame_data {
	oxide_boot_net_frame_header_t ofd_header;
	uint64_t ofd_offset;
} __packed oxide_boot_net_frame_data_t;

#define	OXBOOT_NET_LEN_DATA	(sizeof (oxide_boot_net_frame_data_t) - \
				sizeof (oxide_boot_net_frame_header_t))

typedef struct oxide_boot_net_frame_read {
	oxide_boot_net_frame_header_t ofr_header;
	uint64_t ofr_noffsets;
	uint64_t ofr_offsets[OXBOOT_NET_NOFFSETS];
} __packed oxide_boot_net_frame_read_t;

#define	OXBOOT_NET_LEN_READ	(sizeof (oxide_boot_net_frame_read_t) - \
				sizeof (oxide_boot_net_frame_header_t))

#define	OXBOOT_NET_LEN_RESET	0
#define	OXBOOT_NET_LEN_FINISHED	0

/*
 * Ethernet protocol state machine.
 */
typedef enum oxide_boot_net_ether_state {
	OXBOOT_NET_STATE_REST,
	OXBOOT_NET_STATE_READING,
	OXBOOT_NET_STATE_FINISHED,
} oxide_boot_net_ether_state_t;

typedef struct oxide_boot_net_ether {
	kmutex_t oe_mutex;
	kcondvar_t oe_cv;
	uint64_t oe_npkts;
	ether_addr_t oe_macaddr;
	ether_addr_t oe_server;

	oxide_boot_net_ether_state_t oe_state;
	hrtime_t oe_download_start;
	hrtime_t oe_last_hello;
	hrtime_t oe_last_status;
	bool oe_reset;

	bool oe_eof;
	uint64_t oe_offsets[OXBOOT_NET_NOFFSETS];
	hrtime_t oe_offset_time[OXBOOT_NET_NOFFSETS];
	uint64_t oe_offset;
	uint64_t oe_data_size;
	mblk_t *oe_q;
} oxide_boot_net_ether_t;

typedef struct oxide_boot_net_find_ether {
	boolean_t ofe_print_only;
	char ofe_linkname[MAXLINKNAMELEN];
} oxide_boot_net_find_ether_t;

static int
oxide_boot_net_find_ether(dev_info_t *dip, void *arg)
{
	oxide_boot_net_find_ether_t *ofe = arg;

	if (i_ddi_devi_class(dip) == NULL ||
	    strcmp(i_ddi_devi_class(dip), ESC_NETWORK) != 0) {
		/*
		 * We do not think that this is a network interface.
		 */
		return (DDI_WALK_CONTINUE);
	}

	if (ofe->ofe_print_only) {
		printf("    %s%d\n",
		    ddi_driver_name(dip),
		    i_ddi_devi_get_ppa(dip));
	}

	/*
	 * If we have not picked a NIC yet, accept any NIC.  If we see either a
	 * vioif NIC or an Intel NIC, prefer those for now.
	 */
	if (ofe->ofe_linkname[0] == '\0' ||
	    strncmp(ddi_driver_name(dip), "igb", 3) == 0 ||
	    strncmp(ddi_driver_name(dip), "e1000g", 6) == 0 ||
	    strncmp(ddi_driver_name(dip), "vioif", 5) == 0) {
		(void) snprintf(ofe->ofe_linkname, sizeof (ofe->ofe_linkname),
		    "%s%d", ddi_driver_name(dip), i_ddi_devi_get_ppa(dip));
	}

	return (DDI_WALK_CONTINUE);
}

static void
oxide_boot_net_ether_rx(void *arg, mac_resource_handle_t mrh, mblk_t *m,
    boolean_t is_loopback)
{
	oxide_boot_net_ether_t *oe = arg;

	if (is_loopback) {
		goto drop;
	}

	while (m != NULL) {
		mutex_enter(&oe->oe_mutex);
		bool reset = oe->oe_reset;
		mutex_exit(&oe->oe_mutex);

		if (reset) {
			goto drop;
		}

		mblk_t *next = m->b_next;
		m->b_next = NULL;

		if (m->b_cont != NULL) {
			mblk_t *nm;
			if ((nm = msgpullup(m,
			    sizeof (oxide_boot_net_frame_header_t))) == NULL) {
				goto next;
			}
			freemsg(m);
			m = nm;
		}

		if (MBLKL(m) < sizeof (oxide_boot_net_frame_header_t)) {
			goto next;
		}

		oxide_boot_net_frame_header_t *ofh = (void *)m->b_rptr;
		if (ntohl(ofh->ofh_magic) != OXBOOT_NET_MAGIC) {
			goto next;
		}

		/*
		 * Decide what to do with this message type.
		 */
		switch (ntohl(ofh->ofh_type)) {
		case OXBOOT_NET_TYPE_OFFER:
			if (ntohl(ofh->ofh_len) != OXBOOT_NET_LEN_OFFER) {
				goto next;
			} else {
				/*
				 * Pull the whole message up.
				 */
				mblk_t *nm;
				if ((nm = msgpullup(m, -1)) == NULL) {
					goto next;
				}
				freemsg(m);
				m = nm;
			}
			break;
		case OXBOOT_NET_TYPE_DATA:
			if (ntohl(ofh->ofh_len) > 1476 ||
			    ntohl(ofh->ofh_len) < OXBOOT_NET_LEN_DATA) {
				goto next;
			} else {
				/*
				 * Pull up the offset portion of the frame.
				 */
				size_t pu =
				    sizeof (oxide_boot_net_frame_data_t);
				mblk_t *nm;
				if ((nm = msgpullup(m, pu)) == NULL) {
					goto next;
				}
				freemsg(m);
				m = nm;
			}
			break;
		case OXBOOT_NET_TYPE_RESET:
			if (ntohl(ofh->ofh_len) != OXBOOT_NET_LEN_RESET) {
				goto next;
			}
			mutex_enter(&oe->oe_mutex);
			oe->oe_reset = true;
			mutex_exit(&oe->oe_mutex);
			goto drop;
		default:
			goto next;
		}

		mutex_enter(&oe->oe_mutex);
		if (oe->oe_q == NULL) {
			oe->oe_q = m;
		} else {
			mblk_t *t = oe->oe_q;
			while (t->b_next != NULL) {
				t = t->b_next;
			}
			t->b_next = m;
		}
		m = NULL;
		cv_broadcast(&oe->oe_cv);
		mutex_exit(&oe->oe_mutex);

next:
		freemsg(m);
		m = next;
	}

drop:
	freemsgchain(m);
}

static void
oxide_boot_net_set_ether_header(oxide_boot_net_ether_t *oe,
    oxide_boot_net_frame_header_t *ofh, uchar_t *addr)
{
	ofh->ofh_ether.ether_type = htons(OXBOOT_NET_ETHERTYPE);
	(void) memcpy(&ofh->ofh_ether.ether_shost, oe->oe_macaddr, ETHERADDRL);
	if (addr == NULL) {
		/*
		 * Broadcast address:
		 */
		(void) memset(&ofh->ofh_ether.ether_dhost, 0xFF, ETHERADDRL);
	} else {
		(void) memcpy(&ofh->ofh_ether.ether_dhost, addr, ETHERADDRL);
	}
}

static void
oxide_boot_net_send_hello(oxide_boot_net_ether_t *oe, mac_client_handle_t mch)
{
	mutex_exit(&oe->oe_mutex);
	mblk_t *m;
	if ((m = allocb(ETHERMAX, 0)) == NULL) {
		mutex_enter(&oe->oe_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&oe->oe_mutex);

	oxide_boot_net_frame_header_t *ofh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*ofh);
	bzero(ofh, sizeof (*ofh));

	oxide_boot_net_set_ether_header(oe, ofh, NULL);

	ofh->ofh_magic = htonl(OXBOOT_NET_MAGIC);
	ofh->ofh_type = htonl(OXBOOT_NET_TYPE_HELLO);
	size_t len = snprintf((char *)m->b_wptr, 128,
	    "Hello!  I'd like to buy a ramdisk please.");
	m->b_wptr += len;
	ofh->ofh_len = htonl(len);

	mutex_exit(&oe->oe_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&oe->oe_mutex);
}

static void
oxide_boot_net_send_read(oxide_boot_net_ether_t *oe, mac_client_handle_t mch)
{
	mutex_exit(&oe->oe_mutex);
	mblk_t *m;
	if ((m = allocb(ETHERMAX, 0)) == NULL) {
		mutex_enter(&oe->oe_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&oe->oe_mutex);

	oxide_boot_net_frame_read_t *ofr = (void *)m->b_wptr;
	m->b_wptr += sizeof (*ofr);
	bzero(ofr, sizeof (*ofr));

	oxide_boot_net_set_ether_header(oe, &ofr->ofr_header, oe->oe_server);

	ofr->ofr_header.ofh_magic = htonl(OXBOOT_NET_MAGIC);
	ofr->ofr_header.ofh_type = htonl(OXBOOT_NET_TYPE_READ);
	ofr->ofr_header.ofh_len = htonl(OXBOOT_NET_LEN_READ);

	uint64_t noffsets = 0;
	hrtime_t now = gethrtime();
	for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
		if (oe->oe_offsets[n] == UINT64_MAX) {
			continue;
		}

		if (oe->oe_offset_time[n] != 0 &&
		    now - oe->oe_offset_time[n] < SEC2NSEC(1)) {
			continue;
		}

		oe->oe_offset_time[n] = now;
		ofr->ofr_offsets[n] = htonll(oe->oe_offsets[n]);
		noffsets++;
	}

	if (noffsets == 0) {
		freemsg(m);
		return;
	}

	ofr->ofr_noffsets = htonll(noffsets);

	mutex_exit(&oe->oe_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&oe->oe_mutex);
}

static void
oxide_boot_net_send_finished(oxide_boot_net_ether_t *oe,
    mac_client_handle_t mch)
{
	mutex_exit(&oe->oe_mutex);
	mblk_t *m;
	if ((m = allocb(ETHERMAX, 0)) == NULL) {
		mutex_enter(&oe->oe_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&oe->oe_mutex);

	oxide_boot_net_frame_header_t *ofh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*ofh);
	bzero(ofh, sizeof (*ofh));

	oxide_boot_net_set_ether_header(oe, ofh, oe->oe_server);

	ofh->ofh_magic = htonl(OXBOOT_NET_MAGIC);
	ofh->ofh_type = htonl(OXBOOT_NET_TYPE_FINISHED);
	ofh->ofh_len = htonl(OXBOOT_NET_LEN_FINISHED);

	mutex_exit(&oe->oe_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&oe->oe_mutex);
}

static mblk_t *
oxide_boot_net_next(oxide_boot_net_ether_t *oe)
{
	mblk_t *m;

	if ((m = oe->oe_q) != NULL) {
		oe->oe_q = m->b_next;
		m->b_next = NULL;
		VERIFY3U(MBLKL(m), >=, sizeof (oxide_boot_net_frame_header_t));
	}

	return (m);
}

static int
oxide_boot_net_ether_turn(oxide_boot_t *oxb, oxide_boot_net_ether_t *oe,
    mac_client_handle_t mch)
{
	mblk_t *m;

	if (oe->oe_reset) {
		/*
		 * The boot server has determined that we need to panic and try
		 * again.
		 */
		panic("boot server requested a reset");
	}

	switch (oe->oe_state) {
	case OXBOOT_NET_STATE_REST:
		/*
		 * First, check to see if we have any offers.
		 */
		while ((m = oxide_boot_net_next(oe)) != NULL) {
			oxide_boot_net_frame_header_t *ofh = (void *)m->b_rptr;

			if (ntohl(ofh->ofh_type) != OXBOOT_NET_TYPE_OFFER) {
				freemsg(m);
				continue;
			}

			oxide_boot_net_frame_offer_t *ofo = (void *)m->b_rptr;
			VERIFY3U(MBLKL(m), >=, sizeof (*ofo));

			/*
			 * Make sure the dataset name is correctly
			 * null-terminated.
			 */
			if (ofo->ofo_dataset[OXBOOT_NET_DATASET_SZ - 1] !=
			    '\0') {
				freemsg(m);
				continue;
			}

			/*
			 * The ramdisk has a size, and the image that we will
			 * download into the beginning of the ramdisk has an
			 * equal-or-smaller size.
			 */
			size_t size = ntohll(ofo->ofo_ramdisk_size);
			size_t data_size = ntohll(ofo->ofo_ramdisk_data_size);
			if (size < 1024 * 1024 || data_size < 1024 * 1024 ||
			    data_size > size) {
				freemsg(m);
				continue;
			}

			if (!oxide_boot_ramdisk_set_csum(oxb, ofo->ofo_sha256,
			    sizeof (ofo->ofo_sha256))) {
				/*
				 * This image does not match the cpio archive,
				 * so we ignore it.
				 */
				printf("ignoring offer (checksum mismatch)\n");
				freemsg(m);
				continue;
			}

			bcopy(&ofh->ofh_ether.ether_shost,
			    &oe->oe_server, ETHERADDRL);

			printf("received offer from "
			    "%02x:%02x:%02x:%02x:%02x:%02x "
			    " -- size %lu data size %lu dataset %s\n",
			    oe->oe_server[0],
			    oe->oe_server[1],
			    oe->oe_server[2],
			    oe->oe_server[3],
			    oe->oe_server[4],
			    oe->oe_server[5],
			    size,
			    data_size,
			    ofo->ofo_dataset);

			/*
			 * Create a ramdisk of this size.
			 */
			if (!oxide_boot_ramdisk_create(oxb, size)) {
				/*
				 * If we could not open the ramdisk, just panic
				 * for now.
				 */
				panic("could not open ramdisk");
			}

			if (!oxide_boot_ramdisk_set_dataset(oxb,
			    ofo->ofo_dataset)) {
				panic("could not set ramdisk metadata");
			}

			oe->oe_offset = 0;
			oe->oe_data_size = data_size;
			oe->oe_state = OXBOOT_NET_STATE_READING;
			oe->oe_download_start = gethrtime();
			freemsg(m);
			return (0);
		}

		if (oe->oe_last_hello == 0 ||
		    gethrtime() - oe->oe_last_hello > SEC2NSEC(4)) {
			/*
			 * Send a broadcast frame every four seconds.
			 */
			printf("hello...\n");
			oxide_boot_net_send_hello(oe, mch);
			oe->oe_last_hello = gethrtime();
		}
		return (0);

	case OXBOOT_NET_STATE_READING:
		/*
		 * Print a status display that shows roughly our progress in
		 * receiving the image.  On a gigabit network most images
		 * transfer almost immediately, but the USB NICs on some of the
		 * control PCs are a bit slower.
		 */
		if (oe->oe_last_status == 0) {
			printf("\n");
		}
		if (oe->oe_last_status == 0 ||
		    gethrtime() - oe->oe_last_status > SEC2NSEC(1)) {
			uint_t pct = 100UL *
			    oe->oe_offset / oe->oe_data_size;
			printf("\r receiving %016lx / %016lx (%3u%%)    \r",
			    oe->oe_offset, oe->oe_data_size, pct);
			oe->oe_last_status = gethrtime();
		}

		/*
		 * Check to see if we have finished all work.
		 */
		if (oe->oe_eof || oe->oe_offset >= oe->oe_data_size) {
			bool finished = true;
			for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
				if (oe->oe_offsets[n] != UINT64_MAX) {
					finished = false;
					break;
				}
			}

			if (finished) {
				uint64_t secs =
				    (gethrtime() - oe->oe_download_start) /
				    SEC2NSEC(1);
				printf("reached EOF at offset %lu "
				    "after %lu seconds           \n",
				    oe->oe_offset, secs);

				oe->oe_state = OXBOOT_NET_STATE_FINISHED;
				return (0);
			}
		}

		/*
		 * Check to see if we have any data messages.
		 */
		while ((m = oxide_boot_net_next(oe)) != NULL) {
			oxide_boot_net_frame_header_t *ofh = (void *)m->b_rptr;

			if (ntohl(ofh->ofh_type) != OXBOOT_NET_TYPE_DATA) {
				freemsg(m);
				continue;
			}

			oxide_boot_net_frame_data_t *ofd = (void *)m->b_rptr;
			VERIFY3U(MBLKL(m), >=, sizeof (*ofd));

			/*
			 * Check through our list of offsets:
			 */
			uint64_t offset = ntohll(ofd->ofd_offset);
			bool found = false;
			for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
				if (offset == oe->oe_offsets[n]) {
					found = true;
					oe->oe_offsets[n] = UINT64_MAX;
					break;
				}
			}

			if (!found) {
				/*
				 * This is not an offset for which we are
				 * currently expecting data.
				 */
				printf("dropped data packet for offset %lu\n",
				    offset);
				freemsg(m);
				continue;
			}

			/*
			 * The data payload in the frame is whatever is left
			 * after the offset field:
			 */
			size_t datasz = ntohl(ofd->ofd_header.ofh_len) -
			    OXBOOT_NET_LEN_DATA;

			if (datasz == 0) {
				/*
				 * An reply with no data other than the offset
				 * means we have reached EOF.  We still have to
				 * wait for all of our in flight requests to be
				 * serviced.
				 */
				oe->oe_eof = true;
				freemsg(m);
				continue;
			}

			/*
			 * Trim out the header, leaving only the data we
			 * received.
			 */
			m->b_rptr += sizeof (*ofd);

			/*
			 * Write the data into the ramdisk at the expected
			 * offset.
			 */
			iovec_t iov[32];
			bzero(iov, sizeof (*iov));

			size_t total = 0;
			uint_t niov = 0;
			mblk_t *w = m;
			while (w != NULL) {
				if (MBLKL(w) > 0) {
					iov[niov].iov_base = (void *)w->b_rptr;
					iov[niov].iov_len = MBLKL(w);
					total += MBLKL(w);

					VERIFY3U(niov, <, ARRAY_SIZE(iov));
					niov++;
				}

				w = w->b_cont;
			}
			VERIFY3U(total, ==, datasz);

			if (!oxide_boot_ramdisk_write(oxb, iov, niov, offset)) {
				panic("write failure pos %lu", offset);
			}

			freemsg(m);
		}

		/*
		 * Issue reads for offsets we still need if there are
		 * any available slots.
		 */
		bool send = false;
		hrtime_t now = gethrtime();
		if (!oe->oe_eof && oe->oe_offset < oe->oe_data_size) {
			/*
			 * Check to see if we have drained our existing
			 * requests before adding more, to avoid entering a
			 * condition where we are sending as many READ frames
			 * as there are blocks to read -- and further to avoid
			 * a permanent stall condition due to unexpected
			 * congestion on the network segment.
			 */
			bool empty = true;
			for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
				if (oe->oe_offsets[n] != UINT64_MAX) {
					empty = false;
					break;
				}
			}

			if (empty) {
				for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS;
				    n++) {
					if (oe->oe_offsets[n] != UINT64_MAX) {
						/*
						 * This slot is in use.
						 */
						continue;
					}

					send = true;
					oe->oe_offsets[n] = oe->oe_offset;
					oe->oe_offset_time[n] = 0;

					oe->oe_offset += OXBOOT_NET_READ_SZ;
				}
			}
		}

		/*
		 * Check to see if we need to send a packet with our
		 * outstanding offset list.
		 */
		for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
			if (oe->oe_offsets[n] == UINT64_MAX) {
				continue;
			}

			if (oe->oe_offset_time[n] == 0 ||
			    now - oe->oe_offset_time[n] > SEC2NSEC(1)) {
				send = true;
				break;
			}
		}

		if (send) {
			oxide_boot_net_send_read(oe, mch);
		}
		return (0);

	case OXBOOT_NET_STATE_FINISHED:
		oxide_boot_net_send_finished(oe, mch);
		if (!oxide_boot_ramdisk_set_len(oxb, oe->oe_offset)) {
			panic("could not set final image length");
		}
		return (1);

	default:
		panic("unexpected state %d\n", oe->oe_state);
	}
}

bool
oxide_boot_net(oxide_boot_t *oxb)
{
	oxide_boot_net_ether_t oe;
	bzero(&oe, sizeof (oe));
	oe.oe_state = OXBOOT_NET_STATE_REST;
	oe.oe_offset = 0;
	for (uint_t n = 0; n < OXBOOT_NET_NOFFSETS; n++) {
		oe.oe_offsets[n] = UINT64_MAX;
	}
	mutex_init(&oe.oe_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&oe.oe_cv, NULL, CV_DRIVER, NULL);

	oxide_boot_net_find_ether_t ofe = {
		.ofe_print_only = B_TRUE,
	};

	printf("TRYING: boot net\n");

	/*
	 * First, force everything which can attach to do so.  The device class
	 * is not derived until at least one minor mode is created, so we
	 * cannot walk the device tree looking for a device class of
	 * ESC_NETWORK until everything is attached.
	 */
	printf("attaching stuff...\n");
	(void) ndi_devi_config(ddi_root_node(), NDI_CONFIG | NDI_DEVI_PERSIST |
	    NDI_NO_EVENT | NDI_DRV_CONF_REPROBE);

	/*
	 * We need to find and attach the Ethernet device we want.
	 */
	printf("Ethernet interfaces:\n");
	ddi_walk_devs(ddi_root_node(), oxide_boot_net_find_ether, &ofe);
	printf("\n");

	if (ofe.ofe_linkname[0] == '\0') {
		printf("did not find any Ethernet devices!\n");
		return (false);
	}

	int r;
	mac_handle_t mh;
	printf("opening %s handle\n", ofe.ofe_linkname);
	if ((r = mac_open(ofe.ofe_linkname, &mh)) != 0) {
		printf("mac_open failed with %d\n", r);
		return (false);
	}

	printf("opening client handle\n");
	mac_client_handle_t mch;
	if ((r = mac_client_open(mh, &mch, NULL,
	    MAC_OPEN_FLAGS_USE_DATALINK_NAME)) != 0) {
		printf("failed to open client handle with %d\n", r);
		mac_close(mh);
		return (false);
	}

	/*
	 * Discover the MAC address of the NIC we have selected and print it to
	 * the console:
	 */
	mac_unicast_primary_get(mh, oe.oe_macaddr);
	printf("MAC address is %02X:%02X:%02X:%02X:%02X:%02X\n",
	    oe.oe_macaddr[0],
	    oe.oe_macaddr[1],
	    oe.oe_macaddr[2],
	    oe.oe_macaddr[3],
	    oe.oe_macaddr[4],
	    oe.oe_macaddr[5]);

	mac_unicast_handle_t muh;
	mac_diag_t diag;
	if (mac_unicast_add(mch, NULL, MAC_UNICAST_PRIMARY, &muh, 0, &diag) !=
	    0) {
		printf("mac unicast add failure (diag %d)\n", diag);
		mac_client_close(mch, 0);
		mac_close(mh);
		return (false);
	}

	/*
	 * Start sending boot server discovery broadcasts, and listening for
	 * frames in response.
	 */
	mac_rx_set(mch, oxide_boot_net_ether_rx, &oe);
	mutex_enter(&oe.oe_mutex);
	printf("listening for packets...\n");
	for (;;) {
		if (oxide_boot_net_ether_turn(oxb, &oe, mch) == 1) {
			printf("all done!\n");
			break;
		}

		(void) cv_reltimedwait(&oe.oe_cv, &oe.oe_mutex,
		    drv_usectohz(50 * 1000), TR_MICROSEC);
	}
	mutex_exit(&oe.oe_mutex);

	printf("closing unicast handle\n");
	(void) mac_unicast_remove(mch, muh);
	printf("closing client handle\n");
	mac_rx_clear(mch);

	printf("freeing remaining messages\n");
	freemsgchain(oe.oe_q);

	mac_client_close(mch, 0);
	printf("closing handle\n");
	mac_close(mh);

	mutex_destroy(&oe.oe_mutex);
	cv_destroy(&oe.oe_cv);

	return (true);
}
