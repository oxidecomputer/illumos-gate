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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright (c) 2018, Joyent, Inc.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

/*
 * Copyright (c) 2018, Joyent, Inc.
 * Copyright 2020 RackTop Systems, Inc.
 */

/*
 * Generic x86 CPU Module
 *
 * This CPU module is used for generic x86 CPUs when Solaris has no other
 * CPU-specific support module available.  Code in this module should be the
 * absolute bare-bones support and must be cognizant of both Intel and AMD etc.
 */

#include <sys/types.h>
#include <sys/cpu_module_impl.h>
#include <sys/cpuvar.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/pghw.h>
#include <sys/x86_archext.h>

#include "gcpu.h"

/*
 * Prevent generic cpu support from loading.
 */
int gcpu_disable = 0;

#define	GCPU_MAX_CHIPID		32
static struct gcpu_chipshared *gcpu_shared[GCPU_MAX_CHIPID];
#ifdef	DEBUG
int gcpu_id_disable = 0;
static const char *gcpu_id_override[GCPU_MAX_CHIPID] = { NULL };
#endif

/*
 * The purpose of this is to construct a unique identifier for a given processor
 * that can be used by things like FMA to determine when a FRU has been
 * replaced. It is supported on Intel Xeon Platforms since Ivy Bridge and AMD
 * 17h processors since Rome. See cpuid_pass1_ppin() for how we determine if a
 * CPU is supported.
 *
 * The protected processor inventory number (PPIN) can be used to create a
 * unique identifier when combined with the processor's cpuid signature. We
 * create a versioned, synthetic ID using the following scheme for the
 * identifier: iv0-<vendor>-<signature>-<PPIN>. The iv0 is the illumos version
 * zero of the ID. If we have a new scheme for a new generation of processors,
 * then that should rev the version field, otherwise for a given processor, this
 * synthetic ID should not change.
 *
 * We use the string "INTC" for Intel and "AMD" for AMD. None of these or the
 * formatting of the values can change without changing the version string.
 */
static char *
gcpu_init_ident_ppin(cmi_hdl_t hdl)
{
	uint_t ppin_ctl_msr, ppin_msr;
	uint64_t value;
	const char *vendor;

	/*
	 * This list should be extended as new Intel Xeon family processors come
	 * out.
	 */
	switch (cmi_hdl_vendor(hdl)) {
	case X86_VENDOR_Intel:
		ppin_ctl_msr = MSR_PPIN_CTL_INTC;
		ppin_msr = MSR_PPIN_INTC;
		vendor = "INTC";
		break;
	case X86_VENDOR_AMD:
		ppin_ctl_msr = MSR_PPIN_CTL_AMD;
		ppin_msr = MSR_PPIN_AMD;
		vendor = "AMD";
		break;
	default:
		return (NULL);
	}

	if (cmi_hdl_rdmsr(hdl, ppin_ctl_msr, &value) != CMI_SUCCESS) {
		return (NULL);
	}

	/*
	 * If the PPIN is not enabled and not locked, attempt to enable it.
	 * Note: in some environments such as Amazon EC2 the PPIN appears
	 * to be disabled and unlocked but our attempts to enable it don't
	 * stick, and when we attempt to read the PPIN we get an uncaught
	 * #GP. To avoid that happening we read the MSR back and verify it
	 * has taken the new value.
	 */
	if ((value & MSR_PPIN_CTL_ENABLED) == 0) {
		if ((value & MSR_PPIN_CTL_LOCKED) != 0) {
			return (NULL);
		}

		if (cmi_hdl_wrmsr(hdl, ppin_ctl_msr, MSR_PPIN_CTL_ENABLED) !=
		    CMI_SUCCESS) {
			return (NULL);
		}

		if (cmi_hdl_rdmsr(hdl, ppin_ctl_msr, &value) != CMI_SUCCESS) {
			return (NULL);
		}

		if ((value & MSR_PPIN_CTL_ENABLED) == 0) {
			return (NULL);
		}
	}

	if (cmi_hdl_rdmsr(hdl, ppin_msr, &value) != CMI_SUCCESS) {
		return (NULL);
	}

	/*
	 * Now that we've read data, lock the PPIN. Don't worry about success or
	 * failure of this part, as we will have gotten everything that we need.
	 * It is possible that it locked open, for example.
	 */
	if (cmi_hdl_wrmsr(hdl, ppin_ctl_msr, MSR_PPIN_CTL_DISABLED) ==
	    CMI_SUCCESS) {
		(void) cmi_hdl_wrmsr(hdl, ppin_ctl_msr, MSR_PPIN_CTL_LOCKED);
	}

	return (kmem_asprintf("iv0-%s-%x-%llx", vendor, cmi_hdl_chipsig(hdl),
	    value));
}

static void
gcpu_init_ident(cmi_hdl_t hdl, struct gcpu_chipshared *sp)
{
#ifdef	DEBUG
	uint_t chipid;

	/*
	 * On debug, allow a developer to override the string to more
	 * easily test CPU autoreplace without needing to physically
	 * replace a CPU.
	 */
	if (gcpu_id_disable != 0) {
		return;
	}

	chipid = cmi_hdl_chipid(hdl);
	if (gcpu_id_override[chipid] != NULL) {
		sp->gcpus_ident = strdup(gcpu_id_override[chipid]);
		return;
	}
#endif

	if (is_x86_feature(x86_featureset, X86FSET_PPIN)) {
		sp->gcpus_ident = gcpu_init_ident_ppin(hdl);
	}
}

/*
 * Our cmi_init entry point, called during startup of each cpu instance.
 */
int
gcpu_init(cmi_hdl_t hdl, void **datap)
{
	uint_t chipid = cmi_hdl_chipid(hdl);
	struct gcpu_chipshared *sp, *osp;
	gcpu_data_t *gcpu;

	if (gcpu_disable || chipid >= GCPU_MAX_CHIPID)
		return (ENOTSUP);

	/*
	 * Allocate the state structure for this cpu.  We will only
	 * allocate the bank logout areas in gcpu_mca_init once we
	 * know how many banks there are.
	 */
	gcpu = *datap = kmem_zalloc(sizeof (gcpu_data_t), KM_SLEEP);
	cmi_hdl_hold(hdl);	/* release in gcpu_fini */
	gcpu->gcpu_hdl = hdl;

	/*
	 * Allocate a chipshared structure if no sibling cpu has already
	 * allocated it, but allow for the fact that a sibling core may
	 * be starting up in parallel.
	 */
	if ((sp = gcpu_shared[chipid]) == NULL) {
		sp = kmem_zalloc(sizeof (struct gcpu_chipshared), KM_SLEEP);
		mutex_init(&sp->gcpus_poll_lock, NULL, MUTEX_DRIVER, NULL);
		mutex_init(&sp->gcpus_cfglock, NULL, MUTEX_DRIVER, NULL);
		osp = atomic_cas_ptr(&gcpu_shared[chipid], NULL, sp);
		if (osp != NULL) {
			mutex_destroy(&sp->gcpus_cfglock);
			mutex_destroy(&sp->gcpus_poll_lock);
			kmem_free(sp, sizeof (struct gcpu_chipshared));
			sp = osp;
		} else {
			gcpu_init_ident(hdl, sp);
		}
	}

	atomic_inc_32(&sp->gcpus_actv_cnt);
	gcpu->gcpu_shared = sp;

	return (0);
}

/*
 * deconfigure gcpu_init()
 */
void
gcpu_fini(cmi_hdl_t hdl)
{
	uint_t chipid = cmi_hdl_chipid(hdl);
	gcpu_data_t *gcpu = cmi_hdl_getcmidata(hdl);
	struct gcpu_chipshared *sp;

	if (gcpu_disable || chipid >= GCPU_MAX_CHIPID)
		return;

	gcpu_mca_fini(hdl);

	/*
	 * Keep shared data in cache for reuse.
	 */
	sp = gcpu_shared[chipid];
	ASSERT(sp != NULL);
	atomic_dec_32(&sp->gcpus_actv_cnt);

	if (gcpu != NULL)
		kmem_free(gcpu, sizeof (gcpu_data_t));

	/* Release reference count held in gcpu_init(). */
	cmi_hdl_rele(hdl);
}

void
gcpu_post_startup(cmi_hdl_t hdl)
{
	gcpu_data_t *gcpu = cmi_hdl_getcmidata(hdl);

	if (gcpu_disable)
		return;

	if (gcpu != NULL)
		cms_post_startup(hdl);

	/*
	 * The boot CPU has a bit of a chicken and egg problem for CMCI. Its MCA
	 * initialization is run before we have initialized the PSM module that
	 * we would use for enabling CMCI. Therefore, we use this as a chance to
	 * enable CMCI for the boot CPU. For all other CPUs, this chicken and
	 * egg problem will have already been solved.
	 */
	gcpu_mca_cmci_enable(hdl);
}

void
gcpu_post_mpstartup(cmi_hdl_t hdl)
{
	if (gcpu_disable)
		return;

	cms_post_mpstartup(hdl);

	/*
	 * All cpu handles are initialized only once all cpus are started, so we
	 * can begin polling post mp startup.
	 */
	gcpu_mca_poll_start(hdl);
}

const char *
gcpu_ident(cmi_hdl_t hdl)
{
	uint_t chipid;
	struct gcpu_chipshared *sp;

	if (gcpu_disable)
		return (NULL);

	chipid = cmi_hdl_chipid(hdl);
	if (chipid >= GCPU_MAX_CHIPID)
		return (NULL);

	if (cmi_hdl_getcmidata(hdl) == NULL)
		return (NULL);

	sp = gcpu_shared[cmi_hdl_chipid(hdl)];
	return (sp->gcpus_ident);
}

#define	GCPU_OP(ntvop, xpvop)	ntvop

cmi_api_ver_t _cmi_api_version = CMI_API_VERSION_3;

const cmi_ops_t _cmi_ops = {
	gcpu_init,				/* cmi_init */
	gcpu_post_startup,			/* cmi_post_startup */
	gcpu_post_mpstartup,			/* cmi_post_mpstartup */
	gcpu_faulted_enter,			/* cmi_faulted_enter */
	gcpu_faulted_exit,			/* cmi_faulted_exit */
	gcpu_mca_init,				/* cmi_mca_init */
	GCPU_OP(gcpu_mca_trap, NULL),		/* cmi_mca_trap */
	GCPU_OP(gcpu_cmci_trap, NULL),		/* cmi_cmci_trap */
	gcpu_msrinject,				/* cmi_msrinject */
	GCPU_OP(gcpu_hdl_poke, NULL),		/* cmi_hdl_poke */
	gcpu_fini,				/* cmi_fini */
	GCPU_OP(NULL, gcpu_xpv_panic_callback),	/* cmi_panic_callback */
	gcpu_ident				/* cmi_ident */
};

static struct modlcpu modlcpu = {
	&mod_cpuops,
	"Generic x86 CPU Module"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modlcpu,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}
