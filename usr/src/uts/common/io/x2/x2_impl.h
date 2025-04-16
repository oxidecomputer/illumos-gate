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

#ifndef _SYS_X2_IMPL_H
#define	_SYS_X2_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following triplet is used to version the interface between the x2
 * driver and the userspace dataplane daemon.  This is primarily expected to be
 * bumped when an ioctl changes, but it needn't be limited to that.  For
 * example, changing the device name or the poll semantics would trigger a
 * version change as well.
 */
#define	X2_DRIVER_MAJOR	1
#define	X2_DRIVER_MINOR	1
#define	X2_DRIVER_PATCH	0

#define	X2_VENDID	0x1e6c

/*
 * The device has 2 64-bit BARs (0 and 2).
 */
#define	X2_NBARS		2

typedef struct x2 {
	list_node_t		x2_link;
	kmutex_t		x2_mutex;
	kcondvar_t		x2_cv;
	int			x2_instance;
	dev_info_t		*x2_dip;
	ddi_acc_handle_t	x2_cfgspace;
	uint32_t		x2_devid;
	ddi_acc_handle_t	x2_regs_hdls[X2_NBARS];
	caddr_t			x2_regs_bases[X2_NBARS];
	off_t			x2_regs_lens[X2_NBARS];

	int			x2_nintrs;
	int			x2_intr_cap;
	uint_t			x2_intr_pri;
} x2_t;

/*
 * Information maintained for each open() of a x2 device.
 */
typedef struct x2_instance_data_t {
	kmutex_t	xid_mutex;
	x2_t		*xid_x2;
} x2_instance_data_t;

void x2_dlog(x2_t *x2, const char *fmt, ...);
void x2_err(x2_t *x2, const char *fmt, ...);
int x2_read_reg(dev_info_t *dip, size_t offset, uint64_t *val);
int x2_write_reg(dev_info_t *dip, size_t offset, uint64_t val);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_X2_IMPL_H */
