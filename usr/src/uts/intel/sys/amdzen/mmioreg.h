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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_AMDZEN_MMIOREG_H
#define	_SYS_AMDZEN_MMIOREG_H

/*
 * Memory-mapped I/O (MMIO) register access infrastructure.  The purpose here is
 * to allow, as much as possible, easy access to MMIO registers in a manner
 * similar to that described in sys/amdzen/smn.h.  The main difference here is
 * that the SMN address space is not virtualised; it is simply a linear physical
 * space.  This forces us to provide additional functions and callers to take
 * additional steps to access registers in this manner.  Unlike SMN, however,
 * this method of access is compatible with existing DDI functionality on all
 * supported machine architectures.  Note that many functional units in current
 * AMD processors allow access to their registers by either SMN or MMIO, and it
 * is expressly intended that we make it possible to define such functional
 * units *once* and then access their registers by whichever method(s) may be
 * convenient for the caller.  To this end, we currently reuse smn_reg_def_t
 * here.  It is likely that this will eventually either become a single
 * reg_def_t or more a distinct type generated along with the SMN definitions
 * from a single input source.  Certainly it would be nice to move some of the
 * static logic to compile-time; much of the computation involves constants.
 *
 * The other essential goal here is to support access to these registers either
 * from the kernel or from DDI-compliant device drivers.  That means it needs to
 * be easy to obtain and use handles that can reference either the absolute
 * physical address of a register block or the offset from its base.  As leaf
 * drivers aren't aware of the absolute physical addresses, this also means we
 * want to define blocks in a way that drivers can make use of: for a given
 * functional unit, we want to consider a block the largest contiguous
 * collection of registers that provide related functionality.  It's ok if these
 * registers have multiple instances scattered over a region larger than a
 * single page, and even if the total set of registers is not contiguous within
 * that set of pages, so long as there's nothing *else* (no other functional
 * unit) contained within that address range.  Something like the NBIO/IOHC
 * registers where each functional unit has blocks of registers offset by (1 <<
 * 22) bytes won't work here, since each such address range contains multiple
 * different functional units that would not normally be shared by a single
 * device instance.  Fortunately, most MMIO-accessible register blocks don't
 * look like that, and are reasonably conducive to this approach.
 *
 * Although we want this to support either type of consumer, nothing here
 * provides any kind of locking, so the basic model is that a register might be
 * accessed by the kernel earlier in boot, then later by a device driver that
 * takes ownership of it.  When possible, we will likely want to use LDI or
 * similar means to access registers via a driver rather than doing so directly.
 * In addition to this basic type of access conflict that could arise, it's also
 * possible to have conflicting mappings onto the same physical page(s).  This
 * is safe as long as all the mappings have the same access type; we always go
 * for UC here which is overwhelmingly the most common used by drivers,
 * corresponding to DDI_STRICTORDER_ACC.  Our implementation (see
 * io/mmio_subr.c) always uses hat_devload() and does not allow the caller to
 * pass in any of the associated flags, reducing the likely scope for abuse.
 * There are, nevertheless, hazards here not only for mapping conflicts between
 * the DDI and non-DDI mechanisms but even among perfectly compliant DDI
 * consumers of ddi_regs_map_setup().  First, if two drivers have overlapping
 * "reg" properties, they can easily create mappings of differing types, but
 * this is almost certainly a bug in one of the ancestor nexi.  Second, even if
 * there are no overlaps, if "reg" regions handed out to two different drivers
 * (or even instances of the same driver) share a page, this type of conflict
 * can arise.  Obviously, in that scenario it becomes easier for this mechanism
 * to cause the same problem, particularly if one region of a page is used
 * exclusively by the kernel and other by a DDI-compliant driver.  The fact that
 * practically all driver mappings use STRICTORDER will eliminate potential
 * conflicts here, but perhaps more by accident than we might like.  One
 * possible solution to this that we can explore in the future would be to have
 * sub-page mappings shared at the parent nexus, or even at the root nexus, and
 * have bus_map() reject non-STRICTORDER requests (or squash them to
 * STRICTORDER) if the register space is smaller than a page.  This could be
 * used in conjunction with SPARC-like global use of busra to manage the address
 * space and prevent conflicts among multiple drivers.  If these mechanisms were
 * employed, we could hook into busra to guarantee ourselves exclusive use of
 * our mapping and/or to indicate our need for a STRICTORDER (UC) sub-page
 * mapping that rootnex could also hand out to other devices occupying parts of
 * the same page.  These mechanisms would not only address potential mapping
 * conflicts with kernel consumers but also with other drivers.  For now all of
 * this is left to the future as we do not anticipate conflicts, though sub-page
 * mappings are a very real problem.
 *
 * This functionality is considered experimental and should not be used outside
 * the amdzen-related subsystems.  For the moment it's considered machdep, so it
 * should be used only by oxide code or oxide-specific drivers.  Some or all of
 * it may be replaced or otherwise modified incompatibly.  This header should
 * not be delivered into the proto area.
 *
 * To use this:
 *
 * 1. Obtain a mapped register block descriptor.  There are two basic paths
 *    here, one for the kernel or other non-DDI consumers and one for DDI
 *    consumers.  Non-DDI consumers may require unit and sub-unit instance
 *    numbers, while DDI consumers must know the index into their preconfigured
 *    array of register blocks.  It is assumed that each "register number"
 *    belonging to a DDI consumer corresponds to a single lowest-level sub-unit
 *    within the functional unit.  Note: the non-DDI block mapping functions can
 *    sleep to allocate VA, and cannot fail.
 *
 *    Non-DDI:
 *
 * const mmio_reg_block_t bm = fch_misc_mmio_block();
 *
 *    DDI:
 *
 * mmio_reg_block_t block;
 * if (x_ddi_reg_block_setup(dip, 2, &attr, &block) != DDI_SUCCESS) {
 *	handle_ddi_failure...
 * }
 *
 * The block-lookup function may require arguments to identify the functional
 * sub-unit.
 *
 * 2. Get the instance of the register you want to access.  Note that even if a
 *    block refers to a single functional sub-unit, registers may still have
 *    multiple instances; those that do will require one or more iterators to
 *    identify a unique instance.
 *
 * mmio_reg_t reg = fch_misc_mmio_reg(block, D_FCH_PM_DECODEEN, 0);
 *
 * Alternately, use the instantiation macro named for the desired register:
 *
 * mmio_reg_t reg = FCH_PM_DECODEEN(block);
 *
 * If the register FCH::PM::DECODEEN had multiple instances within the block,
 * this would look, as it does for SMN, like:
 *
 * mmio_reg_t reg = FCH_PM_DECODEEN(block, instance);
 *
 * 3. Finally, the register can be accessed through the mmio_reg_t, which
 *    contains the register's size so you don't have to keep track of it.  The
 *    functions to read or write, like the SMN functions for now, are considered
 *    part of the outside infrastructure the consumer relies upon: the amdzen
 *    client library, the DDI, etc.  Since this is machdep for now, we
 *    have two functions, one for DDI consumers (an experimental machdep DDI
 *    extension) and one for kernel consumers.
 *
 *    Non-DDI:
 *
 * uint64_t val = mmio_reg_read(reg);
 * val = FCH_PM_DECODEEN_SET_IOAPICEN(reg, 1);
 * mmio_reg_write(reg, val);
 *
 *    DDI:
 *
 * uint64_t val = x_ddi_reg_get(reg);
 * val = FCH_PM_DECODEEN_SET_IOAPICEN(reg, 1);
 * x_ddi_reg_put(reg, val);
 *
 * 4. As mentioned above, after the first step has been completed, it's not
 *    necessary to repeat it for anything in the same block.  Unlike SMN
 *    registers, it's of course necessary to tear down the mapping when you've
 *    finished using the block.  Once again, we have separate routines for DDI
 *    and non-DDI consumers.
 *
 *    Non-DDI:
 *
 * mmio_reg_block_unmap(&block);
 *
 *    DDI:
 *
 * x_ddi_reg_block_free(&block);
 *
 * Note that while the non-DDI consumers must identify in advance the block of
 * registers they wish to map by invoking a block-specific function that may
 * require one or more unit identifiers, DDI consumers need only their DDI
 * register instance number.  In either case it is required that the consumer
 * know which physical block of registers their block handle refers to; this has
 * always been the case in the DDI and practically speaking it has also been
 * true for non-DDI consumers of e.g. i86devmap() or psm_map_phys().  We do
 * improve slightly on this by adding a runtime check for non-DDI consumers (a
 * Rust implementation would make these checks at compile time): each attempt to
 * instantiate a register within a block will assert that the block's unit
 * identifier matches the definition of that register.  There is currently no
 * good way to do the same for DDI consumers, because there is no source for the
 * block's unit identifier.  We could force the caller to pass the unit
 * identifier, but this would require a second block-lookup function for every
 * block, which would be a hassle for implementers.  One possible solution to
 * this is to introduce additional properties (or private data) to the DDI, so
 * that nexi would be expected to supply a set of mappings from each DDI
 * register instance property ("reg") onto the corresponding functional unit
 * identifier.  There are some significant namespacing challenges associated
 * with this, but it may be worth exploring.  For now, as it has been from time
 * immemorial, drivers are responsible for interpreting their register spaces
 * properly and there's not much help we can offer, though we can (and do)
 * perform some bounds-checking.
 *
 * On the implementation side, the intent is very similar to that for SMN: we
 * have a pair of inline functions for each functional unit to generate the
 * block and register instance objects.  The documentation in sys/amdzen/smn.h
 * along with the examples in the fch directory should be sufficient; in
 * addition, the function-generating macro MAKE_MMIO_REG_FN() is likely to be
 * serviceable for most needs, similar to AMDZEN_MAKE_SMN_REG_FN().
 *
 * Eventually, the disparate means of accessing registers should be merged
 * together and hidden from DDI consumers, and at least most of the types can
 * probably be merged for everyone.  It's not yet obvious where else we might go
 * from here; there are some intriguing possibilities around a system-wide
 * namespaced register dictionary that could provide easier access to
 * definitions of registers and blocks.  To my (still poorly-informed)
 * knowledge, no one is really doing anything like that in mainstream operating
 * systems.  It might look something like
 *
 * const devreg_block_desc_t d = {
 *     .dbd_scope = DBD_SCOPE_GLOBAL,
 *     .dbd_name = "FCH::GPIO",
 *     .dbd_units = DEVREG_BLOCK_UNITS(1, 6, 0, 0),
 *     .dbd_access = { .da_mech = DEVREG_ACC_MECH_SMN, .da_target = ... }
 * };
 * devreg_block_hdl_t *dbhp = devreg_block_hold(&d);
 * if (dbhp == NULL) {
 *     handle_failure...
 * }
 *
 * const devreg_t reg = devreg_inst(dbhp, "EGPIO42", 0);
 * uint64_t val = devreg_read(reg);
 * val = FCH_GPIO_SET_OUTPUT_VAL(val, FCH_GPIO_OUTPUT_HIGH);
 * val = FCH_GPIO_SET_OUTPUT_EN(val, 1);
 * devreg_write(reg, val);
 *
 * devreg_block_rele(&dbhp);
 *
 * DDI-compliant drivers might instead use
 *
 * const ddi_devreg_block_desc_t d = {
 *     .dbd_scope = DBD_SCOPE_DRIVER,
 *     .dbd_name = "FCH::GPIO",
 *     .dbd_access = { .da_mech = DEVREG_ACC_MECH_AUTO }
 * };
 * ddi_devreg_block_hdl_t dbhp;
 * if (ddi_devreg_block_setup(dip, &attr, &d, &dbhp) != DDI_SUCCESS) {
 *     handle_failure...
 * }
 * ...
 * ddi_devreg_block_free(&dbhp);
 *
 * It also remains to be done to incorporate access to registers via the legacy
 * IO mechanism (aka IO ports, {in,out}{b,w,l} instructions) into this somehow;
 * there are unfortunately quite a few registers accessible this way -- some of
 * them exclusively so, and often at relocatable port bases -- in the AMD
 * processors.
 *
 * For now, this...
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/dditypes.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>

/*
 * When instantiated by a non-DDI consumer, the members of this struct will come
 * from the block-lookup function specific to the particular functional unit.
 * For now, this isn't used for DDI consumers at all.  We could in principle get
 * the parent nexus to populate it, but DDI_MO_MAP_HANDLE doesn't quite get us
 * enough information (we lose the offset from the page base and the true size
 * of the block), and i_ddi_rnumber_to_regspec() assumes the mostly obsolete
 * 32-bit format "reg" property because the necessary ddi_map_op_t doesn't
 * currently exist.
 */
typedef struct mmio_reg_block_phys {
	paddr_t	mrbp_base;
	size_t	mrbp_len;
} mmio_reg_block_phys_t;

typedef enum mmio_reg_block_type {
	MRBF_NONE = 0,
	MRBF_DDI = 1,
	MRBF_KBM = 2,
	MRBF_HAT = 3
} mmio_reg_block_type_t;

/*
 * After mapping in the block, mrbm_va will point to the base of the block; we
 * don't currently support mapping registers into user space directly, but it's
 * certainly possible.  If this mapping was created by our DDI extension,
 * MRBF_DDI will be set in mrb_type, and mrb_acc will be a valid access handle.
 * Otherwise, mrb_phys will be filled in.  When MRBF_DDI is set, we also ignore
 * mrb_unit when instantiating register; otherwise, we will check that it
 * matches the definition's srd_unit value.  Additionally, when MRBF_DDI is set,
 * our read and write implementations will use ddi_{get,put}N(); otherwise we
 * use our generic mmio_reg_{read,write} routines.  It's unclear at this time
 * whether it would ever be useful to support for kernel consumers the more
 * cautious access mechanisms drivers can use to implement FMA, but for now we
 * do not.
 */
typedef struct mmio_reg_block {
	smn_unit_t			mrb_unit;
	mmio_reg_block_type_t		mrb_type;
	caddr_t				mrb_va;
	union {
		ddi_acc_handle_t	mrb_acc;
		mmio_reg_block_phys_t	mrb_phys;
	};
} mmio_reg_block_t;

/*
 * Create a mapping for this register block, suitable for accessing any instance
 * of any register contained in the functional unit to which the block refers.
 * This is a helper function intended for use by block-lookup functions called
 * by kernel consumers; DDI-compliant device drivers will use
 * x_ddi_reg_block_setup() instead.
 *
 * For now, this function always maps the block read-write (not execute), and
 * will sleep for VA space.  We may wish to generalise that if need arises.
 */
extern mmio_reg_block_t mmio_reg_block_map(const smn_unit_t,
    const mmio_reg_block_phys_t);

/*
 * Tear down a mapping created by mmio_reg_block_map(); called by non-DDI
 * consumers only.
 */
extern void mmio_reg_block_unmap(mmio_reg_block_t *);

/*
 * There's really only one practical difference between something accessible
 * over SMN and something that can be memory-mapped: an SMN register definition
 * allows for instances that span discontiguous pages.  For example, it's
 * perfectly valid for instances of an SMN-accessed register to reside at
 * locations separated by 1 << 20 bytes for each functional unit and a further 1
 * << 22 (or 16, etc) per sub-unit, and so forth.  We insist that each MMIO
 * block occupy contiguous pages, which requires factoring out the units
 * spanning larger spaces into blocks first.  All blocks that can be made from
 * these SMN register definitions need to satisfy this constraint so that the
 * 32-bit SMN address associated with each instance can be added to the
 * containing block's base physical address to obtain the correct address of the
 * contained register.
 */
typedef smn_reg_def_t mmio_reg_def_t;

/*
 * An instance of a memory-mapped register.  As with SMN, a bit of useful
 * metadata comes along for the ride.  mr_acc is NULL if this register is not
 * being accessed via the DDI.
 */
typedef struct mmio_reg {
	caddr_t			mr_va;
	ddi_acc_handle_t	mr_acc;
	uint8_t			mr_size;
} mmio_reg_t;

/*
 * Now the rather ugly third piece corresponding to our somewhat less tedious
 * smn_reg_t constructors: the caller has a mapped block and now wants to obtain
 * a handle to one of the register instances it contains.  This macro expands to
 * an inline function suitable for performing this transform.  We perform a
 * number of checks here, but we assume that the function responsible for
 * creating the mapping ensured that the block fits into the available address
 * space in its entirety, which greatly reduces what we have to worry about.
 */
#define	MAKE_MMIO_REG_FN(_fn, _unit, _defsz)				\
CTASSERT((_defsz) == 1 || (_defsz) == 2 || (_defsz) == 4 || (_defsz) == 8); \
static inline mmio_reg_t						\
_fn(const mmio_reg_block_t block, const mmio_reg_def_t def,		\
    const uint16_t reginst)						\
{									\
	uintptr_t va = (const uintptr_t)block.mrb_va;			\
	const uint32_t reginst32 = (const uint32_t)reginst;		\
									\
	const uint32_t nents = (def.srd_nents == 0) ? 1 :		\
	    (const uint32_t)def.srd_nents;				\
									\
	const uint8_t size = (def.srd_size == 0) ? (_defsz) :		\
	    (const uint8_t)def.srd_size;				\
	const uintptr_t stride = (def.srd_stride == 0) ?		\
	    (const uintptr_t)size : (const uintptr_t)def.srd_stride;	\
									\
	ASSERT3U(va, !=, 0);						\
	ASSERT(size == 1 || size == 2 || size == 4 || size == 8);	\
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_ ## _unit);			\
	ASSERT3U(nents, >, reginst32);					\
	ASSERT3U(size, <=, stride);					\
									\
	const uintptr_t reg = (const uintptr_t)def.srd_reg;		\
	const uintptr_t instoff = reginst32 * stride;			\
									\
	ddi_acc_handle_t hdl;						\
									\
	if (block.mrb_type != MRBF_DDI) {			\
		ASSERT3U(instoff, <, block.mrb_phys.mrbp_len);		\
		ASSERT3U(block.mrb_phys.mrbp_len - instoff, >=, size);	\
		ASSERT3S(block.mrb_unit, ==, def.srd_unit);		\
		hdl = NULL;						\
	} else {							\
		hdl = block.mrb_acc;					\
	}								\
									\
	ASSERT3U(UINTPTR_MAX - va, >=, reg);				\
	va += reg;							\
	ASSERT3U(UINTPTR_MAX - va, >=, instoff);			\
	va += instoff;							\
	ASSERT3U(UINTPTR_MAX - va, >=, size);				\
									\
	const mmio_reg_t inst = {					\
		.mr_va = (const caddr_t)va,				\
		.mr_acc = hdl,						\
		.mr_size = size						\
	};								\
									\
	return (inst);							\
}

/*
 * Prototypes for kernel consumers.  DDI consumers should not call these.
 */
extern uint64_t mmio_reg_read(const mmio_reg_t reg);
extern void mmio_reg_write(const mmio_reg_t reg, const uint64_t val);

/*
 * Experimental prototypes for DDI consumers.  They must include this header for
 * now.
 */
extern int x_ddi_reg_block_setup(dev_info_t *, uint_t, ddi_device_acc_attr_t *,
    mmio_reg_block_t *);
extern void x_ddi_reg_block_free(mmio_reg_block_t *);

extern uint64_t x_ddi_reg_get(const mmio_reg_t);
extern void x_ddi_reg_put(const mmio_reg_t, const uint64_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_MMIOREG_H */
