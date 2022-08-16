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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef	_SYS_DDI_INTR_IMPL_H
#define	_SYS_DDI_INTR_IMPL_H

/*
 * Sun DDI interrupt implementation specific definitions
 */

#include <sys/list.h>
#include <sys/ksynch.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

/*
 * Interrupt operation types passed to the bus_intr_op() NDI endpoint.  Comments
 * above each operation describe the purpose and the interpretation of the
 * result parameter passed by pointer into the endpoint.  These descriptions
 * refer to parameters according to this conventional prototype:
 *
 * int bus_intr_ops(dev_info_t *dip, dev_info_t *rdip, ddi_intr_op_t op,
 *     ddi_intr_handle_impl_t *hdl, void *result);
 *
 * For all operations, dip references the node corresponding to the nexus
 * instance whose endpoint is being called.  rdip references the requesting or
 * responsible node, the node to which the operation applies.  In general it is
 * possible for dip == rdip.  Often, rdip is a child node of dip, but as
 * requests may be passed up the tree, it is possible for rdip to be an indirect
 * descendant of dip, so implementers cannot always assume that the properties
 * or private data attached to rdip conform to the conventions the receiving
 * nexus implements with respect to its immediate children.
 *
 * Similarly, hdl may point to a handle that was previously initialised by the
 * receiving nexus's ALLOC op following a call by the child driver to
 * ddi_intr_alloc(), but for some operations this is not the case.  These
 * exceptions are noted in the description of each operation to which they
 * apply; additionally, when operations are passed up the tree, the state of the
 * handle may not always match the way the receiving nexus would have
 * initialised it even if it was previously initialised by a child nexus's ALLOC
 * endpoint.  These inconsistencies are generally bugs in child nexus drivers
 * encouraged by general weaknesses in the NDI, but we highlight them here as
 * they are often behind various special cases in nexus drivers.
 *
 * There is some additional useful background on the interrupt allocation state
 * machine and other aspects of this interface in PSARC 2004/253.
 */
typedef enum {
	/*
	 * Get the set of interrupt types supported for rdip.
	 * True type of result: int *
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: a bitmask of supported
	 * interrupt types DDI_INTR_TYPE_XXX
	 * Handle properties: ih_dip == rdip, otherwise undefined
	 *
	 * This endpoint indirectly implements ddi_intr_get_supported_types(9f).
	 * Prior to performing other interrupt operations on rdip, the DDI
	 * invokes this operation and caches the value of *result iff it is
	 * nonzero and the nexus returns DDI_SUCCESS.  Otherwise, the DDI will
	 * invoke this operation prior to invoking the NINTRS, NAVAIL, or ALLOC
	 * operations to determine whether the requested type is supported.
	 * Thus, the *result value effectively gates the interrupt state machine
	 * for all interrupts of a given type for rdip.
	 *
	 * If no interrupts of any kind are supported for the device indicated
	 * by rdip, the nexus must return a failure code (i.e., something other
	 * than DDI_SUCCESS).  Do not return DDI_SUCCESS with *result == 0.
	 *
	 * The semantics of this operation are poorly specified in the case
	 * where interrupts of a particular type are (or could be) supported for
	 * rdip but no interrupts of that type are actually supported.  To
	 * address this, we will state that the responding nexus is responsible
	 * for collapsing this case at the time of a call to SUPPORTED_TYPES
	 * into the case where interrupts of that type are never supported for
	 * rdip.  This holds for the existing relevant consumer (pci) with
	 * respect to both MSI and MSI-X.  It also implies that the nexus must
	 * always provide the same result and return status for both
	 * SUPPORTED_TYPES and NINTRS (see below) for a given value of rdip as
	 * long as that descendant node exists.
	 */
	DDI_INTROP_SUPPORTED_TYPES = 1,

	/*
	 * Get the number of interrupts of hdl->ih_type available for rdip.
	 * True type of result: int *
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: the number of interrupts
	 * available
	 * Handle properties: ih_dip == rdip, ih_type == DDI_INTR_TYPE_XXX
	 *
	 * The DDI guarantees that hdl->ih_type is supported for rdip as
	 * determined by a previous successful invocation of SUPPORTED_TYPES.
	 * The nexus will not be invoked to determine the number of interrupts
	 * of a type that is never supported for rdip.
	 *
	 * As discussed above, the semantics of this operation are poorly
	 * specified when an interrupt type is supported for rdip but no
	 * interrupts of that type are actually supported for that node.
	 * Additionally, a quirk (probably a bug) in the DDI implementation
	 * allows the return value of the nexus's endpoint to be given directly
	 * to callers of ddi_intr_get_nintrs(9f) even if that return status code
	 * is not among those documented.  Therefore, a conservative nexus must:
	 *
	 * - always return one of those status codes for this operation, and
	 * - not return DDI_SUCCESS with *result == 0, and
	 * - always provide the same return status code and value of *result for
	 *   given values of ih_type and ih_dip/rdip for as long as rdip exists.
	 */
	DDI_INTROP_NINTRS,

	/*
	 * Allocate interrupts as documented for ddi_intr_alloc(9f).
	 * True type of result: int *
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: the number of interrupts
	 * actually allocated
	 * Handle properties: ih_dip == rdip, ih_type == DDI_INTR_TYPE_XXX,
	 * ih_inum is the index into rdip's set of possible interrupts of the
	 * beginning of the range requested, ih_scratch1 is the number of
	 * interrupts requested in the contiguous range, ih_scratch2 is an int
	 * containing a mask of behaviour flags as described by
	 * ddi_intr_alloc(9f).  This is a temporary handle and there is only
	 * one; filling in multiple handles for count > 1 is done by the
	 * framework.  The nexus endpoint is allowed to modify the contents of
	 * the handle but the modified contents are not generally preserved into
	 * the child's handle(s); see the ddi_intr_alloc() implementation.
	 *
	 * The framework guarantees that ih_type is among those included in the
	 * result of a previous SUPPORTED_TYPES call and that ih_inum +
	 * ih_scratch1 <= the result of a previous NINTRS call for that type.
	 * That is, the framework will not invoke the nexus to allocate an
	 * interrupt for a descendant node that has no supported interrupts of
	 * the specified type.
	 */
	DDI_INTROP_ALLOC,

	/*
	 * Get the priority level (ipl/pil) of the interrupt belonging to rdip
	 * and described by hdl.
	 * True type of result: uint_t *
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: the priority level
	 * Handle properties: ih_{dip, type, inum} may be relied upon, and
	 * ih_dip == rdip; ih_pri may not be valid and cannot be returned
	 * blindly; generally, the framework will not invoke this endpoint when
	 * ih_pri is valid, but this is not guaranteed either.
	 */
	DDI_INTROP_GETPRI,

	/*
	 * Set the priority level (ipl/pil) of the interrupt belonging to rdip
	 * and described by hdl.
	 * True type of result: uint_t *
	 * Initial contents of *result: the desired priority level
	 * Final contents of *result upon success: the actual priority level set
	 * Handle properties: Attributes set during the ddi_intr_alloc() path
	 * may be assumed.  Currently the framework will not make this request
	 * with *result == hdl->ih_pri but that may not be guaranteed.
	 */
	DDI_INTROP_SETPRI,

	/*
	 * Implementation for ddi_intr_add_handler(9f).
	 * True type of result: void
	 * Handle properties: Attributes set during the ddi_intr_alloc() path
	 * may be assumed.  Additionally, ih_cb_func and ih_cb_arg{1,2} will be
	 * set.  This endpoint will not be invoked again on a handle for which
	 * ADDISR has already succeeded.
	 */
	DDI_INTROP_ADDISR,

	/*
	 * Implementation for ddi_intr_dup_handler(9f), which is used only on
	 * SPARC and therefore obsolete.  Just return DDI_FAILURE; DDI_ENOTSUP
	 * would make more sense but is not documented in the manual.
	 */
	DDI_INTROP_DUPVEC,

	/*
	 * Implementation for ddi_intr_enable(9f).
	 * True type of result: void
	 * Handle properties: Attributes set during the ALLOC path
	 * and at least one successful invocation of ADDISR may be assumed.  If
	 * the interrupt's capabilities include DDI_INTR_FLAG_BLOCK, this
	 * endpoint will not be invoked.  Note that the obsolete DDI function
	 * ddi_add_intr(9f) also ends up here after going through the ALLOC,
	 * GETPRI, and ADDISR endpoints, and is still used by several drivers.
	 */
	DDI_INTROP_ENABLE,

	/*
	 * Implementation for ddi_intr_block_enable(9f).
	 * True type of result: void
	 * Handle properties: The handle will be the first one of the block
	 * previously allocated via the ALLOC path.  ih_scratch1 == number of
	 * interrupts in the block, ih_scratch2 is a ddi_intr_handle_impl_t **
	 * pointing to the array of handles.  This will not be invoked except
	 * for MSI interrupts with the BLOCK capability.
	 */
	DDI_INTROP_BLOCKENABLE,

	/*
	 * Implementation for ddi_intr_block_disable(9f).
	 * True type of result: void
	 * Handle properties: See BLOCKENABLE.  This endpoint will not be
	 * invoked for interrupts that were not previously enabled.
	 */
	DDI_INTROP_BLOCKDISABLE,

	/*
	 * Implementation for ddi_intr_disable(9f).
	 * True type of result: void
	 * Handle properties: See ENABLE.  This endpoint will not be invoked for
	 * interrupts that were not previously enabled.
	 */
	DDI_INTROP_DISABLE,

	/*
	 * Implementation for ddi_intr_add_handler(9f).
	 * True type of result: void
	 * Handle properties: see ADDISR.  This endpoint will not be invoked for
	 * interrupts that have not previously had an ADDISR call succeed.
	 */
	DDI_INTROP_REMISR,

	/*
	 * Implementation for ddi_intr_free(9f).
	 * True type of result: void
	 * Handle properties: This endpoint will be invoked only for interrupts
	 * that have previously:
	 * 1. Had a successful call to ALLOC, and
	 * 2a. Have either never had a successful call to ADDISR or
	 * 2b. Have had a subsequent successful call to REMISR.
	 * ih_scratch1 == 1, always, even if this interrupt was allocated as
	 * part of a contiguous region of multiple interrupts.
	 */
	DDI_INTROP_FREE,

	/*
	 * Implementation for ddi_intr_get_cap(9f).
	 * True type of result: int *
	 * Initial contents of *result: 0
	 * Final contents of *result upon success: a bitmask of DDI_INTR_FLAG_*
	 * capabilities associated with the interrupt referenced by hdl.
	 * Handle properties: May be invoked with a temporary handle or a real
	 * one, but always one that has had a successful call to ALLOC and
	 * GETPRI at some point in the past.  ih_cap should be not assumed to be
	 * valid and cannot be returned blindly.
	 */
	DDI_INTROP_GETCAP,

	/*
	 * Implementation for ddi_intr_set_cap(9f).
	 * True type of result: int *
	 * Initial contents of *result: the set of desired capabilities (which
	 * may or may not include those defined to be read-only)
	 * Final contents of *result upon success: ignored
	 * Handle properties: Same as GETCAP.
	 */
	DDI_INTROP_SETCAP,

	/*
	 * Implementation for ddi_intr_set_mask(9f).
	 * True type of result: void
	 * Handle properties: Same as for DISABLE; i.e., the handle will refer
	 * to a valid, enabled interrupt.  Additionally, this endpoint will not
	 * be invoked for interrupts that do not have the MASKABLE capability.
	 */
	DDI_INTROP_SETMASK,

	/*
	 * Implementation for ddi_intr_clr_mask(9f).
	 * True type of result: void
	 * Handle properties: Same as for SETMASK.
	 */
	DDI_INTROP_CLRMASK,

	/*
	 * Implementation for ddi_intr_get_pending(9f).
	 * True type of result: int * (see manual)
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: 0 if no pending interrupt,
	 * nonzero otherwise (see manual).
	 * Handle properties: Dangerously underspecified.  In practice, this
	 * endpoint has no callers, so any possible callers should be
	 * DDI-compliant consumers that will have obtained the handle via ALLOC.
	 * There is no guarantee that ADDISR or ENABLE has ever succeeded,
	 * however, and nothing in the framework itself guarantees the validity
	 * of any member other than ih_dip.  Practically speaking, implementers
	 * have to assume that at least ih_inum is also valid but beyond that
	 * can assume only what their own ALLOC endpoint has done.
	 */
	DDI_INTROP_GETPENDING,

	/*
	 * Implementation for ddi_intr_get_navail(9f) and internal consumers.
	 * True type of result: uint_t *
	 * Initial contents of *result: undefined
	 * Final contents of *result upon success: number of interrupts of type
	 * hdl->ih_type that are available to be allocated for rdip
	 * Handle properties: May be real or temporary.  Only ih_dip and ih_type
	 * are guaranteed to be valid.
	 *
	 * The DDI implementation quirk discussed in the section on NINTRS above
	 * does not apply to NAVAIL; in general, the DDI treats *result == 0 the
	 * same as a non-DDI_SUCCESS return status code from this endpoint, and
	 * will replace all such return status codes with DDI_INTR_NOTFOUND to
	 * satisfy the documented behaviour of ddi_intr_get_navail(9f)
	 * indirectly implemented by this entry point.  However, a conservative
	 * nexus's entry point should satisfy the same constraints as for NINTRS
	 * above; doing so is also likely to improve allocation performance.
	 */
	DDI_INTROP_NAVAIL,

	/*
	 * Obtain the interrupt resource management (IRM) pool that supplies
	 * interrupts of type hdl->ih_type to rdip via this nexus.  IRM pools
	 * apply onto to nexi that support MSI-X interrupts and can return
	 * DDI_ENOTSUP otherwise.
	 * True type of result: ddi_irm_pool_t **
	 * Initial contents of *result: undefined
	 * Final contents of *result: a pointer to the IRM pool
	 * Handle properties: May be real or temporary.  Only ih_dip and ih_type
	 * are guaranteed to be valid.
	 */
	DDI_INTROP_GETPOOL,

	/*
	 * Obtain the target CPU for the interrupt described by hdl.
	 * True type of result: processorid_t *
	 * Initial contents of *result: undefined
	 * Final contents of *result: the CPU identifier to which this interrupt
	 * is currently directed, regardless of whether it has been bound
	 * explicitly
	 * Handle properties: The handle will be associated with an interrupt
	 * that has been enabled via the ENABLE endpoint.  This endpoint is
	 * invoked only via get_intr_affinity(), which currently has no callers.
	 */
	DDI_INTROP_GETTARGET,

	/*
	 * Set the target CPU for the interrupt described by hdl.
	 * True type of result: processorid_t *
	 * Initial contents of *result: the target CPU identifier
	 * Final contents of *result: the actual target CPU identifier assigned
	 * Handle properties: See GETTARGET.  Additionally, the framework
	 * invokes this endpoint only for MSI-X interrupts, though it's unclear
	 * whether this is a guarantee or a temporary limitation.
	 */
	DDI_INTROP_SETTARGET
} ddi_intr_op_t;

/* Version number used in the handles */
#define	DDI_INTR_VERSION_1	1
#define	DDI_INTR_VERSION	DDI_INTR_VERSION_1

/*
 * One such data structure is allocated per ddi_intr_handle_t
 * This is the incore copy of the regular interrupt info.
 */
typedef struct ddi_intr_handle_impl {
	dev_info_t		*ih_dip;	/* dip associated with handle */
	uint16_t		ih_type;	/* interrupt type being used */
	ushort_t		ih_inum;	/* interrupt number */
	uint32_t		ih_vector;	/* vector number */
	uint16_t		ih_ver;		/* Version */
	uint_t			ih_state;	/* interrupt handle state */
	uint_t			ih_cap;		/* interrupt capabilities */
	uint_t			ih_pri;		/* priority - bus dependent */
	krwlock_t		ih_rwlock;	/* read/write lock per handle */

	uint_t			(*ih_cb_func)(caddr_t, caddr_t);
	void			*ih_cb_arg1;
	void			*ih_cb_arg2;

	/*
	 * The following 3 members are used to support MSI-X specific features
	 */
	uint_t			ih_flags;	/* Misc flags */
	uint_t			ih_dup_cnt;	/* # of dupped msi-x vectors */
	struct ddi_intr_handle_impl	*ih_main;
						/* pntr to the main vector */
	/*
	 * The next set of members are for 'scratch' purpose only.
	 * The DDI interrupt framework uses them internally and their
	 * interpretation is left to the framework. For now,
	 *	scratch1	- used to send NINTRs information
	 *			  to various nexus drivers.
	 *	scratch2	- used to send 'behavior' flag
	 *			  information to the nexus drivers
	 *			  from ddi_intr_alloc().  It is also
	 *			  used to send 'h_array' to the nexus drivers
	 *			  for ddi_intr_block_enable/disable() on x86.
	 *	private		- On X86 it usually carries a pointer to
	 *			  ihdl_plat_t.  Not used on SPARC platforms.
	 */
	void			*ih_private;	/* Platform specific data */
	uint_t			ih_scratch1;	/* Scratch1: #interrupts */
	void			*ih_scratch2;	/* Scratch2: flag/h_array */

	/*
	 * The ih_target field may not reflect the actual target that is
	 * currently being used for the given interrupt. This field is just a
	 * snapshot taken either during ddi_intr_add_handler() or
	 * get/set_intr_affinity() calls.
	 */
	processorid_t		ih_target;	/* Target ID */
} ddi_intr_handle_impl_t;

/* values for ih_state (strictly for interrupt handle) */
#define	DDI_IHDL_STATE_ALLOC	0x01	/* Allocated. ddi_intr_alloc() called */
#define	DDI_IHDL_STATE_ADDED	0x02	/* Added interrupt handler */
					/* ddi_intr_add_handler() called */
#define	DDI_IHDL_STATE_ENABLE	0x04	/* Enabled. ddi_intr_enable() called */

#define	DDI_INTR_IS_MSI_OR_MSIX(type) \
	((type) == DDI_INTR_TYPE_MSI || (type) == DDI_INTR_TYPE_MSIX)

#define	DDI_INTR_BEHAVIOR_FLAG_VALID(f) \
	    (((f) == DDI_INTR_ALLOC_NORMAL) || ((f) == DDI_INTR_ALLOC_STRICT))

#define	DDI_INTR_TYPE_FLAG_VALID(t) \
	    (((t) == DDI_INTR_TYPE_FIXED) || \
	    ((t) == DDI_INTR_TYPE_MSI) || \
	    ((t) == DDI_INTR_TYPE_MSIX))

/* values for ih_flags */
#define	DDI_INTR_MSIX_DUP	0x01	/* MSI-X vector which has been dupped */

/* Maximum number of MSI resources to allocate */
#define	DDI_MAX_MSI_ALLOC	2

/* Default number of MSI-X resources to allocate */
#define	DDI_DEFAULT_MSIX_ALLOC	2

#define	DDI_MSIX_ALLOC_DIVIDER	32
#define	DDI_MIN_MSIX_ALLOC	8
#define	DDI_MAX_MSIX_ALLOC	2048

struct av_softinfo;

/*
 * One such data structure is allocated per ddi_soft_intr_handle
 * This is the incore copy of the softint info.
 */
typedef struct ddi_softint_hdl_impl {
	dev_info_t	*ih_dip;		/* dip associated with handle */
	uint_t		ih_pri;			/* priority - bus dependent */
	krwlock_t	ih_rwlock;		/* read/write lock per handle */
	struct av_softinfo *ih_pending;		/* whether softint is pending */

	uint_t		(*ih_cb_func)(caddr_t, caddr_t);
						/* cb function for soft ints */
	void		*ih_cb_arg1;		/* arg1 of callback function */
	void		*ih_cb_arg2;		/* arg2 passed to "trigger" */

	/*
	 * The next member is for 'scratch' purpose only.
	 * The DDI interrupt framework uses it internally and its
	 * interpretation is left to the framework.
	 *	private		- used by the DDI framework to pass back
	 *			  and forth 'softid' information on SPARC
	 *			  side only. Not used on X86 platform.
	 */
	void		*ih_private;		/* Platform specific data */
} ddi_softint_hdl_impl_t;

/* Softint internal implementation defines */
#define	DDI_SOFT_INTR_PRI_M	4
#define	DDI_SOFT_INTR_PRI_H	6

/*
 * One such data structure is allocated for MSI-X enabled
 * device. If no MSI-X is enabled then it is NULL
 */
typedef struct ddi_intr_msix {
	/* MSI-X Table related information */
	ddi_acc_handle_t	msix_tbl_hdl;		/* MSI-X table handle */
	uint32_t		*msix_tbl_addr;		/* MSI-X table addr */
	uint32_t		msix_tbl_offset;	/* MSI-X table offset */

	/* MSI-X PBA Table related information */
	ddi_acc_handle_t	msix_pba_hdl;		/* MSI-X PBA handle */
	uint32_t		*msix_pba_addr;		/* MSI-X PBA addr */
	uint32_t		msix_pba_offset;	/* MSI-X PBA offset */

	ddi_device_acc_attr_t	msix_dev_attr;		/* MSI-X device attr */
} ddi_intr_msix_t;

/*
 * Interrupt Resource Management (IRM).
 */

#define	DDI_IRM_POLICY_LARGE	1
#define	DDI_IRM_POLICY_EVEN	2

#define	DDI_IRM_POLICY_VALID(p)	(((p) == DDI_IRM_POLICY_LARGE) || \
				((p) == DDI_IRM_POLICY_EVEN))

#define	DDI_IRM_FLAG_ACTIVE	0x1		/* Pool is active */
#define	DDI_IRM_FLAG_QUEUED	0x2		/* Pool is queued */
#define	DDI_IRM_FLAG_WAITERS	0x4		/* Pool has waiters */
#define	DDI_IRM_FLAG_EXIT	0x8		/* Balance thread must exit */
#define	DDI_IRM_FLAG_NEW	0x10		/* Request is new */
#define	DDI_IRM_FLAG_CALLBACK	0x20		/* Request has callback */

/*
 * One such data structure for each supply of interrupt vectors.
 * Contains information about the size and policies defining the
 * supply, and a list of associated device-specific requests.
 */
typedef struct ddi_irm_pool {
	int		ipool_flags;		/* Status flags of the pool */
	int		ipool_types;		/* Types of interrupts */
	int		ipool_policy;		/* Rebalancing policy */
	uint_t		ipool_totsz;		/* Total size of the pool */
	uint_t		ipool_defsz;		/* Default allocation size */
	uint_t		ipool_minno;		/* Minimum number consumed */
	uint_t		ipool_reqno;		/* Total number requested */
	uint_t		ipool_resno;		/* Total number reserved */
	kmutex_t	ipool_lock;		/* Protects all pool usage */
	kmutex_t	ipool_navail_lock;	/* Protects 'navail' of reqs */
	kcondvar_t	ipool_cv;		/* Condition variable */
	kthread_t	*ipool_thread;		/* Balancing thread */
	dev_info_t	*ipool_owner;		/* Device that created pool */
	list_t		ipool_req_list;		/* All requests in pool */
	list_t		ipool_scratch_list;	/* Requests being reduced */
	list_node_t	ipool_link;		/* Links in global pool list */
} ddi_irm_pool_t;

/*
 * One such data structure for each dip's devinfo_intr_t.
 * Contains information about vectors requested from IRM.
 */
typedef struct ddi_irm_req {
	int		ireq_flags;		/* Flags for request */
	int		ireq_type;		/* Type requested */
	uint_t		ireq_nreq;		/* Number requested */
	uint_t		ireq_navail;		/* Number available */
	uint_t		ireq_scratch;		/* Scratch value */
	dev_info_t	*ireq_dip;		/* Requesting device */
	ddi_irm_pool_t	*ireq_pool_p;		/* Supplying pool */
	list_node_t	ireq_link;		/* Request list link */
	list_node_t	ireq_scratch_link;	/* Scratch list link */
} ddi_irm_req_t;

/*
 * This structure is used to pass parameters to ndi_create_irm(),
 * and describes the operating parameters of an IRM pool.
 */
typedef struct ddi_irm_params {
	int	iparams_types;		/* Types of interrupts in pool */
	uint_t	iparams_total;		/* Total size of the pool */
} ddi_irm_params_t;

/*
 * One such data structure is allocated for each dip.
 * It has interrupt related information that can be
 * stored/retrieved for convenience.
 */
typedef struct devinfo_intr {
	/* These three fields show what the device is capable of */
	uint_t		devi_intr_sup_types;	/* Intrs supported by device */

	ddi_intr_msix_t	*devi_msix_p;		/* MSI-X info, if supported */

	/* Next three fields show current status for the device */
	uint_t		devi_intr_curr_type;	/* Interrupt type being used */
	uint_t		devi_intr_sup_nintrs;	/* #intr supported */
	uint_t		devi_intr_curr_nintrs;	/* #intr currently being used */
	/*
	 * #intr currently being enabled
	 * (for MSI block enable, the valuse is either 1 or 0.)
	 */
	uint_t		devi_intr_curr_nenables;

	ddi_intr_handle_t *devi_intr_handle_p;	/* Hdl for legacy intr APIs */

#if defined(__i386) || defined(__amd64)
	/* Save the PCI config space handle */
	ddi_acc_handle_t devi_cfg_handle;
	int		 devi_cap_ptr;		/* MSI or MSI-X cap pointer */
#endif

	ddi_irm_req_t	*devi_irm_req_p;	/* IRM request information */
} devinfo_intr_t;

#define	NEXUS_HAS_INTR_OP(dip)	\
	((DEVI(dip)->devi_ops->devo_bus_ops) && \
	(DEVI(dip)->devi_ops->devo_bus_ops->busops_rev >= BUSO_REV_9) && \
	(DEVI(dip)->devi_ops->devo_bus_ops->bus_intr_op))

int	i_ddi_intr_ops(dev_info_t *dip, dev_info_t *rdip, ddi_intr_op_t op,
	    ddi_intr_handle_impl_t *hdlp, void *result);

int	i_ddi_add_softint(ddi_softint_hdl_impl_t *);
void	i_ddi_remove_softint(ddi_softint_hdl_impl_t *);
int	i_ddi_trigger_softint(ddi_softint_hdl_impl_t *, void *);
int	i_ddi_set_softint_pri(ddi_softint_hdl_impl_t *, uint_t);

void	i_ddi_intr_devi_init(dev_info_t *dip);
void	i_ddi_intr_devi_fini(dev_info_t *dip);

uint_t	i_ddi_intr_get_supported_types(dev_info_t *dip);
void	i_ddi_intr_set_supported_types(dev_info_t *dip, int sup_type);
uint_t	i_ddi_intr_get_current_type(dev_info_t *dip);
void	i_ddi_intr_set_current_type(dev_info_t *dip, int intr_type);
uint_t	i_ddi_intr_get_supported_nintrs(dev_info_t *dip, int intr_type);
void	i_ddi_intr_set_supported_nintrs(dev_info_t *dip, int nintrs);
uint_t	i_ddi_intr_get_current_nintrs(dev_info_t *dip);
void	i_ddi_intr_set_current_nintrs(dev_info_t *dip, int nintrs);
uint_t	i_ddi_intr_get_current_nenables(dev_info_t *dip);
void	i_ddi_intr_set_current_nenables(dev_info_t *dip, int nintrs);
uint_t	i_ddi_intr_get_current_navail(dev_info_t *dip, int intr_type);
uint_t	i_ddi_intr_get_limit(dev_info_t *dip, int intr_type,
	    ddi_irm_pool_t *pool_p);

ddi_irm_pool_t	*i_ddi_intr_get_pool(dev_info_t *dip, int intr_type);

void	irm_init(void);
int	i_ddi_irm_insert(dev_info_t *dip, int intr_type, int count);
int	i_ddi_irm_modify(dev_info_t *dip, int nreq);
int	i_ddi_irm_remove(dev_info_t *dip);
void	i_ddi_irm_set_cb(dev_info_t *dip, boolean_t cb_flag);
int	i_ddi_irm_supported(dev_info_t *dip, int type);

ddi_intr_handle_t i_ddi_get_intr_handle(dev_info_t *dip, int inum);
void	i_ddi_set_intr_handle(dev_info_t *dip, int inum, ddi_intr_handle_t hdl);

ddi_intr_msix_t	*i_ddi_get_msix(dev_info_t *dip);
void	i_ddi_set_msix(dev_info_t *dip, ddi_intr_msix_t *msix_p);

#if defined(__i386) || defined(__amd64)
ddi_acc_handle_t	i_ddi_get_pci_config_handle(dev_info_t *dip);
void	i_ddi_set_pci_config_handle(dev_info_t *dip, ddi_acc_handle_t handle);
int	i_ddi_get_msi_msix_cap_ptr(dev_info_t *dip);
void	i_ddi_set_msi_msix_cap_ptr(dev_info_t *dip, int cap_ptr);
#endif

int32_t i_ddi_get_intr_weight(dev_info_t *);
int32_t i_ddi_set_intr_weight(dev_info_t *, int32_t);

void	i_ddi_alloc_intr_phdl(ddi_intr_handle_impl_t *);
void	i_ddi_free_intr_phdl(ddi_intr_handle_impl_t *);

extern	int irm_enable; /* global flag for IRM */

#define	DDI_INTR_ASSIGN_HDLR_N_ARGS(hdlp, func, arg1, arg2) \
	hdlp->ih_cb_func = func; \
	hdlp->ih_cb_arg1 = arg1; \
	hdlp->ih_cb_arg2 = arg2;

#ifdef DEBUG
#define	I_DDI_VERIFY_MSIX_HANDLE(hdlp)					\
	if ((hdlp->ih_type == DDI_INTR_TYPE_MSIX) &&			\
	    (hdlp->ih_flags & DDI_INTR_MSIX_DUP)) {			\
		ASSERT(hdlp->ih_dip == hdlp->ih_main->ih_dip);		\
		ASSERT(hdlp->ih_type == hdlp->ih_main->ih_type);	\
		ASSERT(hdlp->ih_vector == hdlp->ih_main->ih_vector);	\
		ASSERT(hdlp->ih_ver == hdlp->ih_main->ih_ver);		\
		ASSERT(hdlp->ih_cap == hdlp->ih_main->ih_cap);		\
		ASSERT(hdlp->ih_pri == hdlp->ih_main->ih_pri);		\
	}
#else
#define	I_DDI_VERIFY_MSIX_HANDLE(hdlp)
#endif

#else	/* _KERNEL */

typedef struct devinfo_intr devinfo_intr_t;

#endif	/* _KERNEL */

/*
 * Used only by old DDI interrupt interfaces.
 */

/*
 * This structure represents one interrupt possible from the given
 * device. It is used in an array for devices with multiple interrupts.
 */
struct intrspec {
	uint_t intrspec_pri;		/* interrupt priority */
	uint_t intrspec_vec;		/* vector # (0 if none) */
	uint_t (*intrspec_func)();	/* function to call for interrupt, */
					/* If (uint_t (*)()) 0, none. */
					/* If (uint_t (*)()) 1, then */
};

#ifdef _KERNEL

/*
 * Figure out how many FIXED nintrs are supported
 */
int	i_ddi_get_intx_nintrs(dev_info_t *dip);

/*
 * NOTE:
 *	The following 4 busops entry points are obsoleted with version
 *	9 or greater. Use i_ddi_intr_op interface in place of these
 *	obsolete interfaces.
 *
 *	Remove these busops entry points and all related data structures
 *	in future minor/major solaris release.
 */
typedef enum {DDI_INTR_CTLOPS_NONE} ddi_intr_ctlop_t;

/*
 * Interrupt get/set affinity functions
 */
int	get_intr_affinity(ddi_intr_handle_t h, processorid_t *tgt_p);
int	set_intr_affinity(ddi_intr_handle_t h, processorid_t tgt);

/* The following are obsolete interfaces */
ddi_intrspec_t	i_ddi_get_intrspec(dev_info_t *dip, dev_info_t *rdip,
	    uint_t inumber);

int	i_ddi_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	    ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	    ddi_idevice_cookie_t *idevice_cookiep,
	    uint_t (*int_handler)(caddr_t int_handler_arg),
	    caddr_t int_handler_arg, int kind);

void	i_ddi_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	    ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie);

int	i_ddi_intr_ctlops(dev_info_t *dip, dev_info_t *rdip,
	    ddi_intr_ctlop_t op, void *arg, void *val);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDI_INTR_IMPL_H */
