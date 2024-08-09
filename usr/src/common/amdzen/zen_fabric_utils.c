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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * A collection of utility functions for interacting with AMD Zen fabric and
 * APIC IDs.
 */

#include <asm/bitmap.h>
#include <sys/amdzen/ccd.h>

#include <io/amdzen/amdzen_client.h>

/*
 * Validate whether a fabric ID actually represents a valid ID for a given data
 * fabric.
 */
boolean_t
zen_fabric_id_valid_fabid(const df_fabric_decomp_t *decomp,
    const uint32_t fabid)
{
	uint32_t mask = decomp->dfd_node_mask | decomp->dfd_comp_mask;
	return ((fabid & ~mask) == 0);
}

/*
 * Validate whether the parts of a fabric ID (e.g. the socket, die, and
 * component) are in fact valid for a given data fabric.
 */
boolean_t
zen_fabric_id_valid_parts(const df_fabric_decomp_t *decomp, const uint32_t sock,
    const uint32_t die, const uint32_t comp)
{
	uint32_t node;

	if (((sock << decomp->dfd_sock_shift) & ~decomp->dfd_sock_mask) != 0) {
		return (B_FALSE);
	}
	if (((die << decomp->dfd_die_shift) & ~decomp->dfd_die_mask) != 0) {
		return (B_FALSE);
	}
	if ((comp & ~decomp->dfd_comp_mask) != 0) {
		return (B_FALSE);
	}

	node = die << decomp->dfd_die_shift;
	node |= sock << decomp->dfd_sock_shift;

	if (((node << decomp->dfd_node_shift) & ~decomp->dfd_node_mask) != 0) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Take apart a fabric ID into its constituent parts. The decomposition
 * information has the die and socket information relative to the node ID.
 */
void
zen_fabric_id_decompose(const df_fabric_decomp_t *decomp, const uint32_t fabid,
    uint32_t *sockp, uint32_t *diep, uint32_t *compp)
{
	uint32_t node;

	ASSERT(zen_fabric_id_valid_fabid(decomp, fabid));

	*compp = (fabid & decomp->dfd_comp_mask) >> decomp->dfd_comp_shift;
	node = (fabid & decomp->dfd_node_mask) >> decomp->dfd_node_shift;
	*diep = (node & decomp->dfd_die_mask) >> decomp->dfd_die_shift;
	*sockp = (node & decomp->dfd_sock_mask) >> decomp->dfd_sock_shift;
}

/*
 * Compose a fabric ID from its constituent parts: the socket, die, and fabric.
 */
void
zen_fabric_id_compose(const df_fabric_decomp_t *decomp, const uint32_t sock,
    const uint32_t die, const uint32_t comp, uint32_t *fabidp)
{
	uint32_t node;

	ASSERT(zen_fabric_id_valid_parts(decomp, sock, die, comp));

	node = die << decomp->dfd_die_shift;
	node |= sock << decomp->dfd_sock_shift;
	*fabidp = (node << decomp->dfd_node_shift) |
	    (comp << decomp->dfd_comp_shift);
}

#ifdef	DEBUG
static boolean_t
zen_apic_id_valid_parts(const amdzen_apic_decomp_t *decomp, const uint32_t sock,
    const uint32_t die, const uint32_t ccd, const uint32_t ccx,
    const uint32_t core, const uint32_t thread)
{
	ASSERT3U(decomp->aad_sock_shift, <, 32);
	ASSERT3U(decomp->aad_die_shift, <, 32);
	ASSERT3U(decomp->aad_ccd_shift, <, 32);
	ASSERT3U(decomp->aad_ccx_shift, <, 32);
	ASSERT3U(decomp->aad_core_shift, <, 32);
	ASSERT3U(decomp->aad_thread_shift, <, 32);

	if (((sock << decomp->aad_sock_shift) & ~decomp->aad_sock_mask) != 0) {
		return (B_FALSE);
	}

	if (((die << decomp->aad_die_shift) & ~decomp->aad_die_mask) != 0) {
		return (B_FALSE);
	}

	if (((ccd << decomp->aad_ccd_shift) & ~decomp->aad_ccd_mask) != 0) {
		return (B_FALSE);
	}

	if (((ccx << decomp->aad_ccx_shift) & ~decomp->aad_ccx_mask) != 0) {
		return (B_FALSE);
	}

	if (((core << decomp->aad_core_shift) & ~decomp->aad_core_mask) != 0) {
		return (B_FALSE);
	}

	if (((thread << decomp->aad_thread_shift) &
	    ~decomp->aad_thread_mask) != 0) {
		return (B_FALSE);
	}
	return (B_TRUE);
}
#endif	/* DEBUG */

/*
 * Compose an APIC ID from its constituent parts.
 */
void
zen_apic_id_compose(const amdzen_apic_decomp_t *decomp, const uint32_t sock,
    const uint32_t die, const uint32_t ccd, const uint32_t ccx,
    const uint32_t core, const uint32_t thread, uint32_t *apicid)
{
	uint32_t id;

	ASSERT(zen_apic_id_valid_parts(decomp, sock, die, ccd, ccx, core,
	    thread));
	id = thread << decomp->aad_thread_shift;
	id |= core << decomp->aad_core_shift;
	id |= ccx << decomp->aad_ccx_shift;
	id |= ccd << decomp->aad_ccd_shift;
	id |= die << decomp->aad_die_shift;
	id |= sock << decomp->aad_sock_shift;

	*apicid = id;
}

/*
 * Given a specific Zen3+ uarch and values from the INITPKG registers, calculate
 * the shift and mask values necessary to compose an APIC ID.
 */
void
zen_initpkg_to_apic(const uint32_t pkg0, const uint32_t pkg7,
    const x86_uarch_t uarch, amdzen_apic_decomp_t *apic)
{
	uint32_t nsock, nccd, nccx, ncore, nthr, extccx;
	uint32_t nsock_bits, nccd_bits, nccx_bits, ncore_bits, nthr_bits;

	ASSERT3U(uarch, >=, X86_UARCH_AMD_ZEN3);

	/*
	 * These are all 0 based values, meaning that we need to add one to each
	 * of them. However, we skip this because to calculate the number of
	 * bits to cover an entity we would subtract one.
	 */
	nthr = SCFCTP_PMREG_INITPKG0_GET_SMTEN(pkg0);
	ncore = SCFCTP_PMREG_INITPKG7_GET_N_CORES(pkg7);
	nccx = SCFCTP_PMREG_INITPKG7_GET_N_CCXS(pkg7);
	nccd = SCFCTP_PMREG_INITPKG7_GET_N_DIES(pkg7);
	nsock = SCFCTP_PMREG_INITPKG7_GET_N_SOCKETS(pkg7);

	if (uarch >= X86_UARCH_AMD_ZEN4) {
		extccx = SCFCTP_PMREG_INITPKG7_ZEN4_GET_16TAPIC(pkg7);
	} else {
		extccx = 0;
	}

	nthr_bits = highbit(nthr);
	ncore_bits = highbit(ncore);
	nccx_bits = highbit(nccx);
	nccd_bits = highbit(nccd);
	nsock_bits = highbit(nsock);

	apic->aad_thread_shift = 0;
	apic->aad_thread_mask = (1 << nthr_bits) - 1;

	apic->aad_core_shift = nthr_bits;
	if (ncore_bits > 0) {
		apic->aad_core_mask = (1 << ncore_bits) - 1;
		apic->aad_core_mask <<= apic->aad_core_shift;
	} else {
		apic->aad_core_mask = 0;
	}

	/*
	 * The APIC_16T_MODE bit indicates that the total shift to start the CCX
	 * should be at 4 bits if it's not. It doesn't mean that the CCX portion
	 * of the value should take up four bits. In the common Genoa case,
	 * nccx_bits will be zero.
	 */
	apic->aad_ccx_shift = apic->aad_core_shift + ncore_bits;
	if (extccx != 0 && apic->aad_ccx_shift < 4) {
		apic->aad_ccx_shift = 4;
	}
	if (nccx_bits > 0) {
		apic->aad_ccx_mask = (1 << nccx_bits) - 1;
		apic->aad_ccx_mask <<= apic->aad_ccx_shift;
	} else {
		apic->aad_ccx_mask = 0;
	}

	apic->aad_ccd_shift = apic->aad_ccx_shift + nccx_bits;
	if (nccd_bits > 0) {
		apic->aad_ccd_mask = (1 << nccd_bits) - 1;
		apic->aad_ccd_mask <<= apic->aad_ccd_shift;
	} else {
		apic->aad_ccd_mask = 0;
	}

	apic->aad_sock_shift = apic->aad_ccd_shift + nccd_bits;
	if (nsock_bits > 0) {
		apic->aad_sock_mask = (1 << nsock_bits) - 1;
		apic->aad_sock_mask <<= apic->aad_sock_shift;
	} else {
		apic->aad_sock_mask = 0;
	}

	/*
	 * Currently all supported Zen 2+ platforms only have a single die per
	 * socket as compared to Zen 1. So this is always kept at zero.
	 */
	apic->aad_die_mask = 0;
	apic->aad_die_shift = 0;
}

#ifdef	_KERNEL
/*
 * Attempt to determine what (supported) version of the data fabric we're on.
 *
 * An explicit version field was only added in DFv4.0, around the Zen 4
 * timeframe. That allows us to tell apart different versions of the DF register
 * set, most usefully when various subtypes were added.
 *
 * Older versions can theoretically be told apart based on usage of reserved
 * registers. We walk these in the following order, starting with the newest rev
 * and walking backwards to tell things apart:
 *
 *   o v3.5 -> Check function 1, register 0x150. This was reserved prior
 *             to this point. This is actually DF_FIDMASK0_V3P5. We are supposed
 *             to check bits [7:0].
 *
 *   o v3.0 -> Check function 1, register 0x208. The low byte (7:0) was
 *             changed to indicate a component mask. This is non-zero
 *             in the 3.0 generation. This is actually DF_FIDMASK_V2.
 *
 *   o v2.0 -> This is just the not that case. Presumably v1 wasn't part
 *             of the Zen generation.
 *
 * To support consumers with different register access constraints, the caller
 * is expected to provide a callback able to read the necessary DF registers.
 */
void
zen_determine_df_vers(const zen_df_read32_f df_read_f, const void *arg,
    uint8_t *df_major, uint8_t *df_minor, df_rev_t *df_rev)
{
	uint32_t val;
	uint8_t major, minor;
	df_rev_t rev;

	val = df_read_f(DF_FBICNT, arg);
	major = DF_FBICNT_V4_GET_MAJOR(val);
	minor = DF_FBICNT_V4_GET_MINOR(val);
	if (major == 0 && minor == 0) {
		val = df_read_f(DF_FIDMASK0_V3P5, arg);
		if (bitx32(val, 7, 0) != 0) {
			major = 3;
			minor = 5;
			rev = DF_REV_3P5;
		} else {
			val = df_read_f(DF_FIDMASK_V2, arg);
			if (bitx32(val, 7, 0) != 0) {
				major = 3;
				minor = 0;
				rev = DF_REV_3;
			} else {
				major = 2;
				minor = 0;
				rev = DF_REV_2;
			}
		}
	} else if (major == 4 && minor >= 2) {
		/*
		 * These are devices that have the newer memory layout that
		 * moves the DF::DramBaseAddress to 0x200. Please see the df.h
		 * theory statement for more information.
		 */
		rev = DF_REV_4D2;
	} else if (major == 4) {
		rev = DF_REV_4;
	} else {
		rev = DF_REV_UNKNOWN;
	}

	if (df_major != NULL)
		*df_major = major;
	if (df_minor != NULL)
		*df_minor = minor;
	if (df_rev != NULL)
		*df_rev = rev;
}
#endif	/* _KERNEL */
