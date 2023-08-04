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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef _SYS_BOOT_DEBUG_H
#define	_SYS_BOOT_DEBUG_H

/*
 * Common macros for printf debugging during early boot phases.  These macros
 * are available prior to prom_printf() and should be used only by machine-
 * specific code.  Some output is gated on variables contained in unix; these
 * may be set using kmdb, /etc/system (defined statically at build time and not
 * generally useful on this architecture), by the SP via boot policy options, or
 * by changing their initialisers in code.
 *
 * We provide two different families of macros for earlyboot debugging, one
 * intended only for kernel boot memory (kbm) debugging and one for more general
 * use (eb).  Each comes with three standard macros: XXX_DBG, XXX_DBGMSG, and
 * xxx_debug_printf.  All of these are gated on either kbm_debug (kbm) or
 * prom_debug (eb).  The DBG and DBGMSG macros automatically print the filename
 * and line number at the call site; the last does not.  In addition to all of
 * these macros, we also provide eb_printf and eb_vprintf that are general
 * printf type functions that cause spew unconditionally (when possible; see
 * discussion below).  These are equivalent to bop_printf and vbop_printf in
 * behaviour and are conveniences as they elide the (unused and often difficult
 * to obtain) first argument.
 *
 * There are, more or less, four stages of boot with respect to available spew
 * functionality; here, "spew" means free-form text output, normally in the
 * English or American languages in production code, directed to a UART and
 * intended only for human engineers to read during bringup or debugging of boot
 * code:
 *
 * * 1A: Code that runs before boot_console_init() has returned.  There is no
 * normal way to obtain spew at this substage; instead, one must modify the code
 * in os/boot_console.c to reuse the loader's identity mapping for the UART.
 * See VVE_CONSOLE_DEBUG in that file.  Enabling this feature will cause calls
 * to all of our debugging macros to work before the console device is mapped.
 * Note that at this point in boot, there has been no opportunity to set
 * kbm_debug or prom_debug, so if you need conditional spew in phase 1A you must
 * also manually modify the respective initialiser.
 *
 * * 1B: Code that runs after boot_console_init() has returned but before the
 * code in boot_data.c has had a chance to obtain and set boot flags from the
 * SP.  During this time, without any code changes, bop_printf, vbop_printf,
 * eb_printf, and eb_vprintf are available.  The conditional spew macros are
 * also available but will not produce output unless you have manually modified
 * the appropriate initialiser.  It's not necessary to set VVE_CONSOLE_DEBUG to
 * get spew in phase 1B or later.
 *
 * * 1C: Code that runs after IPCC initialisation but before kobj_init() links
 * in genunix.  At this point, the values of kbm_debug, bootrd_debug, and
 * prom_debug will be set according to SP policy, as will the RB_VERBOSE flag in
 * common code, and all the spew functions and macros in this header are
 * available.  While the prom_debug variable is available and has been set
 * according to policy, the functions and macros in prom_debug.h are not yet
 * available and must not be called.  Doing so will result in an earlyboot trap
 * and boot failure.
 *
 * Summary guidance for spewing in stage 1:
 *
 * - Code that manipulates memory mappings or allocations should use the kbm
 *   debugging macros.  This spew will be gated on the value of kbm_debug.
 * - Other code that wishes to spew conditionally should use the eb debugging
 *   macros.  This spew will be gated on the value of prom_debug.
 * - Code that needs to spew unconditionally may use eb_{,v}printf.  This is
 *   strongly discouraged in shipping code but sometimes very useful during
 *   development.
 * - In substage 1C and later, conditional spew will not be visible unless you
 *   have caused the SP to set STARTUP_KBM and/or STARTUP_PROM.
 * - In substages 1A and 1B, conditional spew will not be visible unless you
 *   have modified the initialiser for kbm_debug and/or prom_debug.
 * - In substage 1A, no spew will be visible unless you have set
 *   VVE_CONSOLE_DEBUG.
 * - The functions that can be used to spew in stage 1 are not MT-Safe.  Do not
 *   use them in code that may be called from any thread other than the
 *   primordial one; kernel state will not be harmed but output is likely to be
 *   interleaved and/or incorrect.  Use of these functions after a real driver
 *   has been bound to the console device will interfere with correct operation
 *   of that driver and may lead to data corruption or panic.
 *
 * * 2: Code that runs once genunix has been linked by kobj_init(), which is
 * everything in mlsetup() and everything in startup() up to the call to
 * kmem_init().  Code that never runs before this stage may use prom_printf and
 * its macros PRM_DEBUG, PRM_DEBUGS, and PRM_POINT.  The macros gate spew on the
 * value of prom_debug.  See sys/prom_debug.h.  In addition, it is possible to
 * use the cmn_err family of functions, but at this stage some of its
 * functionality will not work; notably, the ? prefix is ineffective and will
 * result in unconditional spew.  The prom_printf and related functions are not
 * MT-Safe and have the same caveats as the stage 1 spew functions.
 *
 * Guidance for spewing in stage 2:
 *
 * - Use cmn_err or related functions to spew unconditionally.
 * - For conditional spew that should be gated on prom_debug, the PRM_XX macros
 *   may be used if the code in question never runs at later stages.
 * - For conditional spew that should be gated on kbm_debug, the stage 1
 *   mechanisms may be used as above as long as the code that needs to spew is
 *   gated off by hat_init() and/or bop_no_more_mem(), called from startup_vm().
 * - Conditional spew gated on RB_VERBOSE must be gated explicitly.
 *
 * * 3: Code that runs after kmem_init() (which calls log_init()) has returned
 * but before the console device driver has been loaded and initialised.  At
 * this point, the cmn_err family of functions all work properly; however, they
 * will internally -- and in an MT-Safe manner -- use prom_vprintf when they
 * need to spew to the console.  Code that always runs at stage 3 or later
 * should always use the documented cmn_err and related functions to spew.
 *
 * Guidance for spewing in stage 3:
 *
 * - Use cmn_err and related except for code that also runs at earlier stages.
 * - Code that can also run in stages 1 and 2 requires extreme caution.  Limit
 *   use of the kbm family of macros to code that is deactivated by startup_vm()
 *   (specifically hat_init() and/or bop_no_more_mem()) to enforce this.  Use
 *   extreme caution with the eb family of spew routines to ensure their use is
 *   limited to earlier stages of boot.  Code running both in stage 1 and later
 *   is likely to be limited to IPCC.
 *
 * * 4: Code that runs after the console device has been set up by consconfig(),
 * which also binds a device driver to the UART (or other device(s)) used for
 * the console if needed.  Code that runs only in stage 4 must never use any
 * mechanism to spew other than the documented cmn_err and related functions.
 * Note that there are a few exceptions in the halt/reboot paths where the
 * sys/prom_debug.h functions may be needed; see comments in e.g. machdep.c.
 */

#include <sys/types.h>
#include <sys/bootconf.h>

#ifdef __cplusplus
extern "C" {
#endif

extern boolean_t kbm_debug;
extern int prom_debug;
extern void eb_debug_printf_gated(boolean_t, const char *, int,
    const char *, ...) __KPRINTFLIKE(4);

#define	KBM_DBGMSG(_fmt, ...)		\
	eb_debug_printf_gated(kbm_debug, __FILE__, __LINE__, \
	    _fmt, ##__VA_ARGS__)

#define	KBM_DBG(_var)	\
	eb_debug_printf_gated(kbm_debug, __FILE__, __LINE__, \
	    "%s is %" PRIx64 "\n", #_var, ((uint64_t)(_var)))

#define	kbm_debug_printf(_fmt, ...)	\
	eb_debug_printf_gated(kbm_debug, NULL, 0, _fmt, ##__VA_ARGS__)

#define	EB_DBGMSG(_fmt, ...)		\
	eb_debug_printf_gated(prom_debug != 0, __FILE__, __LINE__, \
	    _fmt, ##__VA_ARGS__)

#define	EB_DBG(_var)	\
	eb_debug_printf_gated(prom_debug != 0, __FILE__, __LINE__, _fmt, \
	    "%s is %" PRIx64 "\n", #_var, ((uint64_t)(_var)))

#define	eb_debug_printf(_fmt, ...)	\
	eb_debug_printf_gated(prom_debug != 0, NULL, 0, _fmt, ##__VA_ARGS__)

#define	eb_printf(_fmt, ...)		\
	bop_printf(NULL, _fmt, ##__VA_ARGS__)

#define	eb_vprintf(_fmt, _ap)		\
	vbop_printf(NULL, _fmt, _ap)

extern void eb_halt(void) __NORETURN;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_BOOT_DEBUG_H */
