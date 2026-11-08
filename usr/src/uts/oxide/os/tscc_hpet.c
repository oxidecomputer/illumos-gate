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
 * Copyright 2020 Joyent, Inc.
 * Copyright 2026 Oxide Computer Company
 */

#include <sys/tsc.h>
#include <sys/prom_debug.h>
#include <sys/clock.h>
#include <sys/archsystm.h>
#include <sys/sysmacros.h>
#include <sys/io/fch/hpet.h>
#include <sys/io/fch/pmio.h>

/*
 * TSC calibration against the FCH's HPET. There is no ACPI on this
 * architecture so we find the HPET at its fixed address and, if nothing has
 * done so already, enable its decoding and its main counter ourselves. The
 * counter is left running afterwards. None of the HPET's timers or interrupts
 * are touched.
 *
 * The HPET runs at 14.318MHz on all supported FCHs and reports its period in
 * femtoseconds, so its rate is known to better than 0.01ppm. A single counter
 * read is an uncached access to the FCH that takes around a microsecond, and
 * the read can land anywhere within that window. We count TSC and HPET ticks
 * over a measurement interval, and rather than timing a single read at each
 * end of that interval, we take a block of samples at the start and another
 * at the end and use their average TSC and counter values. Each sample is a
 * counter read taken at the midpoint between a pair of TSC reads. Samples
 * whose bracket is more than four times the narrowest seen were disturbed,
 * for example by an SMI, and are discarded. Over the default interval of
 * 250ms the result repeats to within a few parts in 10^8.
 */
uint32_t tscc_hpet_interval_ms = 250;

#define	TSCC_HPET_INTERVAL_MIN_MS	10U
#define	TSCC_HPET_INTERVAL_MAX_MS	1000U

/*
 * Where in its bracket of around a microsecond a single read lands can vary
 * by about 0.3us. Averaging a block of 1024 samples reduces that by a factor
 * of 32, to about 10ns, which is 0.04ppm over the default interval. A block
 * takes about a millisecond to collect. The warm-up samples load the TLB and
 * caches and establish the narrowest bracket before the first block. The
 * narrowest bracket is normally found within the first few samples, so 64
 * leaves a wide margin.
 */
#define	TSCC_HPET_BLOCK			1024U
#define	TSCC_HPET_WARMUP		64U

typedef struct tscc_hpet {
	/*
	 * HPET main counter register.
	 */
	mmio_reg_t th_ctr;
	/*
	 * The TSC value at the start of the measurement. Sample timestamps
	 * are stored relative to it so that a block's sum of 1024 of them
	 * cannot overflow. The reference is subtracted the same number of
	 * times in both blocks, so it cancels out of the difference.
	 */
	uint64_t th_tsc_ref;
	/*
	 * The HPET count extended to 64 bits. This is the number of HPET
	 * ticks since the start of the measurement, accumulated from the
	 * differences between successive 32-bit counter reads.
	 */
	uint64_t th_ext;
	/*
	 * Narrowest TSC bracket seen.
	 */
	uint64_t th_min_width;
	/*
	 * Last raw counter value read.
	 */
	uint32_t th_last;
} tscc_hpet_t;

/*
 * Map the HPET and make sure that its main counter is running. On success
 * the caller must unmap the block.
 */
static boolean_t
tscc_hpet_enable(mmio_reg_block_t *blockp, uint32_t *periodp)
{
	mmio_reg_block_t block;
	mmio_reg_t reg;
	uint32_t val;

	block = fch_pmio_mmio_block();
	reg = FCH_PMIO_DECODEEN_MMIO(block);
	val = mmio_reg_read(reg);
	if (FCH_PMIO_DECODEEN_GET_HPETEN(val) == 0) {
		PRM_POINT("Enabling HPET decoding");
		val = FCH_PMIO_DECODEEN_SET_HPETEN(val, 1);
		mmio_reg_write(reg, val);
	}
	mmio_reg_block_unmap(&block);

	block = fch_hpet_mmio_block();

	val = mmio_reg_read(FCH_HPET_ID_MMIO(block));
	const uint32_t period = mmio_reg_read(FCH_HPET_CLKPERIOD_MMIO(block));
	PRM_DEBUG(val);
	PRM_DEBUG(period);
	if (val == UINT32_MAX || period == 0 ||
	    period > FCH_HPET_CLKPERIOD_MAX_FS) {
		PRM_POINT("HPET is absent or reports an invalid period");
		mmio_reg_block_unmap(&block);
		return (B_FALSE);
	}

	reg = FCH_HPET_HPETCONFIG_MMIO(block);
	val = mmio_reg_read(reg);
	if (FCH_HPET_HPETCONFIG_GET_TMREN(val) == 0) {
		PRM_POINT("Enabling HPET main counter");
		val = FCH_HPET_HPETCONFIG_SET_TMREN(val, 1);
		mmio_reg_write(reg, val);
	}

	*blockp = block;
	*periodp = period;
	return (B_TRUE);
}

/*
 * Take one sample. Returns B_FALSE if the sample was disturbed and should not
 * be used, otherwise stores the midpoint of the TSC bracket, relative to the
 * reference, in *tscp.
 */
static boolean_t
tscc_hpet_sample(tscc_hpet_t *th, uint64_t *tscp)
{
	const uint64_t before = tsc_read();
	const uint32_t cur = (uint32_t)mmio_reg_read(th->th_ctr);
	const uint64_t after = tsc_read();
	const uint64_t width = after - before;

	th->th_ext += (uint32_t)(cur - th->th_last);
	th->th_last = cur;

	if (width < th->th_min_width)
		th->th_min_width = width;
	if (width > 4 * th->th_min_width)
		return (B_FALSE);

	*tscp = before + width / 2 - th->th_tsc_ref;
	return (B_TRUE);
}

/*
 * Collect a block of undisturbed samples and return the sums of their TSC
 * and extended counter values. Fails if too many samples are disturbed.
 */
static boolean_t
tscc_hpet_block(tscc_hpet_t *th, uint64_t *tscsump, uint64_t *hpetsump)
{
	uint64_t tscsum = 0, hpetsum = 0;
	uint_t i, n = 0;

	for (i = 0; i < 4 * TSCC_HPET_BLOCK && n < TSCC_HPET_BLOCK; i++) {
		uint64_t tsc;

		if (!tscc_hpet_sample(th, &tsc))
			continue;
		tscsum += tsc;
		hpetsum += th->th_ext;
		n++;
	}

	*tscsump = tscsum;
	*hpetsump = hpetsum;
	return (n == TSCC_HPET_BLOCK);
}

static boolean_t
tsc_calibrate_hpet(uint64_t *freqp)
{
	mmio_reg_block_t block;
	uint32_t period;

	PRM_POINT("Attempting to use HPET for TSC calibration...");

	if (!tscc_hpet_enable(&block, &period))
		return (B_FALSE);

	const uint64_t interval_ms = MAX(MIN(tscc_hpet_interval_ms,
	    TSCC_HPET_INTERVAL_MAX_MS), TSCC_HPET_INTERVAL_MIN_MS);
	const uint64_t target = interval_ms * 1000000000000ULL / period;

	/*
	 * If the counter stops advancing we give up once the TSC has counted
	 * for as long as the interval would take at 10GHz, which is at least
	 * as long as the interval itself on any machine we will see.
	 */
	const uint64_t tsc_limit = interval_ms * 10000000ULL;

	tscc_hpet_t th = {
		.th_ctr = FCH_HPET_MAIN_COUNTER_L_MMIO(block),
		.th_min_width = UINT64_MAX
	};
	uint64_t tsc_start = 0, hpet_start = 0, tsc_end = 0, hpet_end = 0;
	uint64_t tsc;
	boolean_t ok = B_FALSE;

	const ulong_t flags = clear_int_flag();

	/*
	 * Warm up the caches and the TLB and find the narrowest TSC bracket
	 * before the measurement starts.
	 */
	th.th_tsc_ref = tsc_read();
	th.th_last = (uint32_t)mmio_reg_read(th.th_ctr);
	for (uint_t i = 0; i < TSCC_HPET_WARMUP; i++)
		(void) tscc_hpet_sample(&th, &tsc);

	th.th_ext = 0;
	th.th_tsc_ref = tsc_read();

	if (!tscc_hpet_block(&th, &tsc_start, &hpet_start)) {
		PRM_POINT("Too many disturbed HPET samples");
		goto out;
	}

	while (th.th_ext < target) {
		if (tsc_read() - th.th_tsc_ref > tsc_limit) {
			PRM_POINT("HPET main counter is not advancing");
			goto out;
		}
		(void) tscc_hpet_sample(&th, &tsc);
	}

	if (!tscc_hpet_block(&th, &tsc_end, &hpet_end)) {
		PRM_POINT("Too many disturbed HPET samples");
		goto out;
	}

	ok = B_TRUE;

out:
	restore_int_flag(flags);
	mmio_reg_block_unmap(&block);

	if (!ok)
		return (B_FALSE);

	/*
	 * Both blocks hold the same number of samples, so the difference
	 * between the two sums is the difference between the two averages
	 * multiplied by the block size, which cancels out in the ratio.
	 * The frequency is tsc_ticks * 10^15 / (hpet_ticks * period), which
	 * we compute in two steps so that no intermediate overflows 64 bits.
	 * The first step loses less than one part in 10^9 to truncation.
	 */
	const uint64_t tsc_ticks = tsc_end - tsc_start;
	const uint64_t ticks = hpet_end - hpet_start;

	if (tsc_ticks == 0 || ticks == 0 ||
	    tsc_ticks > UINT64_MAX / 1000000ULL ||
	    ticks > UINT64_MAX / NANOSEC) {
		return (B_FALSE);
	}

	const uint64_t a = tsc_ticks * 1000000ULL / period;
	*freqp = (a / ticks) * NANOSEC + (a % ticks) * NANOSEC / ticks;

	PRM_POINT("HPET calibration complete");
	PRM_DEBUG(*freqp);

	return (B_TRUE);
}

static tsc_calibrate_t tsc_calibration_hpet = {
	.tscc_source = "HPET",
	.tscc_preference = 20,
	.tscc_calibrate = tsc_calibrate_hpet,
};
TSC_CALIBRATION_SOURCE(tsc_calibration_hpet);
