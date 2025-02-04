/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _IPCC_DRV_H
#define	_IPCC_DRV_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Version history
 *   1	- initial integration.
 *   2	- IPCC_MACS no longer has an input parameter for group, the driver
 *	  now always returns the full set of MACs provided by the SP.
 */
#define	IPCC_DRIVER_VERSION	2

#ifdef _KERNEL

#include <sys/stdbool.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/ipcc.h>
#include <sys/io/fch/espi.h>

#define	IPCC_DRIVER_NAME	"ipcc"
#define	IPCC_NODE_NAME		"ipcc"
#define	IPCC_MINOR		((minor_t)0ul)

/*
 * This structure tracks state for each ioctl() that is being processed.
 */
typedef struct ipcc_state {
	dev_t		is_dev;
	cred_t		*is_cred;

	bool		is_open;

	ldi_ident_t	is_ldiid;

	/* Only one or other of the following two will end up being used */
	ldi_handle_t		is_ldih;
	mmio_reg_block_t	is_espi_block;

	bool		is_sp_intr;
	ldi_handle_t	is_sp_intr_ldih;
} ipcc_state_t;

typedef struct ipcc_stats {
	struct kstat_named	opens;
	struct kstat_named	opens_fail;
	struct kstat_named	interrupts;
	struct kstat_named	ioctl_version;
	struct kstat_named	ioctl_status;
	struct kstat_named	ioctl_ident;
	struct kstat_named	ioctl_macs;
	struct kstat_named	ioctl_keylookup;
	struct kstat_named	ioctl_rot;
	struct kstat_named	ioctl_inventory;
	struct kstat_named	ioctl_keyset;
	struct kstat_named	ioctl_unknown;
	struct kstat_named	ioctl_apob;
} ipcc_stats_t;

#define	IPCC_PROP_PATH			"path"
#define	IPCC_PROP_SP_INTR_PATH		"sp-intr-path"

#define	LDI_FLAGS		(FEXCL | FREAD | FWRITE | FNOCTTY)
#define	LDI_SP_INTR_FLAGS	(FEXCL | FREAD | FNOCTTY)

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _IPCC_DRV_H */
