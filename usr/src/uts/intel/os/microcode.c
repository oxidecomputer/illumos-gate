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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2012 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2018, Joyent, Inc.
 * Copyright 2021 OmniOS Community Edition (OmniOSce) Association.
 * Copyright 2025 Oxide Computer Company
 */

#include <sys/bootconf.h>
#include <sys/cmn_err.h>
#include <sys/controlregs.h>
#include <sys/utsname.h>
#include <sys/debug.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>
#include <sys/ontrap.h>
#include <sys/stdbool.h>
#include <sys/systeminfo.h>
#include <sys/systm.h>
#include <sys/ucode.h>
#include <sys/x86_archext.h>
#include <sys/x_call.h>

/*
 * mcpu_ucode_info for the boot CPU.  Statically allocated.
 */
static struct cpu_ucode_info cpu_ucode_info0;
static const ucode_source_t *ucode;
static char *ucodepath;
static kmutex_t ucode_lock;
static bool ucode_cleanup_done = false;

/*
 * Flag for use by microcode impls to determine if they can use kmem.  Note this
 * is meant primarily for gating use of functions like kobj_open_file() which
 * allocate internally with kmem.  ucode_zalloc() and ucode_free() should
 * otherwise be used.
 */
bool ucode_use_kmem = false;

static const char ucode_failure_fmt[] =
	"cpu%d: failed to update microcode from version 0x%x to 0x%x";
static const char ucode_success_fmt[] =
	"?cpu%d: microcode has been updated from version 0x%x to 0x%x\n";

static const char ucode_path_fmt[] = "/platform/%s/ucode";

SET_DECLARE(ucode_source_set, ucode_source_t);

/*
 * Force flag.  If set, the first microcode binary that matches
 * signature and platform id will be used for microcode update,
 * regardless of version.  Should only be used for debugging.
 */
int ucode_force_update = 0;

void
ucode_init(void)
{
	ucode_source_t **src;

	mutex_init(&ucode_lock, NULL, MUTEX_DEFAULT, NULL);

	/* Set up function pointers */
	SET_FOREACH(src, ucode_source_set) {
		if ((*src)->us_select(CPU)) {
			ucode = *src;
			break;
		}
	}

	if (ucode == NULL)
		return;

#ifdef DEBUG
	cmn_err(CE_CONT, "?ucode: selected %s\n", ucode->us_name);

	if (!ucode->us_capable(CPU)) {
		cmn_err(CE_CONT,
		    "?ucode: microcode update not supported on CPU\n");
		return;
	}
#endif
}

/*
 * Allocate space for mcpu_ucode_info in the machcpu structure
 * for all non-boot CPUs.
 */
void
ucode_alloc_space(cpu_t *cp)
{
	ASSERT(cp->cpu_id != 0);
	ASSERT(cp->cpu_m.mcpu_ucode_info == NULL);
	cp->cpu_m.mcpu_ucode_info =
	    kmem_zalloc(sizeof (*cp->cpu_m.mcpu_ucode_info), KM_SLEEP);
}

void
ucode_free_space(cpu_t *cp)
{
	ASSERT(cp->cpu_m.mcpu_ucode_info != NULL);
	ASSERT(cp->cpu_m.mcpu_ucode_info != &cpu_ucode_info0);
	kmem_free(cp->cpu_m.mcpu_ucode_info,
	    sizeof (*cp->cpu_m.mcpu_ucode_info));
	cp->cpu_m.mcpu_ucode_info = NULL;
}

const char *
ucode_path(void)
{
	ASSERT(ucodepath != NULL);
	return (ucodepath);
}

/*
 * Allocate/free a buffer used to hold ucode data. Space allocated before kmem
 * is available is allocated with BOP_ALLOC() and does not require a free.
 */
void *
ucode_zalloc(size_t size)
{
	if (ucode_use_kmem)
		return (kmem_zalloc(size, KM_NOSLEEP));

	/* BOP_ALLOC() failure results in panic */
	return (BOP_ALLOC(bootops, NULL, size, MMU_PAGESIZE));
}

void
ucode_free(void *buf, size_t size)
{
	if (ucode_use_kmem && buf != NULL)
		kmem_free(buf, size);
}

/*
 * Called to free up space allocated for the microcode file. This is called
 * from start_other_cpus() after an update attempt has been performed on all
 * CPUs.
 */
void
ucode_cleanup(void)
{
	mutex_enter(&ucode_lock);
	if (ucode != NULL)
		ucode->us_file_reset();
	ucode_cleanup_done = true;
	mutex_exit(&ucode_lock);

	/*
	 * We purposefully do not free 'ucodepath' here so that it persists for
	 * any future callers to ucode_locate(), such as could occur on systems
	 * that support DR.
	 */
}

static int
ucode_write(xc_arg_t arg1, xc_arg_t unused2, xc_arg_t unused3)
{
	ucode_update_t *uusp = (ucode_update_t *)arg1;
	cpu_ucode_info_t *uinfop = CPU->cpu_m.mcpu_ucode_info;
	on_trap_data_t otd;

	ASSERT(ucode != NULL);
	ASSERT(uusp->ucodep != NULL);

	/*
	 * Check one more time to see if it is really necessary to update
	 * microcode just in case this is a hyperthreaded processor where
	 * the threads share the same microcode.
	 */
	if (!ucode_force_update) {
		ucode->us_read_rev(uinfop);
		uusp->new_rev = uinfop->cui_rev;
		if (uinfop->cui_rev >= uusp->expected_rev)
			return (0);
	}

	if (!on_trap(&otd, OT_DATA_ACCESS)) {
		if (ucode->us_invalidate) {
			/*
			 * On some platforms a cache invalidation is required
			 * for the ucode update to be successful due to the
			 * parts of the processor that the microcode is
			 * updating.
			 */
			invalidate_cache();
		}
		wrmsr(ucode->us_write_msr, (uintptr_t)uusp->ucodep);
	}

	no_trap();
	ucode->us_read_rev(uinfop);
	uusp->new_rev = uinfop->cui_rev;

	return (0);
}

/*
 * Entry points to microcode update from the 'ucode' driver.
 */

ucode_errno_t
ucode_validate(uint8_t *ucodep, size_t size)
{
	if (ucode == NULL)
		return (EM_NOTSUP);
	return (ucode->us_validate(ucodep, size));
}

ucode_errno_t
ucode_update(uint8_t *ucodep, size_t size)
{
	bool		found = false;
	ucode_update_t	cached = { 0 };
	ucode_update_t	*cachedp = NULL;
	ucode_errno_t	rc = EM_OK;
	ucode_errno_t	search_rc = EM_NOMATCH; /* search result */
	cpuset_t cpuset;

	ASSERT(ucode != 0);
	ASSERT(ucodep != 0);
	CPUSET_ZERO(cpuset);

	if (!ucode->us_capable(CPU))
		return (EM_NOTSUP);

	mutex_enter(&cpu_lock);

	for (processorid_t id = 0; id < max_ncpus; id++) {
		cpu_t *cpu;
		ucode_update_t uus = { 0 };
		ucode_update_t *uusp = &uus;

		/*
		 * If there is no such CPU or it is not xcall ready, skip it.
		 */
		if ((cpu = cpu_get(id)) == NULL ||
		    !(cpu->cpu_flags & CPU_READY)) {
			continue;
		}

		uusp->sig = cpuid_getsig(cpu);
		bcopy(cpu->cpu_m.mcpu_ucode_info, &uusp->info,
		    sizeof (uusp->info));

		/*
		 * If the current CPU has the same signature and platform
		 * id as the previous one we processed, reuse the information.
		 */
		if (cachedp && cachedp->sig == cpuid_getsig(cpu) &&
		    cachedp->info.cui_platid == uusp->info.cui_platid) {
			uusp->ucodep = cachedp->ucodep;
			uusp->expected_rev = cachedp->expected_rev;
			/*
			 * Intuitively we should check here to see whether the
			 * running microcode rev is >= the expected rev, and
			 * quit if it is.  But we choose to proceed with the
			 * xcall regardless of the running version so that
			 * the other threads in an HT processor can update
			 * the cpu_ucode_info structure in machcpu.
			 */
		} else if ((search_rc = ucode->us_extract(uusp, ucodep, size))
		    == EM_OK) {
			bcopy(uusp, &cached, sizeof (cached));
			cachedp = &cached;
			found = true;
		}

		/* Nothing to do */
		if (uusp->ucodep == NULL)
			continue;

		CPUSET_ADD(cpuset, id);
		kpreempt_disable();
		xc_sync((xc_arg_t)uusp, 0, 0, CPUSET2BV(cpuset), ucode_write);
		kpreempt_enable();
		CPUSET_DEL(cpuset, id);

		if (uusp->new_rev != 0 && uusp->info.cui_rev == uusp->new_rev &&
		    !ucode_force_update) {
			rc = EM_HIGHERREV;
		} else if ((uusp->new_rev == 0) || (uusp->expected_rev != 0 &&
		    uusp->expected_rev != uusp->new_rev)) {
			cmn_err(CE_WARN, ucode_failure_fmt,
			    id, uusp->info.cui_rev, uusp->expected_rev);
			rc = EM_UPDATE;
		} else {
			cmn_err(CE_CONT, ucode_success_fmt,
			    id, uusp->info.cui_rev, uusp->new_rev);
		}
	}

	mutex_exit(&cpu_lock);

	if (!found) {
		rc = search_rc;
	} else if (rc == EM_OK) {
		cpuid_post_ucodeadm();
	}

	return (rc);
}

/*
 * Called when starting up non-boot CPUs from mp_startup() to read the current
 * microcode revision before the control CPU calls ucode_locate().
 */
void
ucode_read_rev(cpu_t *cp)
{
	cpu_ucode_info_t *uinfop;

	ASSERT3P(cp, !=, NULL);

	if (ucode == NULL || !ucode->us_capable(cp))
		return;

	uinfop = cp->cpu_m.mcpu_ucode_info;
	ASSERT3P(uinfop, !=, NULL);

	ucode->us_read_rev(uinfop);
}

/*
 * Called by the control CPU when starting up non-boot CPUs to find any
 * applicable microcode updates. Initializes mcpu_ucode_info, which will contain
 * the relevant update to be applied, via ucode_apply(), if one is found.
 * ucode_read_rev() must be called before this function on the target CPU.
 */
void
ucode_locate(cpu_t *cp)
{
	cpu_ucode_info_t *uinfop;
	ucode_errno_t rc;
	size_t sz;

	ASSERT3P(cp, !=, NULL);
	ASSERT(ucode_use_kmem);

	mutex_enter(&ucode_lock);

	if (ucode == NULL || !ucode->us_capable(cp))
		goto out;

	if (ucodepath == NULL) {
		sz = snprintf(NULL, 0, ucode_path_fmt, platform) + 1;
		ucodepath = kmem_zalloc(sz, KM_NOSLEEP);
		if (ucodepath == NULL) {
			cmn_err(CE_WARN,
			    "ucode: could not allocate memory for path");
			goto out;
		}
		(void) snprintf(ucodepath, sz, ucode_path_fmt, platform);
	}

	uinfop = cp->cpu_m.mcpu_ucode_info;
	ASSERT3P(uinfop, !=, NULL);

	/*
	 * Search for any applicable updates.
	 *
	 * A return value of EM_HIGHERREV indicates that no update was applied
	 * due to the CPU already being at that or a higher revision, but both
	 * EM_HIGHERREV and EM_OK indicate that some microcode that matches the
	 * CPU was successfully located. In either of these cases it's worth
	 * keeping it around in case it's useful for the next CPU -- and if it
	 * isn't it will end up being discarded. In all other cases we clear it
	 * out just in case we have read in a partial or invalid file.
	 *
	 * Architectural note:
	 *   Depending on the platform, the cpu_t being processed may represent
	 *   a thread within a CPU core. If updating one thread's microcode
	 *   implicitly updates all sibling threads in the core, it's normal to
	 *   see a mix of EM_OK and EM_HIGHERREV when iterating over those
	 *   threads.
	 *
	 * There's one additional consideration. If we are here after
	 * ucode_cleanup() has been called, such as could occur with CPU
	 * hotplug, we also clear the memory and reset the data structure as
	 * nothing else will call ucode_cleanup() and we don't need to cache
	 * the data as we do during boot when starting the APs.
	 */
	rc = ucode->us_locate(cp, uinfop);
	if ((rc != EM_OK && rc != EM_HIGHERREV) || ucode_cleanup_done)
		ucode->us_file_reset();

out:
	mutex_exit(&ucode_lock);
}

/*
 * Called when starting up non-boot CPUs to load any pending microcode updates
 * found in ucode_locate().  Note this is called very early in the startup
 * process (before CPU_READY is set and while CPU_QUIESCED is) so we must be
 * careful about what we do here, e.g., no kmem_free or anything that might call
 * hat_unload; no kmem_alloc or anything which may cause thread context switch.
 * We also don't take the ucode_lock here for similar reasons (if contended
 * the idle thread will spin with CPU_QUIESCED set). This is fine though since
 * we should not be updating any shared ucode state.
 */
void
ucode_apply(cpu_t *cp)
{
	cpu_ucode_info_t *uinfop;

	ASSERT3P(cp, !=, NULL);

	if (ucode == NULL || !ucode->us_capable(cp))
		return;

	uinfop = cp->cpu_m.mcpu_ucode_info;
	ASSERT3P(uinfop, !=, NULL);

	/*
	 * No pending update -- nothing to do.
	 */
	if (uinfop->cui_pending_ucode == NULL)
		return;

	/*
	 * Apply pending update.
	 */
	ucode->us_load(uinfop);
}

/*
 * Called when starting up non-boot CPUs to free any pending microcode updates
 * found in ucode_locate() and print the result of the attempting to load it in
 * ucode_apply().  This is separate from ucode_apply() as we can't yet call
 * kmem_free() at that point in the startup process.
 */
void
ucode_finish(cpu_t *cp)
{
	cpu_ucode_info_t *uinfop;
	uint32_t old_rev, new_rev;

	ASSERT3P(cp, !=, NULL);

	if (ucode == NULL || !ucode->us_capable(cp))
		return;

	uinfop = cp->cpu_m.mcpu_ucode_info;
	ASSERT3P(uinfop, !=, NULL);

	/*
	 * No pending update -- nothing to do.
	 */
	if (uinfop->cui_pending_ucode == NULL)
		return;

	old_rev = uinfop->cui_rev;
	new_rev = uinfop->cui_pending_rev;
	ucode->us_read_rev(uinfop);

	if (uinfop->cui_rev != new_rev) {
		ASSERT3U(uinfop->cui_rev, ==, old_rev);
		cmn_err(CE_WARN, ucode_failure_fmt, cp->cpu_id, old_rev,
		    new_rev);
	} else {
		cmn_err(CE_CONT, ucode_success_fmt, cp->cpu_id, old_rev,
		    new_rev);
	}

	ucode_free(uinfop->cui_pending_ucode, uinfop->cui_pending_size);
	uinfop->cui_pending_ucode = NULL;
	uinfop->cui_pending_size = 0;
	uinfop->cui_pending_rev = 0;
}

/*
 * Entry point to microcode update from mlsetup() for boot CPU.
 * Initialize mcpu_ucode_info, and perform microcode update if necessary.
 * cpuid_info must be initialized before we can be called.
 */
void
ucode_check_boot(void)
{
	cpu_t *cp = CPU;
	cpu_ucode_info_t *uinfop;
	const char *prop;
	char *plat;
	int prop_len;
	size_t path_len;

	ASSERT3U(cp->cpu_id, ==, 0);
	ASSERT(!ucode_use_kmem);

	mutex_enter(&ucode_lock);

	/* Space statically allocated for BSP; ensure pointer is set */
	ASSERT3P(cp->cpu_m.mcpu_ucode_info, ==, NULL);
	uinfop = cp->cpu_m.mcpu_ucode_info = &cpu_ucode_info0;

	if (ucode == NULL || !ucode->us_capable(cp))
		goto out;

	ASSERT3P(ucodepath, ==, NULL);

	prop = "impl-arch-name";
	prop_len = BOP_GETPROPLEN(bootops, prop);
	if (prop_len <= 0) {
		cmn_err(CE_WARN, "ucode: could not find %s property", prop);
		goto out;
	}

	/*
	 * We're running on the boot CPU before kmem is available so we make use
	 * of BOP_ALLOC() -- which panics on failure -- to allocate any memory
	 * we need.  That also means we don't need to explicity free it.
	 */
	plat = BOP_ALLOC(bootops, NULL, prop_len + 1, MMU_PAGESIZE);
	(void) BOP_GETPROP(bootops, prop, plat);
	if (plat[0] == '\0') {
		/*
		 * If we can't determine the architecture name,
		 * we cannot find microcode files for it.
		 * Return without setting 'ucodepath'.
		 */
		cmn_err(CE_WARN, "ucode: could not determine arch");
		goto out;
	}

	path_len = snprintf(NULL, 0, ucode_path_fmt, plat) + 1;
	ucodepath = BOP_ALLOC(bootops, NULL, path_len, MMU_PAGESIZE);
	(void) snprintf(ucodepath, path_len, ucode_path_fmt, plat);

	/*
	 * Check to see if we need ucode update
	 */
	ucode->us_read_rev(uinfop);
	if (ucode->us_locate(cp, uinfop) == EM_OK) {
		uint32_t old_rev, new_rev;

		old_rev = uinfop->cui_rev;
		new_rev = uinfop->cui_pending_rev;
		ucode->us_load(uinfop);
		ucode->us_read_rev(uinfop);

		if (uinfop->cui_rev != new_rev) {
			ASSERT3U(uinfop->cui_rev, ==, old_rev);
			cmn_err(CE_WARN, ucode_failure_fmt, cp->cpu_id,
			    old_rev, new_rev);
		} else {
			cmn_err(CE_CONT, ucode_success_fmt, cp->cpu_id,
			    old_rev, new_rev);
		}
	}

	/*
	 * Regardless of whether we found a match or not, since the scratch
	 * memory for holding the microcode for the boot CPU came from
	 * BOP_ALLOC, we will reset the data structure as if we never did the
	 * allocation so we don't have to keep track of this special chunk of
	 * memory.
	 */
	ucode->us_file_reset();

	/*
	 * Similarly clear any pending update that may have been found.
	 */
	uinfop->cui_pending_ucode = NULL;
	uinfop->cui_pending_size = 0;
	uinfop->cui_pending_rev = 0;

out:
	/*
	 * Discard the memory that came from BOP_ALLOC and was used to build the
	 * ucode path.  Subsequent CPUs will be handled via ucode_locate() at
	 * which point kmem is available and we can cache the path.
	 */
	ucodepath = NULL;
	ucode_use_kmem = true;

	mutex_exit(&ucode_lock);
}

/*
 * Returns microcode revision from the machcpu structure.
 */
ucode_errno_t
ucode_get_rev(uint32_t *revp)
{
	int i;

	ASSERT(revp != NULL);

	if (ucode == NULL || !ucode->us_capable(CPU))
		return (EM_NOTSUP);

	mutex_enter(&cpu_lock);
	for (i = 0; i < max_ncpus; i++) {
		cpu_t *cpu;

		if ((cpu = cpu_get(i)) == NULL)
			continue;

		revp[i] = cpu->cpu_m.mcpu_ucode_info->cui_rev;
	}
	mutex_exit(&cpu_lock);

	return (EM_OK);
}
