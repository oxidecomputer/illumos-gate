/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


/*
 * Copyright (c) 1985, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 	This product includes software developed by the University of
 * 	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "port_before.h"
#include <sys/types.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <ctype.h>
#include RESOLV_HEADER
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "port_after.h"

#ifndef	ORIGINAL_ISC_CODE
#pragma weak	__dn_skipname	=	dn_skipname
#pragma weak	__res_dnok	=	res_dnok
#pragma weak	__res_hnok	=	res_hnok
#pragma weak	__res_mailok	=	res_mailok
#pragma weak	__res_ownok	=	res_ownok
#endif	/* ORIGINAL_ISC_CODE */

/*%
 * Expand compressed domain name 'src' to full domain name.
 *
 * \li 'msg' is a pointer to the begining of the message,
 * \li 'eom' points to the first location after the message,
 * \li 'dst' is a pointer to a buffer of size 'dstsiz' for the result.
 * \li Return size of compressed name or -1 if there was an error.
 */
int
dn_expand(const u_char *msg, const u_char *eom, const u_char *src,
	  char *dst, int dstsiz)
{
	int n = ns_name_uncompress(msg, eom, src, dst, (size_t)dstsiz);

	if (n > 0 && dst[0] == '.')
		dst[0] = '\0';
	return (n);
}

/*%
 * Pack domain name 'exp_dn' in presentation form into 'comp_dn'.
 *
 * \li Return the size of the compressed name or -1.
 * \li 'length' is the size of the array pointed to by 'comp_dn'.
 */
int
dn_comp(const char *src, u_char *dst, int dstsiz,
	u_char **dnptrs, u_char **lastdnptr)
{
	return (ns_name_compress(src, dst, (size_t)dstsiz,
				 (const u_char **)dnptrs,
				 (const u_char **)lastdnptr));
}


/*%
 * Skip over a compressed domain name. Return the size or -1.
 */
int
dn_skipname(const u_char *ptr, const u_char *eom) {
	const u_char *saveptr = ptr;

	if (ns_name_skip(&ptr, eom) == -1)
		return (-1);
	return (ptr - saveptr);
}

/*%
 * Verify that a domain name uses an acceptable character set.
 *
 * Note the conspicuous absence of ctype macros in these definitions.  On
 * non-ASCII hosts, we can't depend on string literals or ctype macros to
 * tell us anything about network-format data.  The rest of the BIND system
 * is not careful about this, but for some reason, we're doing it right here.
 */
#define PERIOD 0x2e
#define	hyphenchar(c) ((c) == 0x2d)
#define bslashchar(c) ((c) == 0x5c)
#ifdef	SUNW_HNOK_UNDERSCORE
#define	underscorechar(c)	((c) == 0x5f)
#endif	/* SUNW_HNOK_UNDERSCORE */
#define periodchar(c) ((c) == PERIOD)
#define asterchar(c) ((c) == 0x2a)
#define alphachar(c) (((c) >= 0x41 && (c) <= 0x5a) \
		   || ((c) >= 0x61 && (c) <= 0x7a))
#define digitchar(c) ((c) >= 0x30 && (c) <= 0x39)

#define borderchar(c) (alphachar(c) || digitchar(c))
#ifdef	SUNW_HNOK_UNDERSCORE
#define middlechar(c) (borderchar(c) || hyphenchar(c) || underscorechar(c))
#else
#define middlechar(c) (borderchar(c) || hyphenchar(c))
#endif	/* SUNW_HNOK_UNDERSCORE */
#define	domainchar(c) ((c) > 0x20 && (c) < 0x7f)

int
res_hnok(const char *dn) {
	int pch = PERIOD, ch = *dn++;

	while (ch != '\0') {
		int nch = *dn++;

		if (periodchar(ch)) {
			(void)NULL;
		} else if (periodchar(pch)) {
			if (!borderchar(ch))
				return (0);
		} else if (periodchar(nch) || nch == '\0') {
			if (!borderchar(ch))
				return (0);
		} else {
			if (!middlechar(ch))
				return (0);
		}
		pch = ch, ch = nch;
	}
	return (1);
}

/*%
 * hostname-like (A, MX, WKS) owners can have "*" as their first label
 * but must otherwise be as a host name.
 */
int
res_ownok(const char *dn) {
	if (asterchar(dn[0])) {
		if (periodchar(dn[1]))
			return (res_hnok(dn+2));
		if (dn[1] == '\0')
			return (1);
	}
	return (res_hnok(dn));
}

/*%
 * SOA RNAMEs and RP RNAMEs can have any printable character in their first
 * label, but the rest of the name has to look like a host name.
 */
int
res_mailok(const char *dn) {
	int ch, escaped = 0;

	/* "." is a valid missing representation */
	if (*dn == '\0')
		return (1);

	/* otherwise <label>.<hostname> */
	while ((ch = *dn++) != '\0') {
		if (!domainchar(ch))
			return (0);
		if (!escaped && periodchar(ch))
			break;
		if (escaped)
			escaped = 0;
		else if (bslashchar(ch))
			escaped = 1;
	}
	if (periodchar(ch))
		return (res_hnok(dn));
	return (0);
}

/*%
 * This function is quite liberal, since RFC1034's character sets are only
 * recommendations.
 */
int
res_dnok(const char *dn) {
	int ch;

	while ((ch = *dn++) != '\0')
		if (!domainchar(ch))
			return (0);
	return (1);
}

#ifdef BIND_4_COMPAT
/*%
 * This module must export the following externally-visible symbols:
 *	___putlong
 *	___putshort
 *	__getlong
 *	__getshort
 * Note that one _ comes from C and the others come from us.
 */

#ifdef SOLARIS2
#ifdef  __putlong
#undef  __putlong
#endif
#ifdef  __putshort
#undef  __putshort
#endif
#pragma weak    putlong         =       __putlong
#pragma weak    putshort        =       __putshort
#endif /* SOLARIS2 */

void __putlong(u_int32_t src, u_char *dst) { ns_put32(src, dst); }
void __putshort(u_int16_t src, u_char *dst) { ns_put16(src, dst); }
#ifndef __ultrix__
u_int32_t _getlong(const u_char *src) { return (ns_get32(src)); }
u_int16_t _getshort(const u_char *src) { return (ns_get16(src)); }
#endif /*__ultrix__*/
#endif /*BIND_4_COMPAT*/

/*! \file */
