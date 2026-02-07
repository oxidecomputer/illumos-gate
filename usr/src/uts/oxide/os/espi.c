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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * Polling driver to support communication with an eSPI target implemented in
 * an FPGA on an Oxide board. This is used for communicating with the service
 * processor from the kernel, including early in boot when UNIX first starts
 * up. Messages are sent back and forth to the SP via the standard eSPI
 * Out-of-band (tunneled SMBus) message channel.
 */

#include <sys/clock.h>
#include <sys/stdbool.h>
#include <sys/types.h>
#include <sys/archsystm.h>
#include <sys/cpu.h>
#include <sys/prom_debug.h>
#include <sys/boot_data.h>
#include <sys/boot_debug.h>
#include <sys/bootconf.h>
#include <sys/sdt.h>
#include <sys/sysmacros.h>
#include <sys/ipcc_proto.h>
#include <sys/platform_detect.h>
#include <sys/espi_impl.h>
#include <sys/io/fch/espi.h>
#include <vm/kboot_mmu.h>

/*
 * Note that this code is executed very early in unix before a lot of niceties
 * are available. Avoid using ASSERT/VERIFY, DTRACE_PROBExx, cmn_err and things
 * from genunix such as mutexes without checking that things are far enough
 * along via the global `standalone` variable being 0.
 */
extern int standalone;

/*
 * These data are populated during initialisation and cached for subsequent
 * inspection. Only the finally selected OOB payload size (ed_targ_sel_oob_cap)
 * is used thereafter.
 */
typedef struct espi_data {
	/*
	 * The value of the target's general capabilities/config register
	 */
	uint32_t	ed_reg_gencap;
	/*
	 * The value of the target's OOB channel capabilities/config register
	 */
	uint32_t	ed_reg_oobcap;

	/*
	 * The host's maximum out-of-band channel payload, in bytes.
	 */
	size_t		ed_host_max_oob_cap;
	/*
	 * The target's maximum out-of-band channel payload, in bytes.
	 */
	size_t		ed_targ_max_oob_cap;
	/*
	 * The target's currently selected out-of-band channel payload size,
	 * in bytes.
	 */
	size_t		ed_targ_sel_oob_cap;
} espi_data_t;

static espi_data_t espi_data;

/*
 * Bounce buffer for OOB receive data. Hardware delivers data in packets up to
 * the negotiated OOB payload size (at most 256 bytes), but callers may want to
 * read less than a full packet at a time. We read complete packets from the
 * hardware FIFO into this bounce buffer and serve bytes to callers from it,
 * refilling as necessary.
 */
#define	ESPI_OOB_BOUNCE_BUFSZ	256

typedef struct espi_oob_bounce {
	uint8_t		eob_buf[ESPI_OOB_BOUNCE_BUFSZ];
	size_t		eob_pos;	/* current read position */
	size_t		eob_len;	/* amount of valid data */
} espi_oob_bounce_t;

static espi_oob_bounce_t espi_oob_bounce;

/*
 * We place some fairly arbitrary bounds on the length of register polling. We
 * do not expect these values to be exceeded in operation. In general we expect
 * operations to be quick and so we spin briefly before falling back to
 * sleeping.
 */
uint_t espi_delay_ms = 1;
uint_t espi_spins = 20;
uint_t espi_retries = 100;

/*
 * Convert a payload size value in an eSPI register to the corresponding number
 * of bytes, returning 0 for an unknown value.
 */
static size_t
espi_payload_size(uint8_t val)
{
	switch (val) {
	case ESPI_REG_CHAN2_CAP_PAYLOAD_64:
		return (64);
	case ESPI_REG_CHAN2_CAP_PAYLOAD_128:
		return (128);
	case ESPI_REG_CHAN2_CAP_PAYLOAD_256:
		return (256);
	}
	bop_panic("%s: unhandled value 0x%x", __func__, val);
}

/*
 * Convert a payload bytes value into the value used for the eSPI register.
 */
static uint8_t
espi_payload_key(size_t val)
{
	switch (val) {
	case 64:
		return (ESPI_REG_CHAN2_CAP_PAYLOAD_64);
	case 128:
		return (ESPI_REG_CHAN2_CAP_PAYLOAD_128);
	case 256:
		return (ESPI_REG_CHAN2_CAP_PAYLOAD_256);
	}
	bop_panic("%s: unhandled value 0x%zx", __func__, val);
}

/*
 * Determine the limits on the payload of an OOB message from both the host and
 * the target's perspective, and configure the target to use the maximum
 * supported size.
 */
static int
espi_determine_payload(mmio_reg_block_t block)
{
	mmio_reg_t reg;
	uint32_t val;

	reg = FCH_ESPI_MASTER_CAP_MMIO(block);
	val = mmio_reg_read(reg);
	/*
	 * FCH::ITF::ESPI::MASTER_CAP[OOB_MAX_SIZE] appears to be defined in
	 * the same way as payload sizes in the eSPI specification. This is
	 * fixed to 03h in Turin, meaning 256 bytes, but we read it anyway.
	 */
	espi_data.ed_host_max_oob_cap = espi_payload_size(
	    FCH_ESPI_MASTER_CAP_GET_OOB_MAXSZ(val));

	val = espi_get_configuration(block, ESPI_REG_CHAN2_CAP);
	if (val == ESPI_CFG_INVAL32) {
		EB_DBGMSG("eSPI: cannot retrieve OOB channel config reg\n");
		return (ENXIO);
	}

	/*
	 * Retrieve the target's currently selected, and maximum allowed,
	 * payload size for OOB packets.
	 */
	espi_data.ed_targ_sel_oob_cap = espi_payload_size(
	    ESPI_REG_CHAN2_CAP_GET_SELPAYLOAD(val));
	espi_data.ed_targ_max_oob_cap = espi_payload_size(
	    ESPI_REG_CHAN2_CAP_GET_MAXPAYLOAD(val));

	/*
	 * If we can, upgrade the payload size.
	 */
	const size_t maxpayload = MIN(espi_data.ed_host_max_oob_cap,
	    espi_data.ed_targ_max_oob_cap);

	if (maxpayload > espi_data.ed_targ_sel_oob_cap) {
		uint8_t newpayload = espi_payload_key(maxpayload);
		int ret;

		val = ESPI_REG_CHAN2_CAP_SET_SELPAYLOAD(val, newpayload);

		ret = espi_set_configuration(block, ESPI_REG_CHAN2_CAP, val);
		if (ret != 0) {
			bop_panic("eSPI: failed to program OOB payload size, "
			    "got error 0x%x\n", ret);
		}

		/*
		 * Re-read the new selected payload back from the
		 * target.
		 */
		val = espi_get_configuration(block, ESPI_REG_CHAN2_CAP);
		espi_data.ed_targ_sel_oob_cap = espi_payload_size(
		    ESPI_REG_CHAN2_CAP_GET_SELPAYLOAD(val));

		if (espi_data.ed_targ_sel_oob_cap != maxpayload) {
			bop_panic("eSPI: failed to upgrade OOB payload size. "
			    "Set 0x%zx, got 0x%zx\n",
			    maxpayload, espi_data.ed_targ_sel_oob_cap);
		}
	}

	/*
	 * Ensure that the controller is configured to respect the maximum OOB
	 * size according to the eSPI specification. This is the default value,
	 * but let's be sure.
	 */
	reg = FCH_ESPI_MISC_CTL0_MMIO(block);
	val = mmio_reg_read(reg);
	val = FCH_ESPI_MISC_CTL0_SET_OOB_LEN_LIM_EN(val, 1);
	mmio_reg_write(reg, val);

	return (0);
}

/*
 * espi_init() is called from early in UNIX _start(), via the IPCC
 * initialisation routine. We're single-threaded here and can safely populate
 * the global espi_data.
 *
 * We only end up here if we discover that the system was booted via eSPI and
 * we (mostly) assume that the eSPI initialization sequences that the PPR
 * describes the PSP and ABL as doing in that case have been done. That means
 * that the eSPI controller is mostly ready to use. Link speed and width
 * negotiation will have completed, and protocol parameters such as CRC
 * checking will have been decided and configured. For some of these, we
 * re-check and assert that they are enabled.
 */
int
espi_init(mmio_reg_block_t block)
{
	const char *freq, *mode;
	mmio_reg_t reg;
	uint32_t val, hostcap;
	int ret;

	reg = FCH_ESPI_MASTER_CAP_MMIO(block);
	hostcap = mmio_reg_read(reg);
	if (FCH_ESPI_MASTER_CAP_GET_VER(hostcap) !=
	    FCH_ESPI_MASTER_CAP_VER_1_0) {
		EB_DBGMSG("eSPI: host does not support eSPI v1.x "
		    "(cap is 0x%x)\n", FCH_ESPI_MASTER_CAP_GET_VER(hostcap));
		return (ENOTSUP);
	}
	if (FCH_ESPI_MASTER_CAP_GET_OOB(hostcap) == 0) {
		EB_DBGMSG("eSPI: host does not support the OOB channel\n");
		return (ENOTSUP);
	}

	reg = FCH_ESPI_RESERVED_REG0_MMIO(block);
	val = mmio_reg_read(reg);
	if (FCH_ESPI_RESERVED_REG0_INIT_STAT(val) !=
	    FCH_ESPI_RESERVED_REG0_INIT_STAT_SUCCESS) {
		EB_DBGMSG("eSPI: hardware NOT successfully initialised "
		    "- status is 0x%x\n",
		    FCH_ESPI_RESERVED_REG0_INIT_STAT(val));
		return (ENXIO);
	}

	ret = espi_acquire(block);

	if (ret != 0) {
		EB_DBGMSG("eSPI: could not acquire semaphore\n");
		return (ret);
	}

	ret = ENXIO;

	val = espi_get_configuration(block, ESPI_REG_IDENT);
	if (ESPI_REG_IDENT_GET_VERSION(val) != ESPI_REG_IDENT_VERSION_1X) {
		EB_DBGMSG("eSPI: Unsupported version %u\n",
		    ESPI_REG_IDENT_GET_VERSION(val));
		goto out;
	}

	val = espi_get_configuration(block, ESPI_REG_GEN_CAP);
	if (val == ESPI_CFG_INVAL32) {
		EB_DBGMSG("eSPI: failed to read general capability register\n");
		goto out;
	}

	espi_data.ed_reg_gencap = val;
	if (ESPI_REG_GEN_CAP_GET_OOB(val) == 0) {
		EB_DBGMSG("eSPI: OOB channel is not supported\n");
		goto out;
	}

	switch (ESPI_REG_GEN_CAP_GET_IOMODE(val)) {
	case ESPI_REG_GEN_CAP_IOMODE_SINGLE:
		mode = "x1";
		break;
	case ESPI_REG_GEN_CAP_IOMODE_DUAL:
		mode = "x2";
		break;
	case ESPI_REG_GEN_CAP_IOMODE_QUAD:
		mode = "x4";
		break;
	default:
		mode = "??";
	}

	switch (ESPI_REG_GEN_CAP_GET_FREQ(val)) {
	case ESPI_REG_GEN_CAP_FREQ_20MHZ:
		freq = "20MHz";
		break;
	case ESPI_REG_GEN_CAP_FREQ_25MHZ:
		freq = "25MHz";
		break;
	case ESPI_REG_GEN_CAP_FREQ_35MHZ:
		freq = "35MHz";
		break;
	case ESPI_REG_GEN_CAP_FREQ_50MHZ:
		freq = "50MHz";
		break;
	case ESPI_REG_GEN_CAP_FREQ_66MHZ:
		freq = "66MHz";
		break;
	default:
		freq = "?MHz";
	}

	EB_DBGMSG("eSPI: successfully initialised -- %s %s\n", freq, mode);

	/*
	 * Enable CRC checking if it is supported and not already on. This
	 * should have been done by the PSP/ABL as part of eSPI boot, but let's
	 * make sure.
	 */
	if (FCH_ESPI_MASTER_CAP_GET_CRC(hostcap) == 1 &&
	    ESPI_REG_GEN_CAP_GET_CRC_EN(val) == 0) {
		EB_DBGMSG(
		    "eSPI: CRC checking is supported but disabled, enabling\n");
		val = ESPI_REG_GEN_CAP_SET_CRC_EN(val, 1);
		ret = espi_set_configuration(block, ESPI_REG_GEN_CAP, val);
		if (ret != 0) {
			bop_panic("eSPI: failed to enable CRC checking, "
			    "got error 0x%x\n", ret);
		}
		val = espi_data.ed_reg_gencap =
		    espi_get_configuration(block, ESPI_REG_GEN_CAP);
		if (ESPI_REG_GEN_CAP_GET_CRC_EN(val) == 0)
			bop_panic("eSPI: target did not accept CRC enable\n");
	}

	val = espi_get_configuration(block, ESPI_REG_CHAN2_CAP);
	espi_data.ed_reg_oobcap = val;
	if (ESPI_REG_CHAN2_CAP_GET_EN(val) == 0) {
		EB_DBGMSG("eSPI: OOB channel not enabled\n");
		goto out;
	}
	if (ESPI_REG_CHAN2_CAP_GET_READY(val) == 0) {
		EB_DBGMSG("eSPI: OOB channel not ready\n");
		goto out;
	}

	ret = espi_determine_payload(block);
	if (ret != 0)
		goto out;

	/*
	 * Clear any leftover bits in the interrupt status register so that we
	 * start in a clean state.
	 */
	reg = FCH_ESPI_S0_INT_STS_MMIO(block);
	mmio_reg_write(reg, mmio_reg_read(reg));

	ret = 0;

out:
	espi_release(block);
	return (ret);
}

static int
espi_handle_interrupt(uint32_t r)
{
	const struct {
		bool set;
		int errnum;
	} data[] = {
		/*
		 * This table covers all of the interrupts we can receive --
		 * i.e. the bits defined in FCH::ITF::ESPI::SLAVE0_INT_STS.
		 * They are listed in descending order of priority. Once we
		 * find a matching bit which has a non-zero errno against it we
		 * return that errno.
		 */

		{ FCH_ESPI_S0_INT_STS_GET_WDG_TO(r),		ETIMEDOUT },
		{ FCH_ESPI_S0_INT_STS_GET_MST_ABORT(r),		ECONNABORTED },
		{ FCH_ESPI_S0_INT_STS_GET_UPFIFO_WDG_TO(r),	ETIMEDOUT },

		/*
		 * These are all indicative of protocol errors. Either we have
		 * sent invalid data or the target has.
		 */
		{ FCH_ESPI_S0_INT_STS_GET_PROTOERR(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_ILL_LEN(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_ILL_TAG(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_USF_CPL(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_UNK_CYC(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_UNK_RSP(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_CRC_ERR(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_WAIT_TMT(r),		EPROTO },
		{ FCH_ESPI_S0_INT_STS_GET_BUS_ERR(r),		EPROTO },

		/*
		 * These are also indicative of protocol errors. The target has
		 * sent a frame which is too large in one regard or another.
		 */
		{ FCH_ESPI_S0_INT_STS_GET_RXFLASH_OFLOW(r),	EOVERFLOW },
		{ FCH_ESPI_S0_INT_STS_GET_RXMSG_OFLOW(r),	EOVERFLOW },
		{ FCH_ESPI_S0_INT_STS_GET_RXOOB_OFLOW(r),	EOVERFLOW },

		/*
		 * No response was forthcoming when one was expected.
		 */
		{ FCH_ESPI_S0_INT_STS_GET_NO_RSP(r),		ETIMEDOUT },

		/*
		 * The target has sent a fatal error message.
		 */
		{ FCH_ESPI_S0_INT_STS_GET_FATAL_ERR(r),		EPROTO },

		/*
		 * The target has sent a non-fatal error message. This does not
		 * affect its ability to process the received command(!)
		 */
		{ FCH_ESPI_S0_INT_STS_GET_NFATAL_ERR(r),	0 },

		/*
		 * These are completion alerts and not mapped to errors.
		 */
		{ FCH_ESPI_S0_INT_STS_GET_RXVW_G3(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_RXVW_G2(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_RXVW_G1(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_RXVW_G0(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_FLASHREQ(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_RXOOB(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_RXMSG(r),		0 },
		{ FCH_ESPI_S0_INT_STS_GET_DNCMD(r),		0 },
	};

	for (uint_t i = 0; i < ARRAY_SIZE(data); i++) {
		if (data[i].set && data[i].errnum != 0)
			return (data[i].errnum);
	}

	return (0);
}

/*
 * Clear out bits from the interrupt status register so we can better determine
 * if the command we're about to send is responsible for setting any of them.
 * The packet submission code will call espi_handle_interrupt() - above -
 * to check and convert set bits to error codes.
 *
 * All general error bits and those related to OOB transactions are cleared;
 * completion alert bits and errors for other packet types are left intact.
 * It's particularly important to retain the RXOOB bit in case an OOB
 * completion has arrived while the channel is idle from the host perspective.
 * In this case we still need to consume any data in the FIFO and signal to the
 * hardware that we're ready to receive the next packet.
 */
static void
espi_clear_interrupt(mmio_reg_block_t block)
{
	mmio_reg_t intsts = FCH_ESPI_S0_INT_STS_MMIO(block);
	uint32_t val = 0;

	val = FCH_ESPI_S0_INT_STS_CLEAR_WDG_TO(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_MST_ABORT(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_UPFIFO_WDG_TO(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_PROTOERR(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_RXOOB_OFLOW(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_ILL_LEN(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_ILL_TAG(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_USF_CPL(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_UNK_CYC(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_UNK_RSP(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_NFATAL_ERR(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_FATAL_ERR(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_NO_RSP(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_CRC_ERR(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_WAIT_TMT(val);
	val = FCH_ESPI_S0_INT_STS_CLEAR_BUS_ERR(val);
	mmio_reg_write(intsts, val);
}

/*
 * Acquire ownership of the eSPI semaphore.
 */
int
espi_acquire(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_SEM_MISC_CTL_REG0_MMIO(block);
	uint32_t val;
	int ret = ETIMEDOUT;

	val = mmio_reg_read(reg);

	if (standalone == 0) {
		VERIFY0(FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(val));
	}

	for (uint_t i = 0; i < espi_retries; i++) {
		/*
		 * Poll for idle. This comes from the PPR and is the set of
		 * fields we need to ensure are zero before we attempt to
		 * acquire the semaphore.
		 */
		if (FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW4_USER_ID(val) != 0 ||
		    FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW0_OWN_STAT(val) != 0 ||
		    FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW1_OWN_STAT(val) != 0 ||
		    FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(val) != 0 ||
		    FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW3_OWN_STAT(val) != 0) {
			if (standalone == 0) {
				DTRACE_PROBE2(espi__acquire__locked,
				    uint32_t, val, uint_t, i);
			}
			eb_pausems(espi_delay_ms);
			continue;
		}

		/*
		 * Attempt to acquire the semaphore as owner 2
		 * (reserved for x86).
		 */
		val = FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_SET(val, 1);
		val = FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_CLR(val, 0);

		mmio_reg_write(reg, val);
		val = mmio_reg_read(reg);

		/* Confirm semaphore acquisition */
		if (FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(val) == 1) {
			/* Success */
			ret = 0;
			espi_clear_interrupt(block);
			break;
		}

		if (standalone == 0) {
			DTRACE_PROBE2(espi__acquire__failed, uint32_t, val,
			    uint_t, i);
		}

		eb_pausems(espi_delay_ms);
	}

	return (ret);
}

/*
 * Release the eSPI bus semaphore.
 */
void
espi_release(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_SEM_MISC_CTL_REG0_MMIO(block);

	uint32_t val = mmio_reg_read(reg);

	if (standalone == 0) {
		VERIFY(FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(val));
	}

	/* Release semaphore */
	val = FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_CLR(val, 1);
	mmio_reg_write(reg, val);
	val = mmio_reg_read(reg);

	/* Wait for ownership status to change */
	while (FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(val) != 0) {
		if (standalone == 0) {
			DTRACE_PROBE1(espi__release__wait, uint32_t, val);
		}
		eb_pausems(espi_delay_ms);
		val = mmio_reg_read(reg);
	}

	/* Complete release operation */
	val = FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_CLR(val, 0);
	val = FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_SET(val, 0);
	mmio_reg_write(reg, val);
}

uint32_t
espi_intstatus(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_S0_INT_STS_MMIO(block);

	return (mmio_reg_read(reg));
}

/*
 * Wait until the eSPI bus is idle, as indicated by
 * FCH::ITF::ESPI::DN_TXHDR_0th[DNCMD_STATUS] being clear.
 */
static int
espi_wait_idle(mmio_reg_block_t block)
{
	mmio_reg_t hdr0 = FCH_ESPI_DN_TXHDR0_MMIO(block);
	uint32_t val;
	int ret = ETIMEDOUT;

	/* Wait until the hardware is ready */
	for (uint_t i = 0; i < espi_retries; i++) {
		val = mmio_reg_read(hdr0);
		if (FCH_ESPI_DN_TXHDR0_GET_DNCMD_STATUS(val) == 0) {
			ret = 0;
			break;
		}
		if (standalone == 0) {
			DTRACE_PROBE2(espi__wait__idle, uint32_t, val,
			    uint_t, i);
		}
		if (i > espi_spins)
			eb_pausems(espi_delay_ms);
	}

	return (ret);
}

/*
 * This routine takes care of sending a prepared message downstream. The
 * header registers and FIFO have already been programmed appropriately before
 * it is called.
 */
static int
espi_submit(mmio_reg_block_t block)
{
	mmio_reg_t hdr0_type = FCH_ESPI_DN_TXHDR0_TYPE_MMIO(block);
	mmio_reg_t intsts = FCH_ESPI_S0_INT_STS_MMIO(block);
	uint32_t val;

	/*
	 * Clear the DNCMD interrupt bit before sending the command down as we
	 * need to watch for this to become set again to confirm dispatch.
	 */
	mmio_reg_write(intsts, FCH_ESPI_S0_INT_STS_CLEAR_DNCMD(0));

	/* Mark ready to send */
	val = mmio_reg_read(hdr0_type);
	val = FCH_ESPI_DN_TXHDR0_SET_DNCMD_STATUS(val, 1);
	mmio_reg_write(hdr0_type, val);

	/* Poll for command completion and other interrupts */
	for (uint_t i = 0; i < espi_retries; i++) {
		val = mmio_reg_read(intsts);
		if (FCH_ESPI_S0_INT_STS_GET_DNCMD(val) == 1)
			break;
		if (standalone == 0) {
			DTRACE_PROBE2(espi__submit__waitintr, uint32_t, val,
			    uint_t, i);
		}
		if (val != 0) {
			int ret = espi_handle_interrupt(val);
			if (ret != 0)
				return (ret);
		}
		if (i > espi_spins)
			eb_pausems(espi_delay_ms);
	}
	if (FCH_ESPI_S0_INT_STS_GET_DNCMD(val) != 1)
		return (ETIMEDOUT);

	/* Poll for completion */
	for (uint_t i = 0; i < espi_retries; i++) {
		val = mmio_reg_read(hdr0_type);
		if (FCH_ESPI_DN_TXHDR0_GET_DNCMD_STATUS(val) == 0)
			break;
		if (standalone == 0) {
			DTRACE_PROBE2(espi__submit__wait, uint32_t, val,
			    uint_t, i);
		}
		if (i > espi_spins)
			eb_pausems(espi_delay_ms);
	}
	if (FCH_ESPI_DN_TXHDR0_GET_DNCMD_STATUS(val) != 0)
		return (ETIMEDOUT);

	return (0);
}

static uint32_t
espi_config_reg(int cmd, uint16_t reg)
{
	uint32_t val = 0;

	/*
	 * Set the command type - SET/GET_CONFIGURATION
	 */
	val = FCH_ESPI_DN_TXHDR0_SET_DNCMD_TYPE(val, cmd);

	/*
	 * Set the requested address (register):
	 *	HDATA0[7:4] = 0
	 *	HDATA0[3:0] = Address[11:8]
	 *	HDATA1[7:0] = Address[7:0]
	 */
	val = FCH_ESPI_DN_TXHDR0_SET_HDATA0(val, bitx16(reg, 11, 8));
	val = FCH_ESPI_DN_TXHDR0_SET_HDATA1(val, bitx16(reg, 7, 0));

	/*
	 * HDATA2 is reserved (must be set to 0) for a set/get configuration.
	 */
	val = FCH_ESPI_DN_TXHDR0_SET_HDATA2(val, 0);

	return (val);
}

uint32_t
espi_get_configuration(mmio_reg_block_t block, uint16_t reg)
{
	mmio_reg_t hdr0 = FCH_ESPI_DN_TXHDR0_MMIO(block);
	mmio_reg_t hdr1 = FCH_ESPI_DN_TXHDR1_MMIO(block);
	uint32_t val0, val;

	/*
	 * The eSPI specification requires that the lower two and upper four
	 * bits of the register are 0.
	 */
	if ((reg & 0x3) != 0 || (reg >> 12) != 0)
		return (ESPI_CFG_INVAL32);

	if (espi_wait_idle(block) != 0)
		return (ESPI_CFG_INVAL32);

	val0 = espi_config_reg(FCH_ESPI_DN_TXHDR0_TYPE_GETCONF, reg);
	mmio_reg_write(hdr0, val0);

	/*
	 * The PPR recommends to set this to 0 to clear any residual value.
	 */
	mmio_reg_write(hdr1, 0);

	if (espi_submit(block) != 0)
		return (ESPI_CFG_INVAL32);

	val = mmio_reg_read(hdr1);

	if (standalone == 0) {
		DTRACE_PROBE2(espi__get__cfg, uint16_t, reg, uint32_t, val);
	}

	return (val);
}

int
espi_set_configuration(mmio_reg_block_t block, uint16_t reg, uint32_t val)
{
	mmio_reg_t hdr0 = FCH_ESPI_DN_TXHDR0_MMIO(block);
	mmio_reg_t hdr1 = FCH_ESPI_DN_TXHDR1_MMIO(block);
	uint32_t val0;
	int ret;

	/*
	 * The eSPI specification requires that the lower two and upper four
	 * bits of the register are 0.
	 */
	if ((reg & 0x3) != 0 || (reg >> 12) != 0)
		return (EINVAL);

	if ((ret = espi_wait_idle(block)) != 0)
		return (ret);

	val0 = espi_config_reg(FCH_ESPI_DN_TXHDR0_TYPE_SETCONF, reg);
	mmio_reg_write(hdr0, val0);

	/* Write the requested value */
	mmio_reg_write(hdr1, val);

	ret = espi_submit(block);

	if (standalone == 0) {
		DTRACE_PROBE3(espi__set__cfg, uint16_t, reg, uint32_t, val0,
		    uint32_t, val);
	}

	return (ret);
}

bool
espi_oob_readable(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_S0_INT_STS_MMIO(block);
	uint32_t val;

	if (espi_oob_bounce.eob_pos < espi_oob_bounce.eob_len)
		return (true);

	val = mmio_reg_read(reg);
	return (FCH_ESPI_S0_INT_STS_GET_RXOOB(val) == 1);
}

bool
espi_oob_writable(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_DN_TXHDR0_MMIO(block);
	uint32_t val;

	val = mmio_reg_read(reg);
	return (FCH_ESPI_DN_TXHDR0_GET_DNCMD_STATUS(val) == 0);
}

void
espi_oob_flush(mmio_reg_block_t block)
{
	mmio_reg_t hdr0 = FCH_ESPI_UP_RXHDR0_MMIO(block);

	/*
	 * Proactively advertise that the RX FIFO is ready to accept a new
	 * upstream OOB request, regardless of whether we have any
	 * indication from the eSPI registers that data is pending.
	 *
	 * We have observed that if data becomes available before the OS is
	 * running, the eSPI controller will already be asserting an OOB
	 * alert, no interrupt will be delivered to the driver and the
	 * controller will not generate another alert until the existing
	 * data is consumed.
	 */
	mmio_reg_write(hdr0, FCH_ESPI_UP_RXHDR0_CLEAR_UPCMD_STAT(0));

	/* Drain the input buffer */
	while (espi_oob_readable(block))
		(void) espi_oob_rx(block, NULL, NULL);
}

/*
 * Wait until the eSPI target advertises that its OOB channel is free, meaning
 * that it is able to accept at least one OOB packet with data up to the
 * configured payload size.
 */
static int
espi_wait_oob_free(mmio_reg_block_t block)
{
	mmio_reg_t reg = FCH_ESPI_MISC_CTL0_MMIO(block);
	uint32_t val;
	int ret = EBUSY;

	/* Wait until the hardware is ready */
	for (uint_t i = 0; i < espi_retries; i++) {
		val = mmio_reg_read(reg);
		if (FCH_ESPI_MISC_CTL0_GET_OOB_FREE(val) == 1) {
			ret = 0;
			break;
		}
		if (standalone == 0) {
			DTRACE_PROBE2(espi__wait__oob__free, uint32_t, val,
			    uint_t, i);
		}
		if (i > espi_spins)
			eb_pausems(espi_delay_ms);
	}

	return (ret);
}

int
espi_oob_tx(mmio_reg_block_t block, uint8_t *buf, size_t *lenp)
{
	static uint8_t tag = 0;
	uint32_t val0, val1;
	size_t len = *lenp;
	size_t written = 0;
	int ret = 0;

	/*
	 * These four 8-bit registers are all part of the same 32-bit
	 * FCH::ITF::ESPI::DN_TXHDR_0th but AMD sources state that at least the
	 * first two should be written as "byte write" operations.
	 * Experimentally this does not seem to actually matter.
	 */
	mmio_reg_t hdr0_type = FCH_ESPI_DN_TXHDR0_TYPE_MMIO(block);
	mmio_reg_t hdr0_hdata0 = FCH_ESPI_DN_TXHDR0_HDATA0_MMIO(block);
	mmio_reg_t hdr0_hdata1 = FCH_ESPI_DN_TXHDR0_HDATA1_MMIO(block);
	mmio_reg_t hdr0_hdata2 = FCH_ESPI_DN_TXHDR0_HDATA2_MMIO(block);

	mmio_reg_t hdr1 = FCH_ESPI_DN_TXHDR1_MMIO(block);

	/*
	 * We have to accommodate the SMBus header in the allowed payload size.
	 * That header consists of (target, opcode, count and optional PEC
	 * byte).
	 */
	const size_t maxpayload = espi_data.ed_targ_sel_oob_cap - 4;

	while (len > 0) {
		const size_t sendlen = MIN(len, maxpayload);

		if ((ret = espi_wait_idle(block)) != 0)
			break;
		if ((ret = espi_wait_oob_free(block)) != 0)
			break;

		val0 = FCH_ESPI_DN_TXHDR0_SET_DNCMD_TYPE(0,
		    FCH_ESPI_DN_TXHDR0_TYPE_OOB);

		/* Set the cycle type */
		val0 = FCH_ESPI_DN_TXHDR0_SET_HDATA0(val0,
		    ESPI_CYCLE_OOB_TUNNELED_SMBUS);

		/*
		 * We use an incrementing tag for each message to aid matching
		 * up with bus traces. The actual tag value is not used by the
		 * target.
		 */
		val0 = FCH_ESPI_DN_TXHDR0_SET_TAG(val0, (tag++ & 0xf));

		/*
		 * We don't add a PEC byte so our SMBus header increases the
		 * packet size by 3.
		 */
		const size_t pktlen = sendlen + 3;
		VERIFY3U(pktlen, <, UINT16_MAX);
		val0 = FCH_ESPI_DN_TXHDR0_SET_LENH(val0, bitx16(pktlen, 15, 8));
		val0 = FCH_ESPI_DN_TXHDR0_SET_LENL(val0, bitx16(pktlen, 7, 0));

		/*
		 * Now the byte-write operations on TXHDR0th
		 */
		mmio_reg_write(hdr0_type,
		    FCH_ESPI_DN_TXHDR0_GET_DNCMD_TYPE(val0));
		mmio_reg_write(hdr0_hdata0,
		    FCH_ESPI_DN_TXHDR0_GET_HDATA0(val0));
		mmio_reg_write(hdr0_hdata1,
		    FCH_ESPI_DN_TXHDR0_GET_HDATA1(val0));
		mmio_reg_write(hdr0_hdata2,
		    FCH_ESPI_DN_TXHDR0_GET_HDATA2(val0));

		/*
		 * Additional header data.
		 */
		val1 = 0;
		/* HDATA6 is reserved for OOB messages and must be 0 */
		val1 = FCH_ESPI_DN_TXHDR1_SET_HDATA6(val1, 0x0);
		val1 = FCH_ESPI_DN_TXHDR1_SET_HDATA5(val1, sendlen);
		val1 = FCH_ESPI_DN_TXHDR1_SET_HDATA4(val1, 0x1); /* Opcode */
		val1 = FCH_ESPI_DN_TXHDR1_SET_HDATA3(val1, 0x1); /* Address */

		mmio_reg_write(hdr1, val1);

		if (standalone == 0) {
			DTRACE_PROBE2(espi__tx, uint32_t, val0, uint32_t, val1);
		}

		/*
		 * Submit data to the FIFO. FIFO writes follow little-endian
		 * order, packing up to four bytes per write to the 32-bit data
		 * register.
		 */
		mmio_reg_t data = FCH_ESPI_DN_TXDATA_PORT_MMIO(block);
		size_t towrite = sendlen;

		while (towrite > 0) {
			uint32_t val = 0;
			size_t l = MIN(towrite, sizeof (val));

			bcopy(buf, &val, l);
			mmio_reg_write(data, val);
			buf += l;
			towrite -= l;
		}

		if ((ret = espi_submit(block)) != 0)
			break;

		len -= sendlen;
		written += sendlen;
	}

	*lenp = written;

	return (ret);
}

/*
 * Read a single OOB packet from the hardware FIFO into the bounce buffer.
 * The caller must have verified both that the buffer is empty and RXOOB
 * is set before calling this.
 */
static void
espi_oob_rx_one(mmio_reg_block_t block)
{
	mmio_reg_t intsts = FCH_ESPI_S0_INT_STS_MMIO(block);
	mmio_reg_t hdr0 = FCH_ESPI_UP_RXHDR0_MMIO(block);
	mmio_reg_t hdr1 = FCH_ESPI_UP_RXHDR1_MMIO(block);
	uint32_t val0, val1;
	size_t len;

	VERIFY3U(espi_oob_bounce.eob_pos, ==, espi_oob_bounce.eob_len);

	val0 = mmio_reg_read(hdr0);
	val1 = mmio_reg_read(hdr1);

	/*
	 * Retrieve the payload length rather than the length in hdr0.
	 * This length will reflect the data we want to read and not
	 * include the length of the header or PEC bytes.
	 */
	len = FCH_ESPI_UP_RXHDR1_GET_HDATA5(val1);

	if (standalone == 0) {
		DTRACE_PROBE3(espi__rx, size_t, len, uint32_t, val0,
		    uint32_t, val1);
	}

	/*
	 * Clear the RXOOB interrupt flag now that we are going to go
	 * and read the FIFO, and request any further data.
	 */
	mmio_reg_write(intsts, FCH_ESPI_S0_INT_STS_CLEAR_RXOOB(0));

	VERIFY3U(len, <=, ESPI_OOB_BOUNCE_BUFSZ);

	bzero(espi_oob_bounce.eob_buf, ESPI_OOB_BOUNCE_BUFSZ);
	espi_oob_bounce.eob_pos = 0;
	espi_oob_bounce.eob_len = len;

	mmio_reg_t data = FCH_ESPI_UP_RXDATA_PORT_MMIO(block);
	uint8_t *dst = espi_oob_bounce.eob_buf;

	while (len > 0) {
		uint32_t val;
		size_t l = MIN(len, sizeof (val));

		val = mmio_reg_read(data);
		bcopy(&val, dst, l);
		dst += l;
		len -= l;
	}

	/*
	 * Let the hardware know we've finished with the message in the
	 * FIFO and are ready to accept a new OOB message.
	 */
	mmio_reg_write(hdr0, FCH_ESPI_UP_RXHDR0_CLEAR_UPCMD_STAT(0));
}

int
espi_oob_rx(mmio_reg_block_t block, uint8_t *buf, size_t *buflen)
{
	mmio_reg_t intsts = FCH_ESPI_S0_INT_STS_MMIO(block);
	uint32_t val;

	/*
	 * When called with a NULL buf, discard all pending data including
	 * any in the bounce buffer.
	 */
	if (buf == NULL) {
		bzero(espi_oob_bounce.eob_buf, ESPI_OOB_BOUNCE_BUFSZ);
		espi_oob_bounce.eob_pos = 0;
		espi_oob_bounce.eob_len = 0;

		for (;;) {
			mmio_reg_t hdr0 = FCH_ESPI_UP_RXHDR0_MMIO(block);

			val = mmio_reg_read(intsts);

			if (FCH_ESPI_S0_INT_STS_GET_RXOOB(val) == 0)
				break;

			mmio_reg_write(intsts,
			    FCH_ESPI_S0_INT_STS_CLEAR_RXOOB(0));
			mmio_reg_write(hdr0,
			    FCH_ESPI_UP_RXHDR0_CLEAR_UPCMD_STAT(0));
		}

		return (0);
	}

	size_t accum = 0;
	size_t space = *buflen;

	while (space > 0) {
		/*
		 * Serve any data remaining in the bounce buffer from a
		 * previous read.
		 */
		if (espi_oob_bounce.eob_pos < espi_oob_bounce.eob_len) {
			size_t avail = espi_oob_bounce.eob_len -
			    espi_oob_bounce.eob_pos;
			size_t n = MIN(avail, space);

			bcopy(
			    &espi_oob_bounce.eob_buf[espi_oob_bounce.eob_pos],
			    buf, n);
			espi_oob_bounce.eob_pos += n;
			buf += n;
			space -= n;
			accum += n;
			continue;
		}

		/*
		 * The bounce buffer is empty. If there is more data
		 * available from the hardware, read one packet into the
		 * bounce buffer and loop to serve it.
		 */
		val = mmio_reg_read(intsts);
		if (FCH_ESPI_S0_INT_STS_GET_RXOOB(val) == 0)
			break;

		espi_oob_rx_one(block);
	}

	*buflen = accum;

	return (0);
}
