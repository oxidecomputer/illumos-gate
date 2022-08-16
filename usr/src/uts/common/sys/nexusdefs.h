/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_NEXUSDEFS_H
#define	_SYS_NEXUSDEFS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Bus Nexus Control Operations
 */

typedef enum {
	DDI_CTLOPS_DMAPMAPC,
	DDI_CTLOPS_INITCHILD,
	DDI_CTLOPS_UNINITCHILD,
	DDI_CTLOPS_REPORTDEV,
	DDI_CTLOPS_REPORTINT,
	DDI_CTLOPS_REGSIZE,
	DDI_CTLOPS_NREGS,
	DDI_CTLOPS_RESERVED0,	/* Originally DDI_CTLOPS_NINTRS, obsolete */
	DDI_CTLOPS_SIDDEV,
	DDI_CTLOPS_SLAVEONLY,
	DDI_CTLOPS_AFFINITY,
	DDI_CTLOPS_IOMIN,
	DDI_CTLOPS_PTOB,
	DDI_CTLOPS_BTOP,
	DDI_CTLOPS_BTOPR,
	DDI_CTLOPS_RESERVED1,	/* Originally DDI_CTLOPS_POKE_INIT, obsolete */
	DDI_CTLOPS_RESERVED2,	/* Originally DDI_CTLOPS_POKE_FLUSH, obsolete */
	DDI_CTLOPS_RESERVED3,	/* Originally DDI_CTLOPS_POKE_FINI, obsolete */
	DDI_CTLOPS_RESERVED4, /* Originally DDI_CTLOPS_INTR_HILEVEL, obsolete */
	DDI_CTLOPS_RESERVED5, /* Originally DDI_CTLOPS_XLATE_INTRS, obsolete */
	DDI_CTLOPS_DVMAPAGESIZE,
	DDI_CTLOPS_POWER,
	DDI_CTLOPS_ATTACH,
	DDI_CTLOPS_DETACH,
	DDI_CTLOPS_QUIESCE,
	DDI_CTLOPS_UNQUIESCE,
	DDI_CTLOPS_PEEK,
	DDI_CTLOPS_POKE
} ddi_ctl_enum_t;

/*
 * For source compatibility, we define the following obsolete code:
 * Do NOT use this, use the real constant name.
 */
#define	DDI_CTLOPS_REMOVECHILD	DDI_CTLOPS_UNINITCHILD

/*
 * Bus config ops.  Arguments are referred to according to the convention
 *
 * int bus_config(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
 *     void *arg, dev_info_t **childp);
 *
 * int bus_unconfig(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
 *     void *arg);
 *
 * The interpretation of these parameters and return values is described when
 * op is the variant in question.  For the flags argument, see NDI_XX in
 * sys/sunndi.h beginning with NDI_DEVI_REMOVE.  Not all flags are valid for a
 * given operation.  pdip always refers to the nexus node.
 *
 * XXX Expand me so that readers can learn how to use this interface properly.
 */
typedef enum {
	/*
	 * Never invoked.  Always return NDI_FAILURE.
	 */
	BUS_ENUMERATE = 0,

	/*
	 * Configure a single child.  arg is a char *; it points to the dev name
	 * of the child to be configured (if such a child exists).  The dev name
	 * consists of the node name and unit address separated by the @
	 * character and is suitable for parsing by i_ddi_parse_name().  Not all
	 * nodes have a unit address, and the nexus may be asked to configure a
	 * child with a name and/or unit address that can't or doesn't exist --
	 * physically or in whatever other sense is applicable to the node --
	 * beneath that nexus, in which case NDI_FAILURE or another suitable
	 * error status should be returned.
	 */
	BUS_CONFIG_ONE,

	/*
	 * Configure all children.  arg is always a major_t and always
	 * DDI_MAJOR_T_NONE so it may be ignored; however it is also possible to
	 * treat BUS_CONFIG_ALL and BUS_CONFIG_DRIVER the same, distinguished
	 * only by this argument.  Configuration of children is intended to be
	 * idempotent and this operation should attempt to configure all
	 * possible children even if configuring a child fails; therefore errors
	 * associated with configuring any individual child are not propagated.
	 */
	BUS_CONFIG_ALL,

	/*
	 * Never invoked.  Always return NDI_FAILURE.
	 */
	BUS_CONFIG_AP,

	/*
	 * Configure all children bound to the major number specified by arg,
	 * which is of type major_t.  If arg is DDI_MAJOR_T_NONE, this operation
	 * is identical to BUS_CONFIG_ALL.  Failure semantics are identical to
	 * those of BUS_CONFIG_ALL.
	 */
	BUS_CONFIG_DRIVER,

	/*
	 * Unconfigure the child named by arg, which is a char * pointing to the
	 * child's dev name.  See BUS_CONFIG_ONE for the format of this string.
	 */
	BUS_UNCONFIG_ONE,

	/*
	 * Unconfigure all children to which the specified driver is bound.  The
	 * driver is specified by major number in arg, of type major_t.
	 * Analogous to BUS_CONFIG_DRIVER, if arg is DDI_MAJOR_T_NONE, this
	 * operation is equivalent to BUS_UNCONFIG_ALL.  Failure semantics are
	 * identical to those of BUS_CONFIG_ALL.
	 */
	BUS_UNCONFIG_DRIVER,

	/*
	 * Unconfigure all children.  This is analogous to BUS_CONFIG_ALL, and
	 * arg is once again always a major_t and always DDI_MAJOR_T_NONE so it
	 * may be ignored.  Failure semantics are identical to those of
	 * BUS_CONFIG_ALL.
	 */
	BUS_UNCONFIG_ALL,

	/*
	 * Never invoked.  Always return NDI_FAILURE.
	 */
	BUS_UNCONFIG_AP,

	/*
	 * Similar to BUS_CONFIG_ONE, used when the OBP dev name differs from
	 * the normal one.  arg is a char * pointing to the OBP dev name.
	 * Currently used only by IB to handle OBP boot service device names;
	 * everyone else wants to return NDI_FAILURE.
	 */
	BUS_CONFIG_OBP_ARGS
} ddi_bus_config_op_t;

/*
 * Bus Power Operations
 */
typedef enum {
	BUS_POWER_CHILD_PWRCHG = 0,
	BUS_POWER_NEXUS_PWRUP,
	BUS_POWER_PRE_NOTIFICATION,
	BUS_POWER_POST_NOTIFICATION,
	BUS_POWER_HAS_CHANGED,
	BUS_POWER_NOINVOL
} pm_bus_power_op_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_NEXUSDEFS_H */
