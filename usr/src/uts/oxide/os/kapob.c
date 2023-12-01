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
 * When an AMD SoC is initialized, a variety of parameters are sourced from the
 * boot flash by the AGESA Boot Loader (ABL) running on the PSP; AMD calls this
 * the APCB which has several possible expansions depending on the source you
 * prefer (we'll go with AGESA PSP Configuration Block).  After the ABL
 * processes all this, parts of it, along with data about the ABL's activities,
 * are transformed and output for us into RAM in the form of something called
 * the APOB (AGESA PSP Output Block). This file attempts to iterate, parse, and
 * provide a means of getting at it.  For the most part, the APOB's contents
 * aren't interesting; we can get the same information from the hardware, and
 * prefer that source.  There are a few possible exceptions related to memory
 * initialisation, however, so we keep the data around and provide these simple
 * mechanisms for interpreting it.
 *
 * The APOB is a TLV-ish type structure; we are given its total size but finding
 * data of interest requires walking it from the start.  We get an initial
 * header (apob_header_t) which is always immediately followed by the first
 * entry (hence why it is in the structure). Each entry itself contains its size
 * and has an absolute offset to the next entry.
 *
 * While we currently keep the APOB around forever, we don't intend that it be
 * accessed by kernel consumers once we've booted and attached any drivers that
 * might be interested in it (e.g., memory controller RAS).  Our intention is
 * that anything we really need from the APOB will end up in some other kernel
 * data structure such as the devinfo tree; however, mainly for diagnostic
 * purposes we do provide a generic access mechanism for user software.  The
 * APOB is considered read-only at all times; it makes no sense to modify it and
 * indeed it's important that consumers understand it may not reflect the actual
 * state of the machine, only what firmware wanted us to believe the state of
 * the machine was when we started running.
 *
 * As far as we know, the basic structure of the APOB itself has remained the
 * same for a long time, and what we have here is useful on a range of different
 * processors with different ABL versions.  While the APOB contains some
 * self-describing version information, observations indicate that this isn't
 * very reliable in terms of describing the format of the APOB's contents.  The
 * code here does not interpret the contents, only the basic structural
 * metadata, which appears fairly stable.  All we can do is hope that any major
 * format change will come with a non-overlapping set of version numbers.  In
 * addition, the interpretation of entries in the APOB is entirely specific to
 * the processor family and possibly also the firmware revision; consumers, both
 * kernel and userland, are responsible for selecting the proper interpretation
 * on whatever nebulous and unreliable basis they prefer. AMD considers the APOB
 * format to be a Private interface between parts of their firmware, and on PCs
 * it is not (intentionally) exposed to the OS at all.  In fact, it is kept in
 * "BIOS reserved" memory and can be accessed via the xsvc driver just like ACPI
 * tables, provided the user knows where it is.  We provide a much more
 * straightforward user access mechanism on this platform, since the APOB is
 * merely a chunk of ordinary kernel memory.
 */

#include <sys/machparam.h>
#include <sys/stdint.h>
#include <sys/bootconf.h>
#include <sys/sysmacros.h>
#include <sys/boot_data.h>
#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>
#include <sys/debug.h>
#include <sys/varargs.h>
#include <sys/apob.h>
#include <sys/apob_impl.h>
#include <sys/kapob.h>
#include <sys/cmn_err.h>
#include <sys/stdbool.h>
#include <vm/kboot_mmu.h>
#include <sys/sunddi.h>

static bool kapob_is_umem = false;
static kmutex_t kapob_hdl_lock;
static apob_hdl_t kapob_hdl;
static ddi_umem_cookie_t kapob_umem_cookie;

/*
 * Initialize the APOB. We've been told that we have a PA that theoretically
 * this exists at. The size is embedded in the APOB itself, so we map the first
 * page, check the size and then construct all the mappings and reserve the
 * underlying pages.  The mappings and pages will be freed automatically toward
 * the end of boot, like all memory we get from kbm.  The caller is responsible
 * for telling us how much memory the APOB could possibly occupy.
 */
void
kapob_eb_init(uint64_t apob_pa, uint64_t limit)
{
	size_t hdr_len = APOB_MIN_LEN;
	size_t apob_len, max_len, new_len;
	uintptr_t base;

	if (limit < apob_pa || limit - apob_pa < hdr_len) {
		bop_panic("APOB: region bounds [%lx, %lx) are too small",
		    apob_pa, limit);
	}

	hdr_len = P2ROUNDUP(hdr_len, MMU_PAGESIZE);

	base = kbm_valloc(hdr_len, MMU_PAGESIZE);
	if (base == 0) {
		bop_panic("failed to allocate %lu bytes of VA for the APOB",
		    hdr_len);
	}
	EB_DBGMSG("APOB: header VA is [%lx, %lx)\n", base, base + hdr_len);

	/*
	 * With the allocation of VA done, map the first 4 KiB and verify that
	 * things check out before we do anything else. Yes, this means that we
	 * lose 4 KiB pages and are eating up more memory for PTEs, but since
	 * this will all get thrown away when we're done with boot, let's not
	 * worry about optimize.
	 */
	for (size_t i = 0; i < hdr_len; i += MMU_PAGESIZE) {
		kbm_map(base + i, apob_pa + i, 0, 0);
	}

	max_len = apob_init_handle(&kapob_hdl, (const uint8_t *)base, hdr_len);

	/*
	 * The APOB is invalid; we have a valid errno but no valid errmsg
	 * because we don't have vsnprintf; however, kapob_eb_vprintf has
	 * already printed a message to the earlyboot console.  It would be nice
	 * not to have to panic here, but for now we assume the APOB is
	 * load-bearing.
	 */
	if (max_len == 0) {
		bop_panic("APOB: initialisation failed with error %d",
		    apob_errno(&kapob_hdl));
	}

	if (apob_pa + max_len > limit) {
		EB_DBGMSG("APOB: header-provided bounds [%lx, %lx) extend "
		    "beyond limit of %lx; truncating\n",
		    apob_pa, apob_pa + max_len, limit);
		apob_len = limit - apob_pa;
	} else {
		apob_len = max_len;
	}

	base = kbm_valloc(P2ROUNDUP(apob_len, MMU_PAGESIZE), MMU_PAGESIZE);
	if (base == 0) {
		bop_panic("failed to allocate %lu bytes of VA for the APOB",
		    apob_len);
	}
	EB_DBGMSG("APOB: VA is [%lx, %lx)\n", base, base + apob_len);

	for (size_t i = 0; i < apob_len; i += MMU_PAGESIZE) {
		kbm_map(base + i, apob_pa + i, 0, 0);
	}

	new_len = apob_init_handle(&kapob_hdl, (const uint8_t *)base, apob_len);

	if (new_len != max_len) {
		bop_panic("APOB: reinitialisation failed with error %d "
		    "(size %lu != expected size %lu)",
		    apob_errno(&kapob_hdl), new_len, max_len);
	}

	eb_physmem_reserve_range(apob_pa,
	    P2ROUNDUP(apob_len, MMU_PAGESIZE), EBPR_NO_ALLOC);
}

void
kapob_eb_vprintf(const char *fmt, va_list ap)
{
	eb_vprintf(fmt, ap);
}

/*
 * Preserve the APOB across the transition from earlyboot so that it survives
 * freeing of earlyboot pages and mappings.  We use umem here instead of normal
 * kmem to simplify access to the APOB from userland; this has no practical
 * effect on kernel accesses.  This memory is never freed.
 */
void
kapob_preserve(void)
{
	const uint8_t *data = apob_get_raw(&kapob_hdl);
	uint8_t *new_data;
	size_t len = apob_get_len(&kapob_hdl);
	size_t new_len;

	VERIFY(!kapob_is_umem);
	VERIFY3U(len, >=, APOB_MIN_LEN);
	VERIFY3P(data, !=, NULL);

	new_data = ddi_umem_alloc(ptob(btopr(len)), DDI_UMEM_SLEEP,
	    &kapob_umem_cookie);
	bcopy(data, new_data, len);

	mutex_init(&kapob_hdl_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_enter(&kapob_hdl_lock);

	new_len = apob_init_handle(&kapob_hdl, new_data, len);
	VERIFY3U(new_len, ==, len);
	kapob_is_umem = true;

	mutex_exit(&kapob_hdl_lock);

	/*
	 * We don't free the earlyboot APOB storage; both the underlying memory
	 * and the mappings will be automatically discarded later in the boot
	 * process.
	 */
}

/*
 * Thin wrapper around apob_find() that always uses the kernel's global copy of
 * the APOB (whichever one that is at the moment) and provides mutual exclusion
 * and error logging.
 */
const void *
kapob_find(const apob_group_t group, const uint32_t type,
    const uint32_t instance, size_t *lenp, int *errp)
{
	const void *entry;

	if (kapob_is_umem) {
		mutex_enter(&kapob_hdl_lock);
	}

	entry = apob_find(&kapob_hdl, group, type, instance, lenp);
	*errp = apob_errno(&kapob_hdl);

	if (entry == NULL) {
		if (genunix_is_loaded()) {
			cmn_err(CE_NOTE, "APOB: %s (errno = %d)",
			    apob_errmsg(&kapob_hdl), apob_errno(&kapob_hdl));
		} else {
			EB_DBGMSG("APOB: errno = %d\n", apob_errno(&kapob_hdl));
		}
	}

	if (kapob_is_umem) {
		mutex_exit(&kapob_hdl_lock);
	}

	return (entry);
}

/*
 * Provides the caller with a clone of the kernel's APOB handle.  This is
 * permitted only after the post-earlyboot preservation step has been completed;
 * i.e., when kapob_is_umem is true.  Returns true on success and false on
 * failure.  Note that callers are never given access to our own handle, but the
 * cloned handles do share the kernel's read-only APOB storage.  We also provide
 * the caller with the umem cookie; the primary consumer of this is apob(4d)
 * which is going to need it.  The caller must not call ddi_umem_free or do
 * anything else that would modify or free the cookie; the only acceptable use
 * is as an argument to devmap_umem_setup, which is safe.  This looks a little
 * sketchy but it's really no different from a normal driver allocating memory
 * once and then allowing multiple mappings to it via devmap.
 */
bool
kapob_clone_handle(apob_hdl_t *clone, ddi_umem_cookie_t *cp)
{
	const uint8_t *data = apob_get_raw(&kapob_hdl);
	size_t len = apob_get_len(&kapob_hdl);
	size_t clone_len;

	if (!kapob_is_umem || data == NULL || len < APOB_MIN_LEN)
		return (false);

	VERIFY3P(kapob_umem_cookie, !=, NULL);

	bzero(clone, apob_handle_size());
	clone_len = apob_init_handle(clone, data, apob_get_len(&kapob_hdl));

	VERIFY3U(len, ==, clone_len);

	*cp = kapob_umem_cookie;

	return (true);
}
