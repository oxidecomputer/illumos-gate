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
 * Copyright 2025 Oxide Computer Company
 */

/*
 * When the AMD Milan SoC is initialized, this is done by passing a bunch of
 * configuration to the PSP through the SPI flash which is called the APCB.
 * After the PSP processes all this, it is transformed and output for us through
 * something called the APOB -- occasionally given as the AMD PSP Output Block,
 * AGESA PSP Output Buffer, and various other permutations. This file provides
 * basic top-level parsing code. It does not provide a means of accessing the
 * APOB, from any context. The functions here all require a pointer to a mapped
 * and valid APOB and make no assumptions about where the APOB might be in
 * either virtual or physical memory. Thus, this code can be shared by
 * earlyboot, normal kernel, and user code.
 *
 * The APOB is structured as an initial header (apob_header_t) which is always
 * immediately followed by the first entry (hence why it is in the structure).
 * Each entry itself contains its size and has an absolute offset to the next
 * entry.
 *
 * See sys/apob_impl.h for the requisite dire warnings about interface
 * stability.  This code is intended to be generic across all APOB
 * implementations, so it is necessarily limited in functionality, but do not
 * confuse this genericism with stability.
 *
 * The APOB is inherently immutable: it represents a snapshot in time, prior to
 * first instruction, of the partial state of the machine visible to or
 * determined by firmware.  While it is possible to copy the APOB all over the
 * place, none of the functions here provide for any kind of modification to the
 * APOB's contents.  In addition, these functions are lock-free; if a consumer
 * needs to pass in a pointer to an APOB that can go away, it is responsible for
 * providing reference counting or some other kind of mutual exclusion so that
 * can't happen while a handle is valid.
 */

#include <sys/stdint.h>
#include <sys/sysmacros.h>
#include <sys/stdbool.h>
#include <sys/apob_impl.h>
#include <sys/apob.h>
#include <sys/debug.h>

#ifdef	_KERNEL
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/boot_data.h>
#else
#include <stdio.h>
#include <stdarg.h>
#endif	/* _KERNEL */

/*
 * Signature value for the APOB. This is unsurprisingly "APOB". This is written
 * out in memory such that byte zero is 'A', etc. This means that when
 * interpreted as a little-endian integer the letters are reversed.  We keep it
 * in a byte form.
 */
static const uint8_t APOB_SIG[4] = { 'A', 'P', 'O', 'B' };

CTASSERT(APOB_MIN_LEN == sizeof (apob_header_t));

/*
 * This goop exists for two reasons: we don't have vsnprintf in the kernel until
 * genunix is loaded, so we have to use a special function instead during
 * earlyboot, and the kernel's and libc's vsnprintf implementations have
 * different signatures.
 */
static void
apob_vsnprintf(char *buf, size_t len, const char *fmt, va_list ap)
{
#ifdef	_KERNEL
	if (genunix_is_loaded()) {
		(void) vsnprintf(buf, len, fmt, ap);
	} else {
		buf[0] = 'e';
		buf[1] = 'b';
		buf[2] = '\0';
		kapob_eb_vprintf(fmt, ap);
	}
#else
	(void) vsnprintf(buf, len, fmt, ap);
#endif	/* _KERNEL */
}

static void
apob_verror(apob_hdl_t *apob, const int err, const char *fmt, va_list ap)
{
	apob_vsnprintf(apob->ah_errmsg, sizeof (apob->ah_errmsg), fmt, ap);
	apob->ah_err = err;
}

static void
apob_error(apob_hdl_t *apob, const int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	apob_verror(apob, err, fmt, ap);
	va_end(ap);
}

static void
apob_ok(apob_hdl_t *apob)
{
	apob->ah_errmsg[0] = '\0';
	apob->ah_err = 0;
}

int
apob_errno(const apob_hdl_t *apob)
{
	return (apob->ah_err);
}

const char *
apob_errmsg(const apob_hdl_t *apob)
{
	return (apob->ah_errmsg);
}

/*
 * Tells the caller how much memory a handle requires.  The handle itself is
 * opaque and this is common code so we aren't going to implement an allocator;
 * the caller has to do that.
 */
size_t
apob_handle_size(void)
{
	return (sizeof (apob_hdl_t));
}

/*
 * Initialise the handle to reference the supplied APOB, which is assumed to be
 * of length not more than (but possibly less than) limit_len bytes including
 * the header.  The APOB's signature and version number are validated.  The
 * caller must ensure that at least limit_len bytes are mapped and readable
 * beginning at ap.  The maximum possible size of an APOB is 4 GiB, as the
 * reported size field is only 32 bits, and the actual size of the APOB
 * beginning at ap is returned.  This allows the caller to use this in a manner
 * similar to snprintf; e.g.,
 *
 * const uint8_t *buf = map_something(addr, APOB_MIN_LEN);
 * apob_hdl_t *apob = alloc(apob_handle_size());
 * size_t apob_len = apob_init_handle(&apob, buf, APOB_MIN_LEN);
 *
 * if (apob_len > APOB_MIN_LEN) {
 *	unmap(buf, APOB_MIN_LEN);
 *	buf = map_something(addr, apob_len);
 *	if (buf == NULL) {
 *		apob_len = 0;
 *	} else {
 *		size_t new_len = apob_init_handle(&apob, buf, apob_len);
 *		VERIFY3U(new_len, ==, apob_len);
 *		apob_len = new_len;
 *	}
 * }
 * if (apob_len == 0) {
 *	error();
 * }
 *
 * Ideally we would do all of this ourselves, but the caller is responsible for
 * mapping or otherwise making the APOB available in memory and the way to do
 * that depends on context.  A return value of 0 indicates that the region does
 * not contain a valid APOB; the handle's error state is valid but the handle
 * cannot otherwise be used and any attempt to do so is programmer error, as is
 * passing in a value of limit_len less than APOB_MIN_LEN.
 */
size_t
apob_init_handle(apob_hdl_t *apob, const uint8_t *ap, const size_t limit_len)
{
	const apob_header_t *ahp = (const apob_header_t *)ap;

	if (limit_len < APOB_MIN_LEN) {
		apob_error(apob, EINVAL, "programmer error: length limit 0x%zx "
		    "is smaller than required minimum 0x%x", limit_len,
		    APOB_MIN_LEN);
		return (0);
	}

	/*
	 * Note, we can't use bcmp/memcmp here because we need to support
	 * calling this from earlyboot context in which krtld hasn't initialized
	 * them and they are in genunix.
	 */
	if (ahp->ah_sig[0] != APOB_SIG[0] || ahp->ah_sig[1] != APOB_SIG[1] ||
	    ahp->ah_sig[2] != APOB_SIG[2] || ahp->ah_sig[3] != APOB_SIG[3]) {
		apob_error(apob, EIO,
		    "bad APOB signature, found 0x%x 0x%x 0x%x 0x%x",
		    ahp->ah_sig[0], ahp->ah_sig[1], ahp->ah_sig[2],
		    ahp->ah_sig[3]);
		return (0);
	}

	/*
	 * The only version that this has been tested with is 0x18.  The
	 * meanings and evolution of versioning are undocumented and likely
	 * would not satisfy illumos engineering rules.  A version of 0x18 does
	 * not guarantee much of anything unfortunately, as far as we know.
	 */
	if (ahp->ah_vers != 0x18) {
		apob_error(apob, ENOTSUP,
		    "unrecognised APOB version 0x%x", ahp->ah_vers);
		return (0);
	}

	apob->ah_header = ahp;
	apob->ah_len = MIN(limit_len, ahp->ah_size);
	apob_ok(apob);

	return ((size_t)ahp->ah_size);
}

size_t
apob_get_len(const apob_hdl_t *apob)
{
	return (apob->ah_len);
}

const uint8_t *
apob_get_raw(const apob_hdl_t *apob)
{
	if (apob->ah_len == 0)
		return (NULL);

	return ((const uint8_t *)apob->ah_header);
}

/*
 * Walk through entries returning each to the caller in turn. Entries have
 * their size embedded in them with pointers to the next one. This leads to
 * lots of uintptr_t arithmetic. Sorry.
 */
static const apob_entry_t *
apob_iter(apob_hdl_t *apob, const apob_entry_t *last)
{
	const uintptr_t base = (const uintptr_t)apob->ah_header;
	const uintptr_t limit = base + apob->ah_len;
	const uintptr_t hdr_limit = base + apob->ah_header->ah_size;
	const apob_entry_t *entry;
	uintptr_t curaddr;

	/*
	 * Guaranteed by handle construction: we won't examine memory beyond the
	 * self-reported end of the APOB even if it's mapped.
	 */
	VERIFY3U(limit, <=, hdr_limit);

	if (last == NULL)
		curaddr = base + apob->ah_header->ah_off;
	else
		curaddr = (uintptr_t)(void *)last + last->ae_size;

	if (curaddr + sizeof (apob_entry_t) > limit)
		return (NULL);

	entry = (const apob_entry_t *)curaddr;

	/*
	 * First ensure that this item's size actually all fits within
	 * our bound. If not, then we're sol.
	 */
	if (entry->ae_size < sizeof (apob_entry_t)) {
		apob_error(apob, EIO,
		    "encountered entry at offset 0x%lx with too small "
		    "size 0x%x", curaddr - base, entry->ae_size);
		return (NULL);
	}

	/*
	 * We distinguish the case in which the entry extends beyond the
	 * self-reported end of the APOB (an error in the construction
	 * of the APOB) from the case in which it extends beyond the
	 * part of the APOB we actually have (not an error to us and the
	 * caller can handle it).
	 */
	if (curaddr + entry->ae_size > hdr_limit) {
		apob_error(apob, EIO,
		    "encountered entry at offset 0x%lx with size 0x%x "
		    "that extends beyond self-reported limit 0x%lx",
		    curaddr - base, entry->ae_size, hdr_limit);
		return (NULL);
	}

	return (entry);
}

/*
 * Walk through entries attempting to find the first entry that matches the
 * requested group, type, and instance. The size we return in *lenp is the
 * number of bytes in the data portion of the entry; it can in principle be 0
 * so the caller must not assume that the entry actually contains a specific
 * data structure without checking. It may also be less than the total size of
 * the entry if the entry extends beyond the available part of the APOB (i.e.,
 * if the APOB is not entirely mapped).
 */
const void *
apob_find(apob_hdl_t *apob, const apob_group_t group, const uint32_t type,
    const uint32_t inst, size_t *lenp)
{
	const uintptr_t base = (const uintptr_t)apob->ah_header;
	const uintptr_t limit = base + apob->ah_len;
	const apob_entry_t *entry = NULL;

	while ((entry = apob_iter(apob, entry)) != NULL) {
		if (entry->ae_group == (const uint32_t)group &&
		    entry->ae_type == type && entry->ae_inst == inst) {
			uintptr_t curaddr = (uintptr_t)(void *)entry;
			size_t len = MIN(entry->ae_size, limit - curaddr);

			/*
			 * Guaranteed by our loop entry condition: the non-data
			 * portion of the entry must fit within the bounds of
			 * the mapped portion of the APOB.
			 */
			VERIFY3U(len, >=, offsetof(apob_entry_t, ae_data));

			len -= offsetof(apob_entry_t, ae_data);
			*lenp = len;
			apob_ok(apob);
			return (&entry->ae_data);
		}
	}

	apob_error(apob, ENOENT, "no entry found matching (%u, %u, %u) in "
	    "[0x%lx, 0x%lx)", (const uint32_t)group, type, inst, base, limit);
	return (NULL);
}

uint8_t *
apob_entry_hmac(const apob_entry_hdl_t *hdl)
{
	apob_entry_t *ae = (apob_entry_t *)hdl;

	return (ae->ae_hmac);
}

/*
 * Walk through entries collecting pointers to those which match the
 * requested group and type. If no entries are found this function still
 * returns successfully but with *nump (the number of entries found) set to 0.
 */
bool
apob_gather(apob_hdl_t *apob, const apob_group_t group, const uint32_t type,
    apob_entry_hdl_t *entries[], size_t *nump)
{
	const apob_entry_t *entry = NULL;
	const size_t entry_limit = *nump;
	size_t index = 0;

	while ((entry = apob_iter(apob, entry)) != NULL) {
		if (entry->ae_group == (const uint32_t)group &&
		    entry->ae_type == type) {
			if (index >= entry_limit) {
				apob_error(apob, EOVERFLOW,
				    "found more than 0x%zx entries",
				    entry_limit);
				return (false);
			}
			entries[index++] = (apob_entry_hdl_t *)entry;
		}
	}

	*nump = index;
	return (true);
}
