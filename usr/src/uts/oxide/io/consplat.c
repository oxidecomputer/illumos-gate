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
 * Copyright (c) 2012 Gary Mills
 * Copyright 2022 Oxide Computer Co.
 *
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/promif.h>
#include <sys/modctl.h>
#include <sys/cpuvar.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/fch.h>

int
plat_use_polled_debug()
{
	return (0);
}

int
plat_support_serial_kbd_and_ms()
{
	return (0);
}

int
plat_stdin_is_keyboard(void)
{
	return (0);
}

int
plat_stdout_is_framebuffer(void)
{
	return (0);
}

char *
plat_fbpath(void)
{
	return (NULL);
}

char *
plat_mousepath(void)
{
	return (NULL);
}

char *
plat_kbdpath(void)
{
	return (NULL);
}

static char *
plat_conspath(void)
{
	switch (chiprev_fch_kind(cpuid_getchiprev(CPU))) {
	case FK_HUASHAN:
		return ("/huashan@0,0/dwu@0:0");
	case FK_SONGSHAN:
		return ("/songshan@0,0/dwu@0:0");
	case FK_NONE:
	default:
		panic("plat_conspath: unsupported FCH type");
	}
}

char *
plat_stdinpath(void)
{
	return (plat_conspath());
}

char *
plat_stdoutpath(void)
{
	return (plat_conspath());
}

char *
plat_diagpath(void)
{
	return (plat_conspath());
}
