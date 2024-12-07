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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * Sometimes you just need to know the contents of an SMN register, but since we
 * don't have watchpoints in the PCIe phy, we just are going to sample it a lot.
 * Think of this as a poor excuse of a logic analyzer.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/io/zen/smn.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/debug.h>
#include <sys/amdzen/smn.h>

/*
 * These are currently all PCIEPORT:: registers that are targetting the T6's
 * root port.
 */
static const uint32_t smn_regs[] = {
	/* LC_STATE_0 */
	0x11240294,
	/* PCIE_LC_SPEED_CNTL */
	0x11240290,
	/* PCIE_LC_LINK_WIDTH_CNTL */
	0x11240288,
	/* PCIE_LC_CNTL2 */
	0x112402C4
};

typedef struct smn_snitch_rec {
	hrtime_t sr_hrtime;
	uint32_t sr_pad;
	uint32_t sr_ndata;
	uint32_t sr_data[];
} smn_snitch_rec_t;

uint8_t snitch_socno = 0;
uint8_t snitch_iodieno = 0;
uint64_t snitch_pause_us = 100;
uint32_t snitch_nrecs = 20000;
void *snitch_data;
zen_iodie_t *snitch_iodie;

volatile boolean_t snitch_done = B_FALSE;

/*
 * The snitch can see through the lack of headers.
 */
extern zen_fabric_t zen_fabric;

static void
smn_snitch_fill(smn_snitch_rec_t *rec)
{
	rec->sr_hrtime = gethrtime();
	rec->sr_ndata = ARRAY_SIZE(smn_regs);
	for (uint32_t i = 0; i < rec->sr_ndata; i++) {
		const smn_reg_t reg = SMN_MAKE_REG_SIZED(smn_regs[i], 4);
		rec->sr_data[i] = zen_iodie_read(snitch_iodie, reg);
	}
}

void
smn_snitch_thread(void)
{
	for (uint32_t i = 0; i < snitch_nrecs; i++) {
		const size_t datalen = ARRAY_SIZE(smn_regs) * sizeof (uint32_t);
		const size_t reclen = sizeof (smn_snitch_rec_t) + datalen;
		const size_t off = i * reclen;
		smn_snitch_rec_t *rec = (void *)((uintptr_t)snitch_data + off);

		smn_snitch_fill(rec);
		drv_usecwait(snitch_pause_us);
	}

	snitch_done = B_TRUE;
}

void
smn_snitch_init(void)
{
	zen_fabric_t *fabric = &zen_fabric;
	size_t datalen = ARRAY_SIZE(smn_regs) * sizeof (uint32_t);
	size_t reclen = sizeof (smn_snitch_rec_t) + datalen;
	size_t alloclen = reclen * snitch_nrecs;

	snitch_data = kmem_zalloc(alloclen, KM_SLEEP);

	/*
	 * Assume the I/O die we care about is always the first one.
	 */
	snitch_iodie = &fabric->zf_socs[snitch_socno].zs_iodies[snitch_iodieno];
	VERIFY3P(snitch_iodie, !=, NULL);
}

#if 0
void
smn_snitch_start(void)
{
	zen_fabric_t *fabric = &zen_fabric;

	thread_create(NULL, 0, smn_snitch_thread, NULL, 0, &p0, TS_RUN, maxclsyspri);
}
#endif
