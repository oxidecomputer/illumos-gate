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

#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/va_list.h>

#ifdef DEBUG
void
oxide_boot_debug_impl(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vcmn_err(CE_CONT, fmt, va);
	va_end(va);
}
#endif

void
oxide_boot_vwarn(const char *fmt, va_list va)
{
	vcmn_err(CE_WARN, fmt, va);
}

void
oxide_boot_warn(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	oxide_boot_vwarn(fmt, va);
	va_end(va);
}

void
oxide_boot_vnote(const char *fmt, va_list va)
{
	vcmn_err(CE_NOTE, fmt, va);
}

void
oxide_boot_note(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	oxide_boot_vnote(fmt, va);
	va_end(va);
}
