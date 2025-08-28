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
 * A test module for various mac routines.
 */
#include <sys/ktest.h>
#include <sys/mac_flow.h>

static struct modlmisc mac_flow_ktest_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "mac flow ktest module"
};

static struct modlinkage mac_flow_ktest_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &mac_flow_ktest_modlmisc, NULL }
};

void
mac_flow_bake_test(ktest_ctx_hdl_t *ctx)
{
	KT_PASS(ctx);
}

int
_init()
{
	int ret;
	ktest_module_hdl_t *km = NULL;
	ktest_suite_hdl_t *ks = NULL;

	VERIFY0(ktest_create_module("mac_flow", &km));
	VERIFY0(ktest_add_suite(km, "bake", &ks));
	VERIFY0(ktest_add_test(ks, "mac_flow_bake_test",
	    mac_flow_bake_test, KTEST_FLAG_NONE));

	if ((ret = ktest_register_module(km)) != 0) {
		ktest_free_module(km);
		return (ret);
	}

	if ((ret = mod_install(&mac_flow_ktest_modlinkage)) != 0) {
		ktest_unregister_module("mac_flow");
		return (ret);
	}

	return (0);
}

int
_fini(void)
{
	ktest_unregister_module("mac_flow");
	return (mod_remove(&mac_flow_ktest_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&mac_flow_ktest_modlinkage, modinfop));
}
