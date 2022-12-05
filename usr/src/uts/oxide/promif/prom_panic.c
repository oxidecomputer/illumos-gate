/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2022 Oxide Computer Company
 */

#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/archsystm.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/kernel_ipcc.h>

void
prom_panic(char *s)
{
	const char fmt[] = "%s: prom_panic: %s\n";
#if defined(_KMDB)
	const char *tag = "kmdb";
#elif defined(_KERNEL)
	const char *tag = "kernel";
#else
#error	"configuration error"
#endif

	if (s == NULL)
		s = "unknown panic";

	kipcc_panic_field(IPF_CAUSE, IPCC_PANIC_EARLYBOOT_PROM);
	kipcc_panic_message(fmt, tag, s);
	kernel_ipcc_panic();
	prom_printf(fmt, tag, s);

#if defined(_KERNEL)
	if (boothowto & RB_DEBUG)
		kmdb_enter();
#endif

	prom_reboot(NULL);
}
