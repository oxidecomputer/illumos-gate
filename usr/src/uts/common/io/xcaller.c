/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * Copyright 2022 Oxide Computer Company
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cpuvar.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/x_call.h>

#include <sys/xcaller.h>

#define	XCALLER_NAME		"xcaller"
#define	XCALLER_CTL_MINOR	0

#define	XCALLER_COUNT_LIMIT	100000

static dev_info_t	*xcaller_dip;

static int
xcaller_xc_func(xc_arg_t arg1 __unused, xc_arg_t arg2 __unused,
    xc_arg_t arg3 __unused)
{
	/* No-op */
	return (0);
}

static int
xcaller_measure(uint_t count, int target, hrtime_t *timings, hrtime_t *total)
{
	VERIFY3P(total, !=, NULL);
	VERIFY(count != 0 && count <= XCALLER_COUNT_LIMIT);

	cpuset_t *set = cpuset_alloc(KM_SLEEP);
	cpuset_zero(set);
	mutex_enter(&cpu_lock);
	if (target < 0) {
		/* All online CPUs */
		cpuset_or(set, &cpu_active_set);
	} else if (cpu_in_set(&cpu_active_set, (uint_t)target)) {
		cpuset_add(set, (uint_t)target);
	}
	mutex_exit(&cpu_lock);

	if (cpuset_isnull(set)) {
		cpuset_free(set);
		return (EINVAL);
	}


	kpreempt_disable();
	const hrtime_t start = gethrtime();
	if (timings == NULL) {
		/* Loop without individual timings */
		for (uint_t i = 0; i < count; i++) {
			xc_call(0, 0, 0, (ulong_t *)set, xcaller_xc_func);
		}
	} else {
		/* Loop with individual timings */
		hrtime_t prev = gethrtime();
		for (uint_t i = 0; i < count; i++) {
			xc_call(0, 0, 0, (ulong_t *)set, xcaller_xc_func);

			hrtime_t now = gethrtime();
			timings[i] = now - prev;
			prev = now;
		}
	}
	kpreempt_enable();

	*total = gethrtime() - start;
	cpuset_free(set);
	return (0);
}

static int
xcaller_ioc_basic_test(void *data, int md)
{
	struct xcaller_basic_test test;
	hrtime_t *timings_buf = NULL;

	if (ddi_copyin(data, &test, sizeof (test), md) != 0) {
		return (EFAULT);
	}
	if (test.xbt_count == 0 || test.xbt_count > XCALLER_COUNT_LIMIT) {
		return (EINVAL);
	}

	const size_t timings_size = test.xbt_count * sizeof (hrtime_t);
	if (test.xbt_timings != NULL) {
		timings_buf = kmem_zalloc(timings_size, KM_SLEEP);
	}

	int err = xcaller_measure(test.xbt_count, test.xbt_target, timings_buf,
	    (hrtime_t *)&test.xbt_duration);

	if (timings_buf != NULL) {
		if (err == 0 &&
		    ddi_copyout(timings_buf, test.xbt_timings, timings_size,
		    md) != 0) {
			err = EFAULT;
		}
		kmem_free(timings_buf, timings_size);
	}

	if (err == 0) {
		if (ddi_copyout(&test, data, sizeof (test), md) != 0) {
			err = EFAULT;
		}
	}

	return (err);
}

static int
xcaller_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	int error;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)xcaller_dip;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
		break;
	}
	return (error);
}

static int
xcaller_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}
	if (xcaller_dip != NULL) {
		return (DDI_FAILURE);
	}

	if (ddi_create_minor_node(dip, XCALLER_NAME, S_IFCHR, XCALLER_CTL_MINOR,
	    DDI_PSEUDO, 0) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	xcaller_dip = dip;
	ddi_report_dev(xcaller_dip);

	return (DDI_SUCCESS);
}

static int
xcaller_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	dev_info_t *old_dip = xcaller_dip;
	VERIFY(old_dip != NULL);
	xcaller_dip = NULL;
	ddi_remove_minor_node(old_dip, NULL);

	return (DDI_SUCCESS);
}

static int
xcaller_open(dev_t *devp, int flag __unused, int otype, cred_t *cr __unused)
{
	if (otype != OTYP_CHR) {
		return (EINVAL);
	}
	if (getminor(*devp) != XCALLER_CTL_MINOR) {
		return (ENXIO);
	}

	return (0);
}

static int
xcaller_close(dev_t dev __unused, int flag __unused, int otype __unused,
    cred_t *cr __unused)
{
	return (0);
}

static int
xcaller_ioctl(dev_t dev, int cmd, intptr_t data, int md, cred_t *cr, int *rv)
{
	if (getminor(dev) != XCALLER_CTL_MINOR) {
		return (ENXIO);
	}
	if (get_udatamodel() != DATAMODEL_NATIVE) {
		return (ENXIO);
	}

	/* You gotta _be_ somebody */
	if (crgetzoneid(cr) != GLOBAL_ZONEID || crgetuid(cr) != 0) {
		return (EPERM);
	}

	if (cmd == XCALLER_BASIC_TEST) {
		*rv = 0;
		return (xcaller_ioc_basic_test((void *)data, md));
	}

	return (ENOTTY);
}

static struct cb_ops xcaller_cb_ops = {
	.cb_open	= xcaller_open,
	.cb_close	= xcaller_close,
	.cb_strategy	= nodev,
	.cb_print	= nodev,
	.cb_dump	= nodev,
	.cb_read	= nodev,
	.cb_write	= nodev,
	.cb_ioctl	= xcaller_ioctl,
	.cb_devmap	= nodev,
	.cb_mmap	= nodev,
	.cb_segmap	= nodev,
	.cb_chpoll	= nochpoll,
	.cb_prop_op	= ddi_prop_op,
	.cb_str		= NULL,
	.cb_flag	= D_MP | D_NEW | D_HOTPLUG,
	.cb_rev		= CB_REV,
	.cb_aread	= nodev,
	.cb_awrite	= nodev
};

static struct dev_ops xcaller_ops = {
	.devo_rev	= DEVO_REV,
	.devo_refcnt	= 0,
	.devo_getinfo	= xcaller_info,
	.devo_identify	= nulldev,
	.devo_probe	= nulldev,
	.devo_attach	= xcaller_attach,
	.devo_detach	= xcaller_detach,
	.devo_reset 	= nodev,
	.devo_cb_ops	= &xcaller_cb_ops,
	.devo_bus_ops	= NULL,
	.devo_power	= ddi_power,
	.devo_quiesce	= ddi_quiesce_not_needed
};

static struct modldrv modldrv = {
	&mod_driverops,
	XCALLER_NAME,
	&xcaller_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
