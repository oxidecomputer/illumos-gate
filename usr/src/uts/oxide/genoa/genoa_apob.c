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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * When the AMD Milan SoC is initialized, this is done by passing a bunch of
 * configuration to the PSP through the SPI flash which is called the APCB.
 * After the PSP processes all this, it is transformed and output for us through
 * something called the APOB -- AMD PSP Output Block. This file attempts to
 * iterate, parse, and provide a means of getting at it.
 *
 * Our intention is that access to the APOB through this mechanism is provided
 * as an soc-bootops style service. Anything that is cared about should be added
 * as a property in the devinfo tree.
 *
 * This relies entirely on boot services for things and as such we have to be a
 * bit careful about the operations that we use to ensure that we can get torn
 * down with boot services.
 *
 * The APOB is structured as an initial header (milan_apob_header_t) which is
 * always immediately followed by the first entry (hence why it is in the
 * structure). Each entry itself contains its size and has an absolute offset to
 * the next entry.
 */

#include <sys/machparam.h>
#include <sys/stdint.h>
#include <sys/bootconf.h>
#include <sys/sysmacros.h>
#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>

#include <vm/kboot_mmu.h>

#include <milan/milan_apob_impl.h>
#include <milan/milan_apob.h>

/*
 * Signature value for the APOB. This is unsurprisingly "APOB". This is written
 * out in memory such that byte zero is 'A', etc. This means that when inter
 * petted as a little-endian value the letters are reversed. This this constant
 * actually represents 'BOPA'. We keep it in a byte form.
 */
static const uint8_t milan_apob_sig[4] = { 'A', 'P', 'O', 'B' };

/*
 * Since we don't know the size of the APOB, we purposefully set an upper bound
 * of what we'll accept for its size. Example ones we've seen in the wild are
 * around ~300 KiB; however, because this can contain information for every DIMM
 * in the system this size can vary wildly.
 */
static uint32_t milan_apob_size_cap = 4 * 1024 * 1024;

static const milan_apob_header_t *milan_apob_header;
static size_t milan_apob_len;

/*
 * Initialize the APOB. We've been told that we have a PA that theoretically
 * this exists at. Because the size is embedded in the APOB itself, we have two
 * general paths. The first is to just map a large amount of VA which we use to
 * constrain the size of this. The second is to map the first page, check the
 * size and then allocate more VA by either allocating the total required or
 * trying to rely on properties of the VA allocator being contiguous. The
 * simpler path here is just to do he first one of these based on our maximum
 * size.
 */
void
milan_apob_init(uint64_t apob_pa)
{
	uintptr_t base;

	base = kbm_valloc(milan_apob_size_cap, MMU_PAGESIZE);
	if (base == 0) {
		bop_panic("failed to allocate %u bytes of VA for the APOB",
		    milan_apob_size_cap);
	}
	EB_DBGMSG("APOB VA is [%lx, %lx)\n", base, base + milan_apob_size_cap);

	/*
	 * With the allocation of VA done, map the first 4 KiB and verify that
	 * things check out before we do anything else. Yes, this means that we
	 * lose 4 KiB pages and are eating up more memory for PTEs, but since
	 * this will all get thrown away when we're done with boot, let's not
	 * worry about optimize.
	 */
	kbm_map(base, apob_pa, 0, 0);

	milan_apob_header = (milan_apob_header_t *)base;

	/*
	 * Right now this assumes that the presence of the APOB is load bearing
	 * for various reasons. It'd be nice to reduce this and therefore
	 * actually not panic below. Note, we can't use bcmp/memcmp at this
	 * phase of boot because krtld hasn't initialized them and they are in
	 * genunix.
	 */
	if (milan_apob_header->mah_sig[0] != milan_apob_sig[0] ||
	    milan_apob_header->mah_sig[1] != milan_apob_sig[1] ||
	    milan_apob_header->mah_sig[2] != milan_apob_sig[2] ||
	    milan_apob_header->mah_sig[3] != milan_apob_sig[3]) {
		bop_panic("Bad APOB signature, found 0x%x 0x%x 0x%x 0x%x",
		    milan_apob_header->mah_sig[0],
		    milan_apob_header->mah_sig[1],
		    milan_apob_header->mah_sig[2],
		    milan_apob_header->mah_sig[3]);
	}

	milan_apob_len = MIN(milan_apob_header->mah_size, milan_apob_size_cap);
	for (size_t i = MMU_PAGESIZE; i < milan_apob_len; i += MMU_PAGESIZE) {
		kbm_map(base + i, apob_pa + i, 0, 0);
	}

	eb_physmem_reserve_range(apob_pa,
	    P2ROUNDUP(milan_apob_len, MMU_PAGESIZE), EBPR_NO_ALLOC);
}

/*
 * Walk through entires attempting to find the first entry that matches the
 * requested group, type, and instance. Entries have their size embedded in them
 * with pointers to the next one. This leads to lots of uintptr_t arithmetic.
 * Sorry.  The size we return in *lenp is the number of bytes in the data
 * portion of the entry; it can in principle be 0 so the caller must not assume
 * that the entry actually contains a specific data structure without checking.
 */
const void *
milan_apob_find(milan_apob_group_t group, uint32_t type, uint32_t inst,
    size_t *lenp, int *errp)
{
	uintptr_t curaddr;
	const uintptr_t limit = (uintptr_t)milan_apob_header + milan_apob_len;

	if (milan_apob_header == NULL) {
		*errp = ENOTSUP;
		return (NULL);
	}

	curaddr = (uintptr_t)milan_apob_header + milan_apob_header->mah_off;
	while (curaddr + sizeof (milan_apob_entry_t) < limit) {
		const milan_apob_entry_t *entry = (milan_apob_entry_t *)curaddr;

		/*
		 * First ensure that this items size actually all fits within
		 * our bound. If not, then we're sol.
		 */
		if (entry->mae_size < sizeof (milan_apob_entry_t)) {
			EB_DBGMSG("Encountered APOB entry at offset 0x%lx with "
			    "too small size 0x%x", curaddr -
			    (uintptr_t)milan_apob_header, entry->mae_size);
			*errp = EIO;
			return (NULL);
		}
		if (curaddr + entry->mae_size >= limit) {
			EB_DBGMSG("Encountered APOB entry at offset 0x%lx with "
			    "size 0x%x that extends beyond limit", curaddr -
			    (uintptr_t)milan_apob_header, entry->mae_size);
			*errp = EIO;
			return (NULL);
		}

		if (entry->mae_group == group && entry->mae_type == type &&
		    entry->mae_inst == inst) {
			*lenp = entry->mae_size -
			    offsetof(milan_apob_entry_t, mae_data);
			*errp = 0;
			return (&entry->mae_data);
		}

		curaddr += entry->mae_size;
	}

	*errp = ENOENT;
	return (NULL);
}
