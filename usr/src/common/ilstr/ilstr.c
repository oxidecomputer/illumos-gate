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

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stdbool.h>
#else
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>
#endif
#include <sys/debug.h>
#include <sys/sysmacros.h>

#include <sys/ilstr.h>

static bool ilstr_have_space(ilstr_t *, size_t);

void
ilstr_init(ilstr_t *ils, int kmflag)
{
	bzero(ils, sizeof (*ils));
	ils->ils_kmflag = kmflag;
}

void
ilstr_init_prealloc(ilstr_t *ils, char *buf, size_t buflen)
{
	bzero(ils, sizeof (*ils));
	ils->ils_data = buf;
	ils->ils_datalen = buflen;
	ils->ils_data[0] = '\0';
	ils->ils_flag |= ILSTR_FLAG_PREALLOC;
}

void
ilstr_reset(ilstr_t *ils)
{
	if (ils->ils_strlen > 0) {
		/*
		 * Truncate the string but do not free the buffer so that we
		 * can use it again without further allocation.
		 */
		ils->ils_data[0] = '\0';
		ils->ils_strlen = 0;
	}
	ils->ils_errno = ILSTR_ERROR_OK;
}

void
ilstr_fini(ilstr_t *ils)
{
	if (ils->ils_flag & ILSTR_FLAG_PREALLOC) {
		/*
		 * Preallocated strings require no additional cleanup.
		 *
		 * We do not touch the underlying buffer so that the assembled
		 * string can be used after it is released.
		 */
		return;
	}

	if (ils->ils_data != NULL) {
#ifdef _KERNEL
		kmem_free(ils->ils_data, ils->ils_datalen);
#else
		free(ils->ils_data);
#endif
	}
}

void
ilstr_append_str(ilstr_t *ils, const char *s)
{
	size_t len;

	if (ils->ils_errno != ILSTR_ERROR_OK) {
		return;
	}

	if ((len = strlen(s)) < 1) {
		return;
	}

	if (!ilstr_have_space(ils, len)) {
		return;
	}

	/*
	 * Copy the string, including the terminating byte:
	 */
	bcopy(s, ils->ils_data + ils->ils_strlen, len + 1);
	ils->ils_strlen += len;
}

/*
 * Confirm that there are needbytes free bytes for string characters left in
 * the buffer.  If there are not, try to grow the buffer unless this string is
 * backed by preallocated memory.  Note that, like the return from strlen(),
 * needbytes does not include the extra byte required for null termination.
 */
static bool
ilstr_have_space(ilstr_t *ils, size_t needbytes)
{
	/*
	 * Make a guess at a useful allocation chunk size.  We want small
	 * strings to remain small, but very large strings should not incur the
	 * penalty of constant small allocations.
	 */
	size_t chunksz = 64;
	if (ils->ils_datalen > 3 * chunksz) {
		chunksz = P2ROUNDUP(ils->ils_datalen / 3, 64);
	}

	/*
	 * Check to ensure that the new string length does not overflow,
	 * leaving room for the termination byte:
	 */
	if (needbytes >= SIZE_MAX - ils->ils_strlen - 1) {
		ils->ils_errno = ILSTR_ERROR_OVERFLOW;
		return (false);
	}
	size_t new_strlen = ils->ils_strlen + needbytes;

	if (new_strlen + 1 > ils->ils_datalen) {
		size_t new_datalen = ils->ils_datalen;
		char *new_data;

		if (ils->ils_flag & ILSTR_FLAG_PREALLOC) {
			/*
			 * We cannot grow a preallocated string.
			 */
			ils->ils_errno = ILSTR_ERROR_NOMEM;
			return (false);
		}

		/*
		 * Grow the string buffer to make room for the new string.
		 */
		while (new_datalen < new_strlen + 1) {
			if (chunksz >= SIZE_MAX - new_datalen) {
				ils->ils_errno = ILSTR_ERROR_OVERFLOW;
				return (false);
			}
			new_datalen += chunksz;
		}

#ifdef _KERNEL
		new_data = kmem_alloc(new_datalen, ils->ils_kmflag);
#else
		new_data = malloc(new_datalen);
#endif
		if (new_data == NULL) {
			ils->ils_errno = ILSTR_ERROR_NOMEM;
			return (false);
		}

		if (ils->ils_data != NULL) {
			bcopy(ils->ils_data, new_data, ils->ils_strlen + 1);
#ifdef _KERNEL
			kmem_free(ils->ils_data, ils->ils_datalen);
#else
			free(ils->ils_data);
#endif
		}

		ils->ils_data = new_data;
		ils->ils_datalen = new_datalen;
	}

	return (true);
}

#ifdef _KERNEL
void
ilstr_append_uint(ilstr_t *ils, uint_t n)
{
	char buf[64];

	if (ils->ils_errno != ILSTR_ERROR_OK) {
		return;
	}

	VERIFY3U(snprintf(buf, sizeof (buf), "%u", n), <, sizeof (buf));

	ilstr_append_str(ils, buf);
}
#endif	/* _KERNEL */

#ifndef _KERNEL
void
ilstr_aprintf(ilstr_t *ils, const char *fmt, ...)
{
	va_list ap;

	if (ils->ils_errno != ILSTR_ERROR_OK) {
		return;
	}

	va_start(ap, fmt);
	ilstr_vaprintf(ils, fmt, ap);
	va_end(ap);
}

void
ilstr_vaprintf(ilstr_t *ils, const char *fmt, va_list ap)
{
	if (ils->ils_errno != ILSTR_ERROR_OK) {
		return;
	}

	/*
	 * First, determine the length of the string we need to construct:
	 */
	va_list tap;
	va_copy(tap, ap);
	int len = vsnprintf(NULL, 0, fmt, tap);

	if (len < 0) {
		ils->ils_errno = ILSTR_ERROR_PRINTF;
		return;
	}

	/*
	 * Grow the buffer to hold the string:
	 */
	if (!ilstr_have_space(ils, len)) {
		return;
	}

	/*
	 * Now, render the string into the buffer space we have made available:
	 */
	if ((len = vsnprintf(ils->ils_data + ils->ils_strlen,
	    len + 1, fmt, ap)) < 0) {
		ils->ils_errno = ILSTR_ERROR_PRINTF;
		return;
	}
	ils->ils_strlen += len;
}
#endif	/* !_KERNEL */

void
ilstr_append_char(ilstr_t *ils, char c)
{
	char buf[2];

	if (ils->ils_errno != ILSTR_ERROR_OK) {
		return;
	}

	buf[0] = c;
	buf[1] = '\0';

	ilstr_append_str(ils, buf);
}

ilstr_errno_t
ilstr_errno(ilstr_t *ils)
{
	return (ils->ils_errno);
}

const char *
ilstr_cstr(ilstr_t *ils)
{
	if (ils->ils_data == NULL) {
		VERIFY3U(ils->ils_datalen, ==, 0);
		VERIFY3U(ils->ils_strlen, ==, 0);

		/*
		 * This function should never return NULL.  If no buffer has
		 * been allocated, return a pointer to a zero-length string.
		 */
		return ("");
	}

	return (ils->ils_data);
}

size_t
ilstr_len(ilstr_t *ils)
{
	return (ils->ils_strlen);
}

const char *
ilstr_errstr(ilstr_t *ils)
{
	switch (ils->ils_errno) {
	case ILSTR_ERROR_OK:
		return ("ok");
	case ILSTR_ERROR_NOMEM:
		return ("could not allocate memory");
	case ILSTR_ERROR_OVERFLOW:
		return ("tried to construct too large a string");
	case ILSTR_ERROR_PRINTF:
		return ("invalid printf arguments");
	default:
		return ("unknown error");
	}
}
