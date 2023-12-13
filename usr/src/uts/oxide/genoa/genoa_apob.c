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
 * Initialization of the AMD Genoa SoC includes passing configuration to
 * the PSP through the SPI flash via the APCB.  The PSP processes the given
 * APCB, transforms it, and leaves the transformed output for us through
 * something called the APOB -- AMD PSP Output Block.
 *
 * The APOB is structured as an initial header (genoa_apob_header_t) that
 * is always immediately followed by the first entry (hence why it is in the
 * structure). Each entry contains its size and has an absolute offset to
 * the next entry.
 *
 * This code attempts to read, parse, and provide a means to access the
 * APOB.
 *
 * We provide access to the APOB through as an soc-bootops style service.
 * Anything that we care about is added as a property in the devinfo tree.
 *
 * This relies entirely on boot services and so we must be careful about
 * the operations we use to ensure that we can get torn down with boot
 * services later.
 */

#include <sys/machparam.h>
#include <sys/stdint.h>
#include <sys/bootconf.h>
#include <sys/sysmacros.h>
#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>

#include <vm/kboot_mmu.h>

#include <genoa/genoa_apob_impl.h>
#include <genoa/genoa_apob.h>

/*
 * Signature value for the APOB. This is unsurprisingly "APOB", stored
 * in memory such that byte zero is 'A', etc, (that is, big-endian).
 * Thus this constant actually represents 'BOPA' when interpreted as a
 * 32-bit integer. We keep it in byte form.
 */
static const uint8_t GENOA_APOB_SIG[4] = { 'A', 'P', 'O', 'B' };

/*
 * Since we don't know the size of the APOB, we purposefully set an upper bound
 * of what we'll accept. Examples we have seen in the wild are around ~300 KiB;
 * however, because this can contain information for every DIMM in the system this
 * size can vary wildly.
 */
static const uint32_t GENOA_APOB_SIZE_CAP = 4 * 1024 * 1024;

static const genoa_apob_header_t *genoa_apob_header;
static size_t genoa_apob_len;

/*
 * Initialize the APOB and set the `genoa_apob_header` pointer and
 * `genoa_apob_len` size.
 *
 * We are given a PA that theoretically addresses the APOB. Because the size
 * is embedded in the APOB itself, we have two paths:
 *
 * 1. Just map a large amount of VA space that constrains the APOB size.
 * 2. Map the first page, check the size and then allocate more VA space
 *    by either allocating the total required or trying to rely on properties
 *    of the VA allocator being contiguous.
 *
 * The first is the simpler path.
 *
 * XXX(cross): A third option is a combination of the two: map the first
 * plage, extract the size, unmap it, and then map a continguous region
 * based on the extracted size.
 */
void
genoa_apob_init(uint64_t apob_pa)
{
	uintptr_t base;

	base = kbm_valloc(GENOA_APOB_SIZE_CAP, MMU_PAGESIZE);
	if (base == 0) {
		bop_panic("failed to allocate %u bytes of VA for the APOB",
		    genoa_apob_size_cap);
	}
	EB_DBGMSG("APOB VA is [%lx, %lx)\n", base, base + GENOA_APOB_SIZE_CAP);

	/*
	 * With the allocation of VA done, map the first 4 KiB and verify that
	 * things check out before we do anything else. Yes, this means that we
	 * lose 4 KiB pages and are eating up more memory for PTEs, but since
	 * this will all get thrown away when we're done with boot, let's not
	 * worry about optimize.
	 */
	kbm_map(base, apob_pa, 0, 0);

	genoa_apob_header = (genoa_apob_header_t *)base;

	/*
	 * Right now this assumes that the presence of the APOB is load bearing
	 * for various reasons. It'd be nice to reduce this dependency and
	 * therefore actually not panic below.
	 *
	 * Note, we can't use bcmp/memcmp at this phase of boot because krtld
	 * hasn't initialized them and they are in genunix.
	 */
	if (genoa_apob_header->mah_sig[0] != GENOA_APOB_SIG[0] ||
	    genoa_apob_header->mah_sig[1] != GENOA_APOB_SIG[1] ||
	    genoa_apob_header->mah_sig[2] != GENOA_APOB_SIG[2] ||
	    genoa_apob_header->mah_sig[3] != GENOA_APOB_SIG[3]) {
		bop_panic("Bad APOB signature, found 0x%x 0x%x 0x%x 0x%x",
		    genoa_apob_header->mah_sig[0],
		    genoa_apob_header->mah_sig[1],
		    genoa_apob_header->mah_sig[2],
		    genoa_apob_header->mah_sig[3]);
	}

	genoa_apob_len = MIN(genoa_apob_header->mah_size, genoa_apob_size_cap);
	for (size_t offset = MMU_PAGESIZE;
	    offset < genoa_apob_len;
	    offset += MMU_PAGESIZE) {
		kbm_map(base + offset, apob_pa + offset, 0, 0);
	}

	eb_physmem_reserve_range(apob_pa,
	    P2ROUNDUP(genoa_apob_len, MMU_PAGESIZE), EBPR_NO_ALLOC);
}

/*
 * Walk through the APOB attempting to find the first entry that matches the
 * requested group, type, and instance.
 *
 * Entries have their size embedded in them and contain pointers to the next
 * one, which leads to lots of uintptr_t arithmetic (Sorry).  The size we return
 * in *lenp is the number of bytes in the data portion of the entry; this can in
 * theory be 0 so the caller must check before assuming that the entry actually
 * contains a specific data structure.
 */
const void *
genoa_apob_find(genoa_apob_group_t group, uint32_t type, uint32_t inst,
    size_t *lenp, int *errp)
{
	const uintptr_t apob_base = (uintptr_t)genoa_apob_header;
	const uintptr_t limit = apob_base + genoa_apob_len;
	uintptr_t curaddr;

	if (genoa_apob_header == NULL) {
		*errp = ENOTSUP;
		return (NULL);
	}

	curaddr = apob_base + genoa_apob_header->mah_off;
	while (curaddr + sizeof (genoa_apob_entry_t) < limit) {
		const genoa_apob_entry_t *entry = (genoa_apob_entry_t *)curaddr;

		/*
		 * First ensure that this items size actually all fits within
		 * our bound. If not, we fail.
		 */
		if (entry->mae_size < sizeof (genoa_apob_entry_t)) {
			EB_DBGMSG("Encountered APOB entry at offset 0x%lx with "
			    "too small size 0x%x",
			    curaddr - apob_base, entry->mae_size);
			*errp = EIO;
			return (NULL);
		}
		if (curaddr + entry->mae_size >= limit) {
			EB_DBGMSG("Encountered APOB entry at offset 0x%lx with "
			    "size 0x%x that extends beyond limit",
			    curaddr - apob_base, entry->mae_size);
			*errp = EIO;
			return (NULL);
		}

		if (entry->gae_group == group && entry->mae_type == type &&
		    entry->mae_inst == inst) {
			*lenp = entry->mae_size -
			    offsetof(genoa_apob_entry_t, mae_data);
			*errp = 0;
			return (&entry->mae_data);
		}

		curaddr += entry->mae_size;
	}

	*errp = ENOENT;
	return (NULL);
}
