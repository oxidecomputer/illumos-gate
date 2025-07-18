
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

#include <sys/ktest.h>
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/comm_page.h>

static void
comm_page_vars_test(ktest_ctx_hdl_t *ctx)
{
	modctl_t *hdl = NULL;

	if ((hdl = mod_hold_by_name("unix")) == NULL) {
		KT_ERROR(ctx, "failed to hold 'unix' module");
		return;
	}

	const uintptr_t base = kobj_lookup(hdl->mod_mp, "comm_page");
	if (base == 0) {
		KT_ERROR(ctx, "failed to locate 'comm_page' symbol");
		goto cleanup;
	}

	/*
	 * Check field offsets in comm page, ensuring they match up with the
	 * offsets of the variables they represent.
	 */
	typedef struct var_check {
		const char	*name;
		uintptr_t	offset;
	} var_check_t;
	const var_check_t var_checks[] = {
		{
			.name = "tsc_last",
			.offset = offsetof(comm_page_t, cp_tsc_last),
		},
		{
			.name = "tsc_hrtime_base",
			.offset = offsetof(comm_page_t, cp_tsc_hrtime_base),
		},
		{
			.name = "tsc_resume_cap",
			.offset = offsetof(comm_page_t, cp_tsc_resume_cap),
		},
		{
			.name = "tsc_type",
			.offset = offsetof(comm_page_t, cp_tsc_type),
		},
		{
			.name = "tsc_max_delta",
			.offset = offsetof(comm_page_t, cp_tsc_max_delta),
		},
		{
			.name = "hres_lock",
			.offset = offsetof(comm_page_t, cp_hres_lock),
		},
		{
			.name = "nsec_scale",
			.offset = offsetof(comm_page_t, cp_nsec_scale),
		},
		{
			.name = "hrestime_adj",
			.offset = offsetof(comm_page_t, cp_hrestime_adj),
		},
		{
			.name = "hres_last_tick",
			.offset = offsetof(comm_page_t, cp_hres_last_tick),
		},
		{
			.name = "tsc_ncpu",
			.offset = offsetof(comm_page_t, cp_tsc_ncpu),
		},
		{
			.name = "hrestime",
			.offset = offsetof(comm_page_t, cp_hrestime),
		},
		{
			.name = "tsc_sync_tick_delta",
			.offset = offsetof(comm_page_t, cp_tsc_sync_tick_delta),
		},
	};
	for (uint_t i = 0; i < ARRAY_SIZE(var_checks); i++) {
		const var_check_t *var = &var_checks[i];

		const uintptr_t addr = kobj_lookup(hdl->mod_mp, var->name);
		if (addr == 0) {
			KT_ERROR(ctx, "failed to locate '%s' symbol",
			    var->name);
			goto cleanup;
		}
		const uintptr_t var_off = (addr - base);
		if (var_off != var->offset) {
			KT_FAIL(ctx,
			    "unexpected offset for symbol '%s': %lu != %lu",
			    var->name, var_off, var->offset);
			goto cleanup;
		}
	}

	/*
	 * Check that if cp_tsc_ncpu is non-zero, that a tsc_tick_delta-aware
	 * gethrtime has been selected.
	 */
	const comm_page_t *cp = (const comm_page_t *)base;
	if (cp->cp_tsc_ncpu != 0) {
		const uintptr_t *ghrt_func =
		    (const uintptr_t *)kobj_lookup(hdl->mod_mp, "gethrtimef");
		if (ghrt_func == NULL) {
			KT_ERROR(ctx, "failed to locate 'gethrtimef' symbol");
			goto cleanup;
		}
		const uintptr_t ghrt_delta =
		    kobj_lookup(hdl->mod_mp, "tsc_gethrtime_delta");
		if (*ghrt_func != ghrt_delta) {
			KT_FAIL(ctx,
			    "tsc_gethrtime_delta not used for gethrtimef: "
			    "%x != %x\n",
			    ghrt_delta, *ghrt_func);
			goto cleanup;
		}
	}

	KT_PASS(ctx);

cleanup:
	mod_release_mod(hdl);
}

static void
oxide_pciereg_capture(ktest_ctx_hdl_t *ctx)
{
	extern void zen_pcie_populate_dbg_adhoc(void);

	zen_pcie_populate_dbg_adhoc();

	KT_PASS(ctx);
}


static struct modlmisc oxide_ktest_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "Oxide ktest module"
};

static struct modlinkage oxide_ktest_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &oxide_ktest_modlmisc, NULL }
};

int
_init()
{
	int ret;
	ktest_module_hdl_t *km = NULL;
	ktest_suite_hdl_t *ks = NULL;

	VERIFY0(ktest_create_module("oxide", &km));

	VERIFY0(ktest_add_suite(km, "comm_page", &ks));
	VERIFY0(ktest_add_test(ks, "comm_page_vars_test",
	    comm_page_vars_test, 0));

	VERIFY0(ktest_add_suite(km, "pcie", &ks));
	VERIFY0(ktest_add_test(ks, "capture", oxide_pciereg_capture,
	    KTEST_FLAG_NONE));

	if ((ret = ktest_register_module(km)) != 0) {
		ktest_free_module(km);
		return (ret);
	}

	if ((ret = mod_install(&oxide_ktest_modlinkage)) != 0) {
		ktest_unregister_module("oxide");
		return (ret);
	}

	return (0);
}

int
_fini(void)
{
	ktest_unregister_module("oxide");
	return (mod_remove(&oxide_ktest_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&oxide_ktest_modlinkage, modinfop));
}
