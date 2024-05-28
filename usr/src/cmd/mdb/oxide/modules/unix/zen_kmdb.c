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
 * This implements several dcmds for getting at state for use in kmdb. Several
 * of these kind of assume that someone else isn't doing something with them at
 * the same time that we are (mostly because there are only so many slots that
 * can be used for different purposes.
 */

#include <mdb/mdb_ctf.h>
#include <mdb/mdb_modapi.h>
#include <kmdb/kmdb_modext.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/sysmacros.h>
#include <sys/amdzen/ccx.h>
#include <sys/amdzen/umc.h>
#include <io/amdzen/amdzen.h>

#include "zen_kmdb_impl.h"

static uint64_t pcicfg_physaddr;
static boolean_t pcicfg_valid;

static df_props_t *df_props = NULL;

static boolean_t df_read32(uint8_t, const df_reg_def_t, uint32_t *);
static boolean_t df_read32_indirect(uint8_t, uint16_t, const df_reg_def_t,
    uint32_t *);

/*
 * Grabs the effective ComponentIDs for each component instance in the DF and
 * updates our ComponentID -> InstanceID mappings.
 */
static boolean_t
df_discover_comp_ids(uint8_t dfno)
{
	uint16_t inst_id;
	uint32_t fabric_id, comp_id;
	uint32_t finfo0, finfo3;

	for (size_t i = 0; i < df_props->dfp_comps_count; i++) {
		inst_id = df_props->dfp_comps[i].dc_inst;

		/*
		 * Skip components that we know have no FabricID.
		 */
		if (df_props->dfp_comps[i].dc_invalid_dest)
			continue;

		if (!df_read32_indirect(dfno, inst_id, DF_FBIINFO0, &finfo0) ||
		    !df_read32_indirect(dfno, inst_id, DF_FBIINFO3, &finfo3)) {
			mdb_warn("failed to FBIINFO0/3 for df %u inst %u\n",
			    dfno, inst_id);
			return (B_FALSE);
		}

		/*
		 * Skip components that are disabled.
		 */
		if (!DF_FBIINFO0_V3_GET_ENABLED(finfo0))
			continue;

		switch (df_props->dfp_rev) {
		case DF_REV_3:
			fabric_id = DF_FBIINFO3_V3_GET_BLOCKID(finfo3);
			break;
		case DF_REV_3P5:
			fabric_id = DF_FBIINFO3_V3P5_GET_BLOCKID(finfo3);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			fabric_id = DF_FBIINFO3_V4_GET_BLOCKID(finfo3);
			break;
		default:
			mdb_warn("unexpected DF revision: %u\n",
			    df_props->dfp_rev);
			return (B_FALSE);
		}

		uint32_t sock, die;
		zen_fabric_id_decompose(&df_props->dfp_decomp, fabric_id,
		    &sock, &die, &comp_id);
		ASSERT3U(sock, ==, (uint32_t)dfno);
		ASSERT0(die);
		ASSERT3U(comp_id, <, MAX_COMPS);

		/*
		 * Update the ComponentID -> InstanceID mapping.
		 */
		df_props->dfp_comp_map[dfno][comp_id] = inst_id;
	}

	return (B_TRUE);
}

/*
 * Called on module initialization to initialize `df_props`.
 */
boolean_t
df_props_init(void)
{
	GElf_Sym board_data_sym;
	uintptr_t board_data_addr;
	mdb_oxide_board_data_t board_data;
	x86_chiprev_t chiprev;
	df_reg_def_t fid0def, fid1def, fid2def;
	uint32_t fid0, fid1, fid2;
	df_fabric_decomp_t *decomp;

	if (df_props != NULL) {
		mdb_warn("df_props already initialized\n");
		return (B_TRUE);
	}

	/*
	 * We need to know what kind of system we're running on to figure out
	 * the appropriate registers, instance/component IDs, mappings, etc.
	 * Using the x86_chiprev routines/structures would be natural to use
	 * but given that this is a kmdb module, we're limited by the API
	 * surface.  Thankfully, we're already relatively constrained by the
	 * fact this is the oxide machine architecture and so we can assume
	 * that oxide_derive_platform() has already been run and populated the
	 * oxide_board_data global, which conveniently has the chiprev handy.
	 */

	if (mdb_lookup_by_name("oxide_board_data", &board_data_sym) != 0) {
		mdb_warn("failed to lookup oxide_board_data in target");
		return (B_FALSE);
	}
	if (GELF_ST_TYPE(board_data_sym.st_info) != STT_OBJECT) {
		mdb_warn("oxide_board_data symbol is not expected type: %u\n",
		    GELF_ST_TYPE(board_data_sym.st_info));
		return (B_FALSE);
	}

	if (mdb_vread(&board_data_addr, sizeof (board_data_addr),
	    (uintptr_t)board_data_sym.st_value) != sizeof (board_data_addr)) {
		mdb_warn("failed to read oxide_board_data addr from target");
		return (B_FALSE);
	}

	if (board_data_addr == 0) {
		mdb_warn("oxide_board_data is NULL\n");
		return (B_FALSE);
	}

	if (mdb_ctf_vread(&board_data, "oxide_board_data_t",
	    "mdb_oxide_board_data_t", board_data_addr, 0) != 0) {
		mdb_warn("failed to read oxide_board_data from target");
		return (B_FALSE);
	}

	chiprev = board_data.obd_cpuinfo.obc_chiprev;

	if (_X86_CHIPREV_VENDOR(chiprev) != X86_VENDOR_AMD) {
		mdb_warn("unsupported non-AMD system: %u\n",
		    _X86_CHIPREV_VENDOR(chiprev));
		return (B_FALSE);
	}

	switch (_X86_CHIPREV_FAMILY(chiprev)) {
	case X86_PF_AMD_MILAN:
		df_props = &df_props_milan;
		break;
	case X86_PF_AMD_GENOA:
		df_props = &df_props_genoa;
		break;
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		/*
		 * For the properties we care about, Turin and Dense Turin
		 * are the same.
		 */
		df_props = &df_props_turin;
		break;
	default:
		mdb_warn("unsupported AMD chiprev family: %u\n",
		    _X86_CHIPREV_FAMILY(chiprev));
		return (B_FALSE);
	}

	/*
	 * Now that we know what we're running on, we can grab the specific
	 * masks/shifts needed to (de)composing Fabric/Node/Component IDs.
	 */
	decomp = &df_props->dfp_decomp;

	switch (df_props->dfp_rev) {
	case DF_REV_3:
		fid0def = DF_FIDMASK0_V3;
		fid1def = DF_FIDMASK1_V3;
		/*
		 * DFv3 doesn't have a third mask register but for the sake
		 * of pulling out the common register read logic, we'll just
		 * set it to a valid register.  The read result won't be used.
		 */
		fid2def = DF_FIDMASK1_V3;
		break;
	case DF_REV_3P5:
		fid0def = DF_FIDMASK0_V3P5;
		fid1def = DF_FIDMASK1_V3P5;
		fid2def = DF_FIDMASK2_V3P5;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		fid0def = DF_FIDMASK0_V4;
		fid1def = DF_FIDMASK1_V4;
		fid2def = DF_FIDMASK2_V4;
		break;
	default:
		mdb_warn("unsupported DF revision: %u\n", df_props->dfp_rev);
		return (B_FALSE);
	}

	if (!df_read32(0, fid0def, &fid0) ||
	    !df_read32(0, fid1def, &fid1) ||
	    !df_read32(0, fid2def, &fid2)) {
		mdb_warn("failed to read masks register\n");
		return (B_FALSE);
	}

	switch (df_props->dfp_rev) {
	case DF_REV_3:
		decomp->dfd_sock_mask = DF_FIDMASK1_V3_GET_SOCK_MASK(fid1);
		decomp->dfd_die_mask = DF_FIDMASK1_V3_GET_DIE_MASK(fid1);
		decomp->dfd_node_mask = DF_FIDMASK0_V3_GET_NODE_MASK(fid0);
		decomp->dfd_comp_mask = DF_FIDMASK0_V3_GET_COMP_MASK(fid0);
		decomp->dfd_sock_shift = DF_FIDMASK1_V3_GET_SOCK_SHIFT(fid1);
		decomp->dfd_die_shift = 0;
		decomp->dfd_node_shift = DF_FIDMASK1_V3_GET_NODE_SHIFT(fid1);
		decomp->dfd_comp_shift = 0;
		break;
	case DF_REV_3P5:
	case DF_REV_4:
	case DF_REV_4D2:
		/*
		 * DFv3.5 and DFv4 have the same format in different registers.
		 */
		decomp->dfd_sock_mask = DF_FIDMASK2_V3P5_GET_SOCK_MASK(fid2);
		decomp->dfd_die_mask = DF_FIDMASK2_V3P5_GET_DIE_MASK(fid2);
		decomp->dfd_node_mask = DF_FIDMASK0_V3P5_GET_NODE_MASK(fid0);
		decomp->dfd_comp_mask = DF_FIDMASK0_V3P5_GET_COMP_MASK(fid0);
		decomp->dfd_sock_shift = DF_FIDMASK1_V3P5_GET_SOCK_SHIFT(fid1);
		decomp->dfd_die_shift = 0;
		decomp->dfd_node_shift = DF_FIDMASK1_V3P5_GET_NODE_SHIFT(fid1);
		decomp->dfd_comp_shift = 0;
		break;
	default:
		mdb_warn("unsupported DF revision: %u\n", df_props->dfp_rev);
		return (B_FALSE);
	}

	/*
	 * The FabricID/ComponentID -> InstanceID mapping is not static so we
	 * query and cache them in dfp_comp_map.  We'll use -1 as a sentinel
	 * for an invalid mapping.
	 */
	memset(df_props->dfp_comp_map, -1, sizeof (df_props->dfp_comp_map));

	/*
	 * We do this unconditionally for the first socket's IO die.
	 */
	if (!df_discover_comp_ids(0)) {
		mdb_warn("failed to discover ComponentIDs\n");
		return (B_FALSE);
	}

	/*
	 * And similarly for the second socket, if it exists (which we discover
	 * by trying a register read against it).
	 */
	if (!df_read32(1, DF_FBIINFO0, &fid0)) {
		mdb_warn("failed to read from second socket\n");
		return (B_FALSE);
	}

	if (fid0 != PCI_EINVAL32 && !df_discover_comp_ids(1)) {
		mdb_warn("failed to discover ComponentIDs on second socket\n");
		return (B_FALSE);
	}

	return (B_TRUE);
}

static const char *
df_comp_name(uint8_t dfno, uint32_t compid)
{
	if (dfno >= MAX_IO_DIES || compid >= MAX_COMPS)
		return (NULL);

	uint16_t instid = df_props->dfp_comp_map[dfno][compid];
	if (instid == (uint16_t)-1)
		return (NULL);

	const df_comp_t *df_comps = df_props->dfp_comps;
	for (size_t i = 0; i < df_props->dfp_comps_count; i++) {
		if (instid == df_comps[i].dc_inst) {
			return (df_comps[i].dc_name);
		}
	}

	return (NULL);
}

static uint_t
df_comp_ndram(uint16_t instid)
{
	const df_comp_t *df_comps = df_props->dfp_comps;
	for (size_t i = 0; i < df_props->dfp_comps_count; i++) {
		if (instid == df_comps[i].dc_inst) {
			return (df_comps[i].dc_ndram);
		}
	}

	return (0);
}

static boolean_t
df_get_smn_busno(uint8_t sock, uint8_t *busno)
{
	df_reg_def_t cfgdef;
	uint32_t df_busctl;

	switch (df_props->dfp_rev) {
	case DF_REV_3:
	case DF_REV_3P5:
		cfgdef = DF_CFG_ADDR_CTL_V2;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		cfgdef = DF_CFG_ADDR_CTL_V4;
		break;
	default:
		mdb_warn("unsupported DF revision: %u\n", df_props->dfp_rev);
		return (B_FALSE);
	}

	if (!df_read32(sock, cfgdef, &df_busctl)) {
		mdb_warn("failed to read DF config address\n");
		return (B_FALSE);
	}

	if (df_busctl == PCI_EINVAL32) {
		mdb_warn("got back PCI_EINVAL32 when reading from the df\n");
		return (B_FALSE);
	}

	*busno = DF_CFG_ADDR_CTL_GET_BUS_NUM(df_busctl);
	return (B_TRUE);
}

/*
 * Determine if MMIO configuration space is valid at this point. Once it is, we
 * store that fact and don't check again.
 */
static boolean_t
pcicfg_space_init(void)
{
	uint64_t msr;

	if (pcicfg_valid) {
		return (B_TRUE);
	}

	if (mdb_x86_rdmsr(MSR_AMD_MMIO_CFG_BASE_ADDR, &msr) != DCMD_OK) {
		mdb_warn("failed to read MSR_AMD_MMIOCFG_BASEADDR");
		return (B_FALSE);
	}

	if (AMD_MMIO_CFG_BASE_ADDR_GET_EN(msr) != 0) {
		pcicfg_physaddr = AMD_MMIO_CFG_BASE_ADDR_GET_ADDR(msr) <<
		    AMD_MMIO_CFG_BASE_ADDR_ADDR_SHIFT;
		pcicfg_valid = B_TRUE;
		return (B_TRUE);
	}

	mdb_warn("PCI config space is not currently enabled in the CPU\n");
	return (B_FALSE);
}

static boolean_t
pcicfg_validate(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg,
    uint8_t len)
{
	if (dev >= PCI_MAX_DEVICES) {
		mdb_warn("invalid pci device: %x\n", dev);
		return (B_FALSE);
	}

	/*
	 * We don't know whether the target uses ARI, but we need to accommodate
	 * the possibility that it does.  If it does not, we allow the
	 * possibility of an invalid function number with device 0.  Note that
	 * we also don't check the function number at all in that case because
	 * ARI allows function numbers up to 255 which is the entire range of
	 * the type we're using for func.  As this is supported only in kmdb, we
	 * really have no choice but to trust the user anyway.
	 */
	if (dev != 0 && func >= PCI_MAX_FUNCTIONS) {
		mdb_warn("invalid pci function: %x\n", func);
		return (B_FALSE);
	}

	if (reg >= PCIE_CONF_HDR_SIZE) {
		mdb_warn("invalid pci register: %x\n", reg);
		return (B_FALSE);
	}

	if (len != 1 && len != 2 && len != 4) {
		mdb_warn("invalid register length: %x\n", len);
		return (B_FALSE);
	}

	if (!IS_P2ALIGNED(reg, len)) {
		mdb_warn("register must be naturally aligned\n", reg);
		return (B_FALSE);
	}

	if (!pcicfg_space_init()) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static uint64_t
pcicfg_mkaddr(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg)
{
	return (pcicfg_physaddr + PCIE_CADDR_ECAM(bus, dev, func, reg));
}

static boolean_t
pcicfg_read(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint8_t len,
    uint32_t *val)
{
	ssize_t ret;
	uint64_t addr;

	if (!pcicfg_validate(bus, dev, func, reg, len)) {
		return (B_FALSE);
	}

	addr = pcicfg_mkaddr(bus, dev, func, reg);
	ret = mdb_pread(val, (size_t)len, addr);
	if (ret != len) {
		mdb_warn("failed to read %x/%x/%x reg 0x%x len %u",
		    bus, dev, func, reg, len);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
pcicfg_write(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint8_t len,
    uint32_t val)
{
	ssize_t ret;
	uint64_t addr;

	if (!pcicfg_validate(bus, dev, func, reg, len)) {
		return (B_FALSE);
	}

	if ((val & ~(0xffffffffU >> ((4 - len) << 3))) != 0) {
		mdb_warn("value 0x%x does not fit in %u bytes\n", val, len);
		return (B_FALSE);
	}

	addr = pcicfg_mkaddr(bus, dev, func, reg);
	ret = mdb_pwrite(&val, (size_t)len, addr);
	if (ret != len) {
		mdb_warn("failed to write %x/%x/%x reg 0x%x len %u",
		    bus, dev, func, reg, len);
		return (B_FALSE);
	}

	return (B_TRUE);
}

typedef enum pcicfg_rw {
	PCICFG_RD,
	PCICFG_WR
} pcicfg_rw_t;

static int
pcicfg_rw(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv,
    pcicfg_rw_t rw)
{
	u_longlong_t parse_val;
	uint32_t val = 0;
	uintptr_t len = 4;
	uint_t next_arg;
	uintptr_t bus, dev, func, off;
	boolean_t res;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	next_arg = mdb_getopts(argc, argv,
	    'L', MDB_OPT_UINTPTR, &len, NULL);

	if (argc - next_arg != (rw == PCICFG_RD ? 3 : 4)) {
		return (DCMD_USAGE);
	}

	bus = (uintptr_t)mdb_argtoull(&argv[next_arg++]);
	dev = (uintptr_t)mdb_argtoull(&argv[next_arg++]);
	func = (uintptr_t)mdb_argtoull(&argv[next_arg++]);
	if (rw == PCICFG_WR) {
		parse_val = mdb_argtoull(&argv[next_arg++]);
		if (parse_val > UINT32_MAX) {
			mdb_warn("write value must be a 32-bit quantity\n");
			return (DCMD_ERR);
		}
		val = (uint32_t)parse_val;
	}
	off = addr;

	if (bus > UINT8_MAX || dev > UINT8_MAX || func > UINT8_MAX ||
	    off > UINT16_MAX) {
		mdb_warn("b/d/f/r does not fit in 1/1/1/2 bytes\n");
		return (DCMD_ERR);
	}

	switch (rw) {
	case PCICFG_RD:
		res = pcicfg_read((uint8_t)bus, (uint8_t)dev, (uint8_t)func,
		    (uint16_t)off, (uint8_t)len, &val);
		break;
	case PCICFG_WR:
		res = pcicfg_write((uint8_t)bus, (uint8_t)dev, (uint8_t)func,
		    (uint16_t)off, (uint8_t)len, val);
		break;
	default:
		mdb_warn("internal error: unreachable PCI R/W type %d\n", rw);
		return (DCMD_ERR);
	}

	if (!res)
		return (DCMD_ERR);

	if (rw == PCICFG_RD) {
		mdb_printf("%llx\n", (u_longlong_t)val);
	}

	return (DCMD_OK);
}

int
rdpcicfg_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (pcicfg_rw(addr, flags, argc, argv, PCICFG_RD));
}

int
wrpcicfg_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (pcicfg_rw(addr, flags, argc, argv, PCICFG_WR));
}

static const char *dfhelp =
"%s a register %s the data fabric. The register is indicated by the address\n"
"of the dcmd. This can either be directed at a specific instance or be\n"
"broadcast to all instances. One of -b or -i inst is required. If no socket\n"
"(really the I/O die) is specified, then the first one will be selected. The\n"
"following options are supported:\n"
"\n"
"  -b		broadcast the I/O rather than direct it at a single function\n"
"  -f func	direct the I/O to the specified DF function\n"
"  -i inst	direct the I/O to the specified instance, otherwise use -b\n"
"  -s socket	direct the I/O to the specified I/O die, generally a socket\n";

void
rddf_dcmd_help(void)
{
	mdb_printf(dfhelp, "Read", "from");
}

void
wrdf_dcmd_help(void)
{
	mdb_printf(dfhelp, "Write", "to");
}

static int
df_dcmd_check(uintptr_t addr, uint_t flags, boolean_t inst_set, uintptr_t inst,
    boolean_t func_set, uintptr_t func, boolean_t sock_set, uintptr_t *sock,
    uint_t broadcast)
{
	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("a register must be specified via an address\n");
		return (DCMD_USAGE);
	} else if ((addr & ~(uintptr_t)df_props->dfp_reg_mask) != 0) {
		mdb_warn("invalid register: 0x%x, must be 4-byte aligned and "
		    "in-range\n", addr);
		return (DCMD_ERR);
	}

	if (sock_set) {
		/*
		 * We don't really know how many I/O dies there are in advance;
		 * however, the theoretical max is 8 (2P Naples with 4 dies);
		 * however, on the Oxide architecture there'll only ever be 2.
		 */
		if (*sock >= MAX_IO_DIES) {
			mdb_warn("invalid socket ID: %lu\n", *sock);
			return (DCMD_ERR);
		}
	} else {
		*sock = 0;
	}

	if (!func_set) {
		mdb_warn("-f is required\n");
		return (DCMD_ERR);
	} else if (func >= 8) {
		mdb_warn("only functions 0-7 are allowed: %lu\n", func);
		return (DCMD_ERR);
	}

	if (inst_set && inst > UINT16_MAX) {
		mdb_warn("specified instance out of range: %lu\n", inst);
		return (DCMD_ERR);
	}

	if ((!inst_set && !broadcast) ||
	    (inst_set && broadcast)) {
		mdb_warn("One of -i or -b must be set\n");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

static boolean_t
df_read32(uint8_t sock, const df_reg_def_t df, uint32_t *valp)
{
	return (pcicfg_read(0, 0x18 + sock, df.drd_func, df.drd_reg,
	    sizeof (*valp), valp));
}

static boolean_t
df_write32(uint8_t sock, const df_reg_def_t df, uint32_t val)
{
	return (pcicfg_write(0, 0x18 + sock, df.drd_func, df.drd_reg,
	    sizeof (val), val));
}

static boolean_t
df_write32_indirect_raw(uint8_t sock, uint16_t inst, uint8_t func,
    uint16_t reg, uint32_t val)
{
	df_reg_def_t ficaa;
	df_reg_def_t ficad;
	uint32_t rval = 0;

	rval = DF_FICAA_V2_SET_TARG_INST(rval, 1);
	rval = DF_FICAA_V2_SET_FUNC(rval, func);
	rval = DF_FICAA_V2_SET_INST(rval, inst);
	rval = DF_FICAA_V2_SET_64B(rval, 0);

	switch (df_props->dfp_rev) {
	case DF_REV_3:
	case DF_REV_3P5:
		ficaa = DF_FICAA_V2;
		ficad = DF_FICAD_LO_V2;
		rval = DF_FICAA_V2_SET_REG(rval, reg >> 2);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		ficaa = DF_FICAA_V4;
		ficad = DF_FICAD_LO_V4;
		rval = DF_FICAA_V4_SET_REG(rval, reg >> 2);
		break;
	default:
		mdb_warn("unsupported DF revision: %u\n", df_props->dfp_rev);
		return (B_FALSE);
	}

	if (!df_write32(sock, ficaa, rval)) {
		return (B_FALSE);
	}

	if (!df_write32(sock, ficad, val)) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
df_read32_indirect_raw(uint8_t sock, uint16_t inst, uint8_t func, uint16_t reg,
    uint32_t *valp)
{
	df_reg_def_t ficaa;
	df_reg_def_t ficad;
	uint32_t val = 0;

	val = DF_FICAA_V2_SET_TARG_INST(val, 1);
	val = DF_FICAA_V2_SET_FUNC(val, func);
	val = DF_FICAA_V2_SET_INST(val, inst);
	val = DF_FICAA_V2_SET_64B(val, 0);

	switch (df_props->dfp_rev) {
	case DF_REV_3:
	case DF_REV_3P5:
		ficaa = DF_FICAA_V2;
		ficad = DF_FICAD_LO_V2;
		val = DF_FICAA_V2_SET_REG(val, reg >> 2);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		ficaa = DF_FICAA_V4;
		ficad = DF_FICAD_LO_V4;
		val = DF_FICAA_V4_SET_REG(val, reg >> 2);
		break;
	default:
		mdb_warn("unsupported DF revision: %u\n", df_props->dfp_rev);
		return (B_FALSE);
	}

	if (!df_write32(sock, ficaa, val)) {
		return (B_FALSE);
	}

	if (!df_read32(sock, ficad, &val)) {
		return (B_FALSE);
	}

	*valp = val;
	return (B_TRUE);
}

static boolean_t
df_read32_indirect(uint8_t sock, uint16_t inst, const df_reg_def_t def,
    uint32_t *valp)
{
	if ((def.drd_gens & df_props->dfp_rev) == 0) {
		mdb_warn("asked to read DF reg with unsupported Gen: "
		    "func/reg: %u/0x%x, gens: 0x%x, dfp_rev: 0x%\n",
		    def.drd_func, def.drd_reg, def.drd_gens,
		    df_props->dfp_rev);
		return (B_FALSE);
	}

	return (df_read32_indirect_raw(sock, inst, def.drd_func, def.drd_reg, valp));
}

int
rddf_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t broadcast = FALSE;
	boolean_t inst_set = FALSE, func_set = FALSE, sock_set = FALSE;
	uintptr_t inst, func, sock;
	uint32_t val;
	int ret;

	if (mdb_getopts(argc, argv,
	    'b', MDB_OPT_SETBITS, TRUE, &broadcast,
	    'f', MDB_OPT_UINTPTR_SET, &func_set, &func,
	    'i', MDB_OPT_UINTPTR_SET, &inst_set, &inst,
	    's', MDB_OPT_UINTPTR_SET, &sock_set, &sock,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if ((ret = df_dcmd_check(addr, flags, inst_set, inst, func_set, func,
	    sock_set, &sock, broadcast)) != DCMD_OK) {
		return (ret);
	}

	/*
	 * For a broadcast read, read directly. Otherwise we need to use the
	 * FICAA register.
	 */
	if (broadcast) {
		if (!pcicfg_read(0, 0x18 + sock, func, addr, sizeof (val),
		    &val)) {
			return (DCMD_ERR);
		}
	} else {
		if (!df_read32_indirect_raw((uint8_t)sock, (uint16_t)inst, func, addr,
		    &val)) {
			return (DCMD_ERR);
		}
	}

	mdb_printf("%x\n", val);
	return (DCMD_OK);
}

int
wrdf_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t broadcast = FALSE;
	boolean_t inst_set = FALSE, func_set = FALSE, sock_set = FALSE;
	uintptr_t inst, func, sock;
	u_longlong_t parse_val;
	uint32_t val;
	int ret;

	if (mdb_getopts(argc, argv,
	    'b', MDB_OPT_SETBITS, TRUE, &broadcast,
	    'f', MDB_OPT_UINTPTR_SET, &func_set, &func,
	    'i', MDB_OPT_UINTPTR_SET, &inst_set, &inst,
	    's', MDB_OPT_UINTPTR_SET, &sock_set, &sock,
	    NULL) != argc - 1) {
		mdb_warn("missing required value to write\n");
		return (DCMD_USAGE);
	}

	parse_val = mdb_argtoull(&argv[argc - 1]);
	if (parse_val > UINT32_MAX) {
		mdb_warn("write value must be a 32-bit quantity\n");
		return (DCMD_ERR);
	}
	val = (uint32_t)parse_val;


	if ((ret = df_dcmd_check(addr, flags, inst_set, inst, func_set, func,
	    sock_set, &sock, broadcast)) != DCMD_OK) {
		return (ret);
	}

	if (broadcast) {
		if (!pcicfg_write(0, 0x18 + sock, func, addr, sizeof (val),
		    val)) {
			return (DCMD_ERR);
		}
	} else {
		if (!df_write32_indirect_raw((uint8_t)sock, (uint16_t)inst, func, addr,
		    val)) {
			return (DCMD_ERR);
		}
	}

	return (DCMD_OK);
}

static const char *smnhelp =
"%s a register %s the system management network (SMN). The address of the\n"
"dcmd is used to indicate the register to target. If no socket (really the\n"
"I/O die) is specified, then the first one will be selected. The NBIO\n"
"instance to use is determined based on what the DF indicates. The following\n"
"options are supported:\n"
"\n"
"  -L len	use access size {1,2,4} bytes, default 4\n"
"  -s socket	direct the I/O to the specified I/O die, generally a socket\n";

void
rdsmn_dcmd_help(void)
{
	mdb_printf(smnhelp, "Read", "from");
}

void
wrsmn_dcmd_help(void)
{
	mdb_printf(smnhelp, "Write", "to");
}

typedef enum smn_rw {
	SMN_RD,
	SMN_WR
} smn_rw_t;

static int
smn_rw_regdef(const smn_reg_t reg, uint8_t sock, smn_rw_t rw,
    uint32_t *smn_val)
{
	uint8_t smn_busno;
	boolean_t res;
	size_t len = SMN_REG_SIZE(reg);
	uint32_t addr = SMN_REG_ADDR(reg);

	if (!SMN_REG_SIZE_IS_VALID(reg)) {
		mdb_warn("invalid read length %lu (allowed: {1,2,4})\n", len);
		return (DCMD_ERR);
	}

	if (!SMN_REG_IS_NATURALLY_ALIGNED(reg)) {
		mdb_warn("address %x is not aligned on a %lu-byte boundary\n",
		    addr, len);
		return (DCMD_ERR);
	}

	if (rw == SMN_WR && !SMN_REG_VALUE_FITS(reg, *smn_val)) {
		mdb_warn("write value %lx does not fit in size %lu\n", *smn_val,
		    len);
		return (DCMD_ERR);
	}

	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);

	if (!df_get_smn_busno(sock, &smn_busno)) {
		mdb_warn("failed to get SMN bus number\n");
		return (DCMD_ERR);
	}

	if (!pcicfg_write(smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, sizeof (base_addr),
	    base_addr)) {
		mdb_warn("failed to write to IOHC SMN address register\n");
		return (DCMD_ERR);
	}

	switch (rw) {
	case SMN_RD:
		res = pcicfg_read(smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA + addr_off,
		    SMN_REG_SIZE(reg), smn_val);
		break;
	case SMN_WR:
		res = pcicfg_write(smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA + addr_off,
		    SMN_REG_SIZE(reg), *smn_val);
		break;
	default:
		mdb_warn("internal error: unreachable SMN R/W type %d\n", rw);
		return (DCMD_ERR);
	}

	if (!res) {
		mdb_warn("failed to read from IOHC SMN data register\n");
		return (DCMD_ERR);
	}

	return (DCMD_OK);

}

static int
smn_rw(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv,
    smn_rw_t rw)
{
	size_t len = 4;
	u_longlong_t parse_val;
	uint32_t smn_val = 0;
	uint64_t sock = 0;
	int ret;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("a register must be specified via an address\n");
		return (DCMD_USAGE);
	}

	if (mdb_getopts(argc, argv, 'L', MDB_OPT_UINTPTR, (uintptr_t *)&len,
	    's', MDB_OPT_UINT64, &sock, NULL) !=
	    ((rw == SMN_RD) ? argc : (argc - 1))) {
		return (DCMD_USAGE);
	}

	if (rw == SMN_WR) {
		parse_val = mdb_argtoull(&argv[argc - 1]);
		if (parse_val > UINT32_MAX) {
			mdb_warn("write value must be a 32-bit quantity\n");
			return (DCMD_ERR);
		}
		smn_val = (uint32_t)parse_val;
	}

	if (sock >= MAX_IO_DIES) {
		mdb_warn("invalid socket ID: %lu\n", sock);
		return (DCMD_ERR);
	}

	if (addr > UINT32_MAX) {
		mdb_warn("address %lx is out of range [0, 0xffffffff]\n", addr);
		return (DCMD_ERR);
	}

	const smn_reg_t reg = SMN_MAKE_REG_SIZED(addr, len);

	ret = smn_rw_regdef(reg, (uint8_t)sock, rw, &smn_val);
	if (ret != DCMD_OK) {
		return (ret);
	}

	if (rw == SMN_RD) {
		mdb_printf("%x\n", smn_val);
	}

	return (DCMD_OK);
}

static int
rdmsn_regdef(const smn_reg_t reg, uint8_t sock, uint32_t *val)
{
	return (smn_rw_regdef(reg, sock, SMN_RD, val));
}

int
rdsmn_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (smn_rw(addr, flags, argc, argv, SMN_RD));
}

int
wrsmn_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (smn_rw(addr, flags, argc, argv, SMN_WR));
}

/*
 * Given a DF fabric ID (critically not an instance ID), print information
 * about that.
 */
static void
df_print_dest(uint32_t dest)
{
	uint32_t sock, die, comp;
	const char *name;

	zen_fabric_id_decompose(&df_props->dfp_decomp, dest, &sock, &die,
	    &comp);
	ASSERT3U(sock, <, MAX_IO_DIES);
	ASSERT0(die);

	name = df_comp_name((uint8_t)sock, comp);

	mdb_printf("%#x (%#x/%#x)", dest, sock, comp);
	if (name != NULL) {
		mdb_printf(" -- %s", name);
	}
}

static const char *df_route_help =
"Print out routing rules in the data fabric. This currently supports reading\n"
"the PCI bus, I/O port, MMIO, and DRAM routing rules. These values can vary,\n"
"especially with DRAM, from instance to instance. All route entries of a\n"
"given type are printed. Where possible, we will select a default instance to\n"
"use for this. The following options are used to specify the type of routing\n"
"entries to print:\n"
"  -b           print PCI bus routing entries\n"
"  -d           print DRAM routing entries\n"
"  -I           print I/O port entries\n"
"  -m           print MMIO routing entries\n"
"\n"
"The following options are used to control which instance to print from\n"
"  -i inst	print entries from the specified instance\n"
"  -s socket	print entries from the specified I/O die, generally a socket\n"
"\n"
"The following letters are used in the rather terse FLAGS output:\n"
"\n"
"    R		Read Enabled (PCI Bus, I/O Ports, MMIO)\n"
"    W		Write Enabled (PCI Bus, I/O Ports, MMIO)\n"
"    I		ISA Shenanigans (I/O ports)\n"
"    N		Non-posted mode (MMIO)\n"
"    C		CPU redirected to compat addresses (MMIO)\n"
"    B		Break Bus lock (DRAM)\n"
"    H		MMIO Hole Enabled (DRAM)\n"
"    V		Rule Valid (DRAM)\n";

void
df_route_dcmd_help(void)
{
	mdb_printf(df_route_help);
}

static void
df_cfgmap(const df_rev_t df_rev, const uint32_t reg1, const uint32_t reg2,
    uint32_t *base, uint32_t *limit, uint32_t *dest, boolean_t *re,
    boolean_t *we)
{
	switch (df_rev) {
	case DF_REV_3:
		*base = DF_CFGMAP_V2_GET_BUS_BASE(reg1);
		*limit = DF_CFGMAP_V2_GET_BUS_LIMIT(reg1);
		*dest = DF_CFGMAP_V3_GET_DEST_ID(reg1);
		*re = DF_CFGMAP_V2_GET_RE(reg1);
		*we = DF_CFGMAP_V2_GET_WE(reg1);
		break;
	case DF_REV_3P5:
		*base = DF_CFGMAP_V2_GET_BUS_BASE(reg1);
		*limit = DF_CFGMAP_V2_GET_BUS_LIMIT(reg1);
		*dest = DF_CFGMAP_V3P5_GET_DEST_ID(reg1);
		*re = DF_CFGMAP_V2_GET_RE(reg1);
		*we = DF_CFGMAP_V2_GET_WE(reg1);
		break;
	case DF_REV_4:
		*base = DF_CFGMAP_BASE_V4_GET_BASE(reg1);
		*limit = DF_CFGMAP_LIMIT_V4_GET_LIMIT(reg2);
		*dest = DF_CFGMAP_LIMIT_V4_GET_DEST_ID(reg2);
		*re = DF_CFGMAP_BASE_V4_GET_RE(reg1);
		*we = DF_CFGMAP_BASE_V4_GET_WE(reg1);
		break;
	case DF_REV_4D2:
		*base = DF_CFGMAP_BASE_V4_GET_BASE(reg1);
		*limit = DF_CFGMAP_LIMIT_V4_GET_LIMIT(reg2);
		*dest = DF_CFGMAP_LIMIT_V4D2_GET_DEST_ID(reg2);
		*re = DF_CFGMAP_BASE_V4_GET_RE(reg1);
		*we = DF_CFGMAP_BASE_V4_GET_WE(reg1);
		break;
	default:
		mdb_warn("unexpected DF revision: %u\n", df_rev);
		return;
	}
}

static int
df_route_buses(uint_t flags, uint8_t sock, uint16_t inst)
{
	df_reg_def_t def1, def2;
	uint32_t reg1, reg2;
	uint32_t base, limit, dest;
	boolean_t re, we;

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-7s %-8s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINATION");
	}

	for (uint_t i = 0; i < df_props->dfp_max_cfgmap; i++) {
		switch (df_props->dfp_rev) {
		case DF_REV_3:
		case DF_REV_3P5:
			def1 = DF_CFGMAP_V2(i);
			/*
			 * These revisions only use a single register but for
			 * the sake of factoring out the register read logic,
			 * we'll read the same register twice.
			 */
			def2 = def1;
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			def1 = DF_CFGMAP_BASE_V4(i);
			def2 = DF_CFGMAP_LIMIT_V4(i);
			break;
		default:
			mdb_warn("unsupported DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}

		if (!df_read32_indirect(sock, inst, def1, &reg1)) {
			mdb_warn("failed to read cfgmap base %u\n", i);
			continue;
		}
		if (reg1 == PCI_EINVAL32) {
			mdb_warn("got back invalid read for cfgmap base %u\n",
			    i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, def2, &reg2)) {
			mdb_warn("failed to read cfgmap limit %u\n", i);
			continue;
		}
		if (reg2 == PCI_EINVAL32) {
			mdb_warn("got back invalid read for cfgmap limit %u\n",
			    i);
			continue;
		}

		df_cfgmap(df_props->dfp_rev, reg1, reg2, &base, &limit, &dest,
		    &re, &we);

		mdb_printf("%-7#x %-7#x %c%c       ",
		    base, limit, re ? 'R' : '-', we ? 'W' : '-');
		df_print_dest(dest);
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static void
df_dram_rule(const df_rev_t df_rev, const uint32_t *regs, const uint_t nreg,
    uint64_t *base, uint64_t *limit, uint16_t *chan_ilv, uint16_t *addr_ilv,
    uint16_t *die_ilv, uint16_t *sock_ilv, boolean_t *valid, boolean_t *hole,
    boolean_t *busbreak, uint32_t *dest)
{
	uint32_t breg, lreg, ireg, creg;

	switch (df_rev) {
	case DF_REV_3:
	case DF_REV_3P5:
		if (nreg != 2) {
			mdb_warn("unexpected number of DRAM registers: %u\n",
			    nreg);
			return;
		}
		breg = regs[0];
		lreg = regs[1];

		*base = DF_DRAM_BASE_V2_GET_BASE(breg) <<
		    DF_DRAM_BASE_V2_BASE_SHIFT;
		*limit = (DF_DRAM_LIMIT_V2_GET_LIMIT(lreg) <<
		    DF_DRAM_LIMIT_V2_LIMIT_SHIFT) + (DF_DRAM_LIMIT_V2_LIMIT_EXCL
		    - 1);

		*valid = DF_DRAM_BASE_V2_GET_VALID(breg);
		*hole = DF_DRAM_BASE_V2_GET_HOLE_EN(breg);

		switch (df_rev) {
		case DF_REV_3:
			*addr_ilv = DF_DRAM_BASE_V3_GET_ILV_ADDR(breg);
			*chan_ilv = DF_DRAM_BASE_V3_GET_ILV_CHAN(breg);
			*die_ilv = DF_DRAM_BASE_V3_GET_ILV_DIE(breg);
			*sock_ilv = DF_DRAM_BASE_V3_GET_ILV_SOCK(breg);
			*dest = DF_DRAM_LIMIT_V3_GET_DEST_ID(lreg);
			*busbreak = DF_DRAM_LIMIT_V3_GET_BUS_BREAK(lreg);
			break;
		case DF_REV_3P5:
			*addr_ilv = DF_DRAM_BASE_V3P5_GET_ILV_ADDR(breg);
			*chan_ilv = DF_DRAM_BASE_V3P5_GET_ILV_CHAN(breg);
			*die_ilv = DF_DRAM_BASE_V3P5_GET_ILV_DIE(breg);
			*sock_ilv = DF_DRAM_BASE_V3P5_GET_ILV_SOCK(breg);
			*dest = DF_DRAM_LIMIT_V3P5_GET_DEST_ID(lreg);
			*busbreak = B_FALSE;
			break;
		default:
			mdb_warn("unexpected DF revision: %u\n", df_rev);
			return;
		}
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		if (nreg != 4) {
			mdb_warn("unexpected number of DRAM registers: %u\n",
			    nreg);
			return;
		}
		breg = regs[0];
		lreg = regs[1];
		ireg = regs[2];
		creg = regs[3];

		*base = (uint64_t)DF_DRAM_BASE_V4_GET_ADDR(breg) <<
		    DF_DRAM_BASE_V4_BASE_SHIFT;
		*limit = ((uint64_t)DF_DRAM_LIMIT_V4_GET_ADDR(lreg) <<
		    DF_DRAM_LIMIT_V4_LIMIT_SHIFT) + (DF_DRAM_LIMIT_V4_LIMIT_EXCL
		    - 1);

		*chan_ilv = (df_rev == DF_REV_4) ?
		    DF_DRAM_ILV_V4_GET_CHAN(ireg) :
		    DF_DRAM_ILV_V4D2_GET_CHAN(ireg);
		*addr_ilv = DF_DRAM_ILV_V4_GET_ADDR(ireg);
		*die_ilv = DF_DRAM_ILV_V4_GET_DIE(ireg);
		*sock_ilv = DF_DRAM_ILV_V4_GET_SOCK(ireg);

		*valid = DF_DRAM_CTL_V4_GET_VALID(creg);
		*hole = DF_DRAM_CTL_V4_GET_HOLE_EN(creg);
		*busbreak = B_FALSE;

		*dest = (df_rev == DF_REV_4) ?
		    DF_DRAM_CTL_V4_GET_DEST_ID(creg) :
		    DF_DRAM_CTL_V4D2_GET_DEST_ID(creg);

		break;
	default:
		mdb_warn("unexpected DF revision: %u\n", df_rev);
		return;
	}
}

static int
df_route_dram(uint_t flags, uint8_t sock, uint16_t inst)
{
	uint_t ndram;

	if ((ndram = df_comp_ndram(inst)) == 0) {
		mdb_warn("component 0x%x has no DRAM rules\n", inst);
		return (DCMD_ERR);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-?s %-?s %-7s %-21s %s\n", "BASE", "LIMIT",
		    "FLAGS", "INTERLEAVE", "DESTINATION");
	}

	for (uint_t i = 0; i < ndram; i++) {
		uint_t nreg;
		df_reg_def_t defs[4];
		uint32_t regs[4];
		uint64_t base, limit;
		const char *chan;
		char ileave[22];
		uint16_t addr_ileave, chan_ileave, die_ileave, sock_ileave;
		boolean_t valid, hole, busbreak;
		uint32_t dest;

		switch (df_props->dfp_rev) {
		case DF_REV_3:
		case DF_REV_3P5:
			nreg = 2;
			defs[0] = DF_DRAM_BASE_V2(i);
			defs[1] = DF_DRAM_LIMIT_V2(i);
			break;
		case DF_REV_4:
			nreg = 4;
			defs[0] = DF_DRAM_BASE_V4(i);
			defs[1] = DF_DRAM_LIMIT_V4(i);
			defs[2] = DF_DRAM_ILV_V4(i);
			defs[3] = DF_DRAM_CTL_V4(i);
			break;
		case DF_REV_4D2:
			nreg = 4;
			defs[0] = DF_DRAM_BASE_V4D2(i);
			defs[1] = DF_DRAM_LIMIT_V4D2(i);
			defs[2] = DF_DRAM_ILV_V4D2(i);
			defs[3] = DF_DRAM_CTL_V4D2(i);
			break;
		default:
			mdb_warn("unexpected DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}

		for (uint_t r = 0; r < nreg; r++) {
			if (!df_read32_indirect(sock, inst, defs[r],
			    &regs[r])) {
				mdb_warn("failed to read DRAM register %x"
				    "port %u\n", defs[r].drd_reg, i);
				return (DCMD_ERR);
			}
		}

		df_dram_rule(df_props->dfp_rev, regs, nreg, &base, &limit,
		    &chan_ileave, &addr_ileave, &die_ileave, &sock_ileave,
		    &valid, &hole, &busbreak, &dest);

		if (chan_ileave >= df_props->dfp_chan_ileaves_count) {
			mdb_warn("DRAM channel interleaving index %u out of \
			    range\n", chan_ileave);
			return (DCMD_ERR);
		}
		chan = (df_props->dfp_chan_ileaves[chan_ileave] == NULL) ?
		    "Reserved" : df_props->dfp_chan_ileaves[chan_ileave];

		(void) mdb_snprintf(ileave, sizeof (ileave), "%u/%s/%u/%u",
		    DF_DRAM_ILV_ADDR_BASE + addr_ileave, chan, die_ileave + 1,
		    sock_ileave + 1);
		mdb_printf("%-?#lx %-?#lx %c%c%c     %-21s ", base, limit,
		    valid ? 'V' : '-', hole ? 'H' : '-', busbreak ? 'B' : '-',
		    ileave);
		df_print_dest(dest);
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
df_route_ioports(uint_t flags, uint8_t sock, uint16_t inst)
{
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-10s %-10s %-6s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINATION");
	}

	for (uint_t i = 0; i < DF_MAX_IO_RULES; i++) {
		df_reg_def_t bdef, ldef;
		uint32_t breg, lreg, base, limit;
		uint32_t dest;

		switch (df_props->dfp_rev) {
		case DF_REV_3:
		case DF_REV_3P5:
			bdef = DF_IO_BASE_V2(i);
			ldef = DF_IO_LIMIT_V2(i);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			bdef = DF_IO_BASE_V4(i);
			ldef = DF_IO_LIMIT_V4(i);
			break;
		default:
			mdb_warn("unsupported DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}

		if (!df_read32_indirect(sock, inst, bdef, &breg)) {
			mdb_warn("failed to read I/O port base %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, ldef, &lreg)) {
			mdb_warn("failed to read I/O port limit %u\n", i);
			continue;
		}

		switch (df_props->dfp_rev) {
		case DF_REV_3:
		case DF_REV_3P5:
			base = DF_IO_BASE_V2_GET_BASE(breg);
			limit = DF_IO_LIMIT_V2_GET_LIMIT(lreg);
			dest = DF_IO_LIMIT_V2_GET_DEST_ID(lreg);
			break;
		case DF_REV_4:
			base = DF_IO_BASE_V4_GET_BASE(breg);
			limit = DF_IO_LIMIT_V4_GET_LIMIT(lreg);
			dest = DF_IO_LIMIT_V4_GET_DEST_ID(lreg);
			break;
		case DF_REV_4D2:
			base = DF_IO_BASE_V4_GET_BASE(breg);
			limit = DF_IO_LIMIT_V4_GET_LIMIT(lreg);
			dest = DF_IO_LIMIT_V4D2_GET_DEST_ID(lreg);
			break;
		default:
			mdb_warn("unsupported DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}
		base <<= DF_IO_BASE_SHIFT;
		limit <<= DF_IO_LIMIT_SHIFT;
		limit += DF_IO_LIMIT_EXCL - 1;

		/* The RE/WE/IE fields are the same across supported DF revs. */
		mdb_printf("%-10#x %-10#x %c%c%c    ", base, limit,
		    DF_IO_BASE_V2_GET_RE(breg) ? 'R' : '-',
		    DF_IO_BASE_V2_GET_WE(breg) ? 'W' : '-',
		    DF_IO_BASE_V2_GET_IE(breg) ? 'I' : '-');
		df_print_dest(dest);
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
df_route_mmio(uint_t flags, uint8_t sock, uint16_t inst)
{
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-?s %-?s %-8s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINATION");
	}

	for (uint_t i = 0; i < DF_MAX_MMIO_RULES; i++) {
		df_reg_def_t bdef, ldef, cdef;
		uint32_t breg, lreg, creg, ereg;
		uint64_t base, limit;
		boolean_t np;
		uint32_t dest;

		switch (df_props->dfp_rev) {
		case DF_REV_3:
		case DF_REV_3P5:
			bdef = DF_MMIO_BASE_V2(i);
			ldef = DF_MMIO_LIMIT_V2(i);
			cdef = DF_MMIO_CTL_V2(i);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			bdef = DF_MMIO_BASE_V4(i);
			ldef = DF_MMIO_LIMIT_V4(i);
			cdef = DF_MMIO_CTL_V4(i);
			break;
		default:
			mdb_warn("unsupported DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}

		if (!df_read32_indirect(sock, inst, bdef, &breg)) {
			mdb_warn("failed to read MMIO base %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, ldef, &lreg)) {
			mdb_warn("failed to read MMIO limit %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, cdef, &creg)) {
			mdb_warn("failed to read MMIO control %u\n", i);
			continue;
		}

		const df_reg_def_t edef = DF_MMIO_EXT_V4(i);
		if ((df_props->dfp_rev & DF_REV_ALL_4) &&
		    !df_read32_indirect(sock, inst, edef, &ereg)) {
			mdb_warn("failed to read MMIO ext %u\n", i);
			continue;
		}

		base = (uint64_t)breg << DF_MMIO_SHIFT;
		limit = (uint64_t)lreg << DF_MMIO_SHIFT;

		switch (df_props->dfp_rev) {
		case DF_REV_3:
			np = DF_MMIO_CTL_V3_GET_NP(creg);
			dest = DF_MMIO_CTL_V3_GET_DEST_ID(creg);
			break;
		case DF_REV_3P5:
			np = DF_MMIO_CTL_V3_GET_NP(creg);
			dest = DF_MMIO_CTL_V3P5_GET_DEST_ID(creg);
			break;
		case DF_REV_4:
			base |= (uint64_t)DF_MMIO_EXT_V4_GET_BASE(ereg)
			    << DF_MMIO_EXT_SHIFT;
			limit |= (uint64_t)DF_MMIO_EXT_V4_GET_LIMIT(ereg)
			    << DF_MMIO_EXT_SHIFT;

			np = DF_MMIO_CTL_V4_GET_NP(creg);
			dest = DF_MMIO_CTL_V4_GET_DEST_ID(creg);
			break;
		case DF_REV_4D2:
			base |= (uint64_t)DF_MMIO_EXT_V4_GET_BASE(ereg)
			    << DF_MMIO_EXT_SHIFT;
			limit |= (uint64_t)DF_MMIO_EXT_V4_GET_LIMIT(ereg)
			    << DF_MMIO_EXT_SHIFT;

			np = DF_MMIO_CTL_V4_GET_NP(creg);
			dest = DF_MMIO_CTL_V4D2_GET_DEST_ID(creg);
			break;
		default:
			mdb_warn("unsupported DF revision: %u\n",
			    df_props->dfp_rev);
			return (DCMD_ERR);
		}
		limit += DF_MMIO_LIMIT_EXCL - 1;

		mdb_printf("%-?#lx %-?#lx %c%c%c%c     ", base, limit,
		    DF_MMIO_CTL_GET_RE(creg) ? 'R' : '-',
		    DF_MMIO_CTL_GET_WE(creg) ? 'W' : '-',
		    np ? 'N' : '-', DF_MMIO_CTL_GET_CPU_DIS(creg) ? 'C' : '-');
		df_print_dest(dest);
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

int
df_route_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint64_t sock = 0;
	uintptr_t inst;
	boolean_t inst_set = B_FALSE;
	uint_t opt_b = FALSE, opt_d = FALSE, opt_I = FALSE, opt_m = FALSE;
	uint_t count = 0;

	if (mdb_getopts(argc, argv,
	    'b', MDB_OPT_SETBITS, TRUE, &opt_b,
	    'd', MDB_OPT_SETBITS, TRUE, &opt_d,
	    'I', MDB_OPT_SETBITS, TRUE, &opt_I,
	    'm', MDB_OPT_SETBITS, TRUE, &opt_m,
	    's', MDB_OPT_UINT64, &sock,
	    'i', MDB_OPT_UINTPTR_SET, &inst_set, &inst, NULL) != argc) {
		return (DCMD_USAGE);
	}

	if ((flags & DCMD_ADDRSPEC) != 0) {
		mdb_warn("df_route does not support addresses\n");
		return (DCMD_USAGE);
	}

	if (opt_b) {
		count++;
	}
	if (opt_d)
		count++;
	if (opt_I)
		count++;
	if (opt_m)
		count++;

	if (count == 0) {
		mdb_warn("one of -b, -d, -I, and -m must be specified\n");
		return (DCMD_ERR);
	} else if (count > 1) {
		mdb_warn("only one of -b -d, -I, and -m may be specified\n");
		return (DCMD_ERR);
	}

	if (sock >= MAX_IO_DIES) {
		mdb_warn("invalid socket ID: %lu\n", sock);
		return (DCMD_ERR);
	}

	if (!inst_set) {
		if (opt_d || opt_I) {
			inst = df_props->dfp_dram_io_inst;
		} else {
			inst = df_props->dfp_mmio_pci_inst;
		}
	} else if (inst > UINT16_MAX) {
		mdb_warn("specified instance out of range: %lu\n", inst);
		return (DCMD_ERR);
	}


	if (opt_d) {
		return (df_route_dram(flags, (uint8_t)sock, (uint16_t)inst));
	} else if (opt_b) {
		return (df_route_buses(flags, (uint8_t)sock, (uint16_t)inst));
	} else if (opt_I) {
		return (df_route_ioports(flags, (uint8_t)sock, (uint16_t)inst));
	} else {
		return (df_route_mmio(flags, (uint8_t)sock, (uint16_t)inst));
	}

	return (DCMD_OK);
}

static const char *dimmhelp =
"Print a summary of DRAM training for each channel on the SoC. This uses the\n"
"UMC::CH::UmcConfig Ready bit to determine whether or not the channel\n"
"trained. Separately, there is a column indicating whether there is a DIMM\n"
"installed in each location in the channel. A 1 DPC system will always show\n"
"DIMM 1 missing. The following columns will be output:\n"
"\n"
"CHAN:\t\tIndicates the socket and board channel letter\n"
"UMC:\t\tIndicates the UMC instance\n"
"TRAIN:\tIndicates whether or not training completed successfully\n"
"DIMM 0:\tIndicates whether DIMM 0 in the channel is present\n"
"DIMM 1:\tIndicates whether DIMM 0 in the channel is present\n";


void
dimm_report_dcmd_help(void)
{
	mdb_printf(dimmhelp);
}

/*
 * Check both the primary and secondary base address values to see if an enable
 * flags is present. DIMM 0 uses chip selects 0/1 and DIMM 1 uses chip selects
 * 2/3.
 */
static int
dimm_report_dimm_present(uint8_t sock, uint8_t umcno, uint8_t dimm,
    boolean_t *pres)
{
	int ret;
	uint32_t base0, base1, sec0, sec1;
	uint8_t cs0 = dimm * 2;
	uint8_t cs1 = dimm * 2 + 1;
	smn_reg_t base0_reg = UMC_BASE(umcno, cs0);
	smn_reg_t base1_reg = UMC_BASE(umcno, cs1);
	smn_reg_t sec0_reg = UMC_BASE_SEC(umcno, cs0);
	smn_reg_t sec1_reg = UMC_BASE_SEC(umcno, cs1);

	if ((ret = rdmsn_regdef(base0_reg, sock, &base0)) != DCMD_OK ||
	    (ret = rdmsn_regdef(base1_reg, sock, &base1)) != DCMD_OK ||
	    (ret = rdmsn_regdef(sec0_reg, sock, &sec0)) != DCMD_OK ||
	    (ret = rdmsn_regdef(sec1_reg, sock, &sec1)) != DCMD_OK) {
		return (ret);
	}

	*pres = UMC_BASE_GET_EN(base0) != 0 || UMC_BASE_GET_EN(base1) != 0 ||
	    UMC_BASE_GET_EN(sec0) != 0 || UMC_BASE_GET_EN(sec1) != 0;
	return (DCMD_OK);
}

/*
 * Output in board order, not UMC order (hence dfp_umc_order[]), a summary of
 * training information for each DRAM channel.
 */
static int
dimm_report_dcmd_sock(uint8_t sock)
{
	for (size_t i = 0; i < df_props->dfp_umc_count; i++) {
		const uint8_t umcno = df_props->dfp_umc_order[i];
		const char *brdchan = df_props->dfp_umc_chan_map[umcno];
		int ret;
		boolean_t train, dimm0, dimm1;

		smn_reg_t umccfg_reg = UMC_UMCCFG(umcno);
		uint32_t umccfg;

		ret = rdmsn_regdef(umccfg_reg, sock, &umccfg);
		if (ret != DCMD_OK) {
			return (ret);
		}
		train = UMC_UMCCFG_GET_READY(umccfg);

		ret = dimm_report_dimm_present(sock, umcno, 0, &dimm0);
		if (ret != DCMD_OK) {
			mdb_warn("failed to read UMC %u DIMM 0 presence\n",
			    umcno);
			return (DCMD_ERR);
		}

		ret = dimm_report_dimm_present(sock, umcno, 1, &dimm1);
		if (ret != DCMD_OK) {
			mdb_warn("failed to read UMC %u DIMM 1 presence\n",
			    umcno);
			return (DCMD_ERR);
		}

		mdb_printf("%u/%s\t%u\t%s\t%s\t%s\n", sock, brdchan, umcno,
		    train ? "yes" : "no", dimm0 ? "present" : "missing",
		    dimm1 ? "present" : "missing");
	}

	return (DCMD_OK);
}

/*
 * Report DIMM presence and DRAM channel readiness, which is a proxy for
 * training having completed.
 */
int
dimm_report_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int ret;
	uint32_t val;

	if ((flags & DCMD_ADDRSPEC) != 0) {
		mdb_warn("::dimm_report does not support addresses\n");
		return (DCMD_USAGE);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("CHAN\tUMC\tTRAIN\tDIMM 0\tDIMM 1\n");
	}

	ret = dimm_report_dcmd_sock(0);
	if (ret != DCMD_OK) {
		return (ret);
	}

	/*
	 * Attempt to read a DF entry to see if the other socket is present as a
	 * proxy.
	 */
	if (!df_read32(1, DF_FBIINFO0, &val)) {
		mdb_warn("failed to probe for second socket\n");
		return (DCMD_ERR);
	}

	if (val != PCI_EINVAL32) {
		ret = dimm_report_dcmd_sock(1);

	}

	return (ret);
}
