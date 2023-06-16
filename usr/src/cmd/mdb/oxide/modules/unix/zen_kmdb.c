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

/*
 * This implements several dcmds for getting at state for use in kmdb. Several
 * of these kind of assume that someone else isn't doing something with them at
 * the same time that we are (mostly because there are only so many slots that
 * can be used for different purposes.
 */

#include <mdb/mdb_modapi.h>
#include <kmdb/kmdb_modext.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/sysmacros.h>
#include <milan/milan_physaddrs.h>
#include <sys/amdzen/ccx.h>
#include <sys/amdzen/umc.h>
#include <io/amdzen/amdzen.h>

#include <milan_apob.h>
#include <milan_apob_impl.h>

static uint64_t pcicfg_physaddr;
static boolean_t pcicfg_valid;

/*
 * These variables, when set, contain a discovered fabric ID.
 */
static boolean_t df_masks_valid;
static uint32_t df_node_shift;
static uint32_t df_node_mask;
static uint32_t df_comp_mask;

typedef struct df_comp {
	uint_t dc_inst;
	const char *dc_name;
	uint_t dc_ndram;
} df_comp_t;

static df_comp_t df_comp_names[0x2b] = {
	{ 0, "UMC0", 2 },
	{ 1, "UMC1", 2 },
	{ 2, "UMC2", 2 },
	{ 3, "UMC3", 2 },
	{ 4, "UMC4", 2 },
	{ 5, "UMC5", 2 },
	{ 6, "UMC6", 2 },
	{ 7, "UMC7", 2 },
	{ 8, "CCIX0" },
	{ 9, "CCIX1" },
	{ 10, "CCIX2" },
	{ 11, "CCIX3" },
	{ 16, "CCM0", 16 },
	{ 17, "CCM1", 16 },
	{ 18, "CCM2", 16 },
	{ 19, "CCM3", 16 },
	{ 20, "CCM4", 16 },
	{ 21, "CCM5", 16 },
	{ 22, "CCM6", 16 },
	{ 23, "CCM7", 16 },
	{ 24, "IOMS0", 16 },
	{ 25, "IOMS1", 16 },
	{ 26, "IOMS2", 16 },
	{ 27, "IOMS3", 16 },
	{ 30, "PIE0", 8 },
	{ 31, "CAKE0" },
	{ 32, "CAKE1" },
	{ 33, "CAKE2" },
	{ 34, "CAKE3" },
	{ 35, "CAKE4" },
	{ 36, "CAKE5" },
	{ 37, "TCDX0" },
	{ 38, "TCDX1" },
	{ 39, "TCDX2" },
	{ 40, "TCDX3" },
	{ 41, "TCDX4" },
	{ 42, "TCDX5" },
	{ 43, "TCDX6" },
	{ 44, "TCDX7" },
	{ 45, "TCDX8" },
	{ 46, "TCDX9" },
	{ 47, "TCDX10" },
	{ 48, "TCDX11" }
};

static const char *df_chan_ileaves[16] = {
	"1", "2", "Reserved", "4",
	"Reserved", "8", "6", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved",
	"COD-4 2", "COD-2 4", "COD-1 8", "Reserved"
};

static const char *milan_chan_map[8] = {
	[0] = "A",
	[1] = "B",
	[2] = "D",
	[3] = "C",
	[4] = "H",
	[5] = "G",
	[6] = "E",
	[7] = "F",
};

static const char *
df_comp_name(uint32_t compid)
{
	for (uint_t i = 0; i < ARRAY_SIZE(df_comp_names); i++) {
		if (compid == df_comp_names[i].dc_inst) {
			return (df_comp_names[i].dc_name);
		}
	}

	return (NULL);
}

static uint_t
df_comp_ndram(uint32_t compid)
{
	for (uint_t i = 0; i < ARRAY_SIZE(df_comp_names); i++) {
		if (compid == df_comp_names[i].dc_inst) {
			return (df_comp_names[i].dc_ndram);
		}
	}

	return (0);
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
	} else if ((addr & ~0x3fc) != 0) {
		mdb_warn("invalid register: 0x%x, must be 4-byte aligned\n",
		    addr);
		return (DCMD_ERR);
	}

	if (sock_set) {
		/*
		 * We don't really know how many I/O dies there are in advance;
		 * however, the theoretical max is 8 (2P Naples with 4 dies);
		 * however, on the Oxide architecture there'll only ever be 2.
		 */
		if (*sock > 1) {
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


	if ((!inst_set && !broadcast) ||
	    (inst_set && broadcast)) {
		mdb_warn("One of -i or -b must be set\n");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

static boolean_t
df_read32(uint64_t sock, const df_reg_def_t df, uint32_t *valp)
{
	return (pcicfg_read(0, 0x18 + sock, df.drd_func, df.drd_reg,
	    sizeof (*valp), valp));
}

static boolean_t
df_write32(uint64_t sock, const df_reg_def_t df, uint32_t val)
{
	return (pcicfg_write(0, 0x18 + sock, df.drd_func, df.drd_reg,
	    sizeof (val), val));
}

static boolean_t
df_read32_indirect_raw(uint64_t sock, uintptr_t inst, uintptr_t func,
    uint16_t reg, uint32_t *valp)
{
	uint32_t val = 0;
	const df_reg_def_t ficaa = DF_FICAA_V2;

	val = DF_FICAA_V2_SET_TARG_INST(val, 1);
	val = DF_FICAA_V2_SET_FUNC(val, func);
	val = DF_FICAA_V2_SET_INST(val, inst);
	val = DF_FICAA_V2_SET_64B(val, 0);
	val = DF_FICAA_V2_SET_REG(val, reg >> 2);

	if (!pcicfg_write(0, 0x18 + sock, ficaa.drd_func, ficaa.drd_reg,
	    sizeof (val), val)) {
		return (B_FALSE);
	}

	if (!df_read32(sock, DF_FICAD_LO_V2, &val)) {
		return (B_FALSE);
	}

	*valp = val;
	return (B_TRUE);
}

static boolean_t
df_read32_indirect(uint64_t sock, uintptr_t inst, const df_reg_def_t def,
    uint32_t *valp)
{
	if ((def.drd_gens & DF_REV_3) == 0) {
		mdb_warn("asked to read DF reg that doesn't support Gen 3: "
		    "func/reg: %u/0x%x, gens: 0x%x\n", def.drd_func,
		    def.drd_reg, def.drd_gens);
		return (B_FALSE);
	}

	return (df_read32_indirect_raw(sock, inst, def.drd_func, def.drd_reg,
	    valp));
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
		if (!df_read32_indirect_raw(sock, inst, func, addr, &val)) {
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
		uint32_t rval = 0;

		rval = DF_FICAA_V2_SET_TARG_INST(rval, 1);
		rval = DF_FICAA_V2_SET_REG(rval, addr >> 2);
		rval = DF_FICAA_V2_SET_INST(rval, inst);
		rval = DF_FICAA_V2_SET_64B(rval, 0);
		rval = DF_FICAA_V2_SET_FUNC(rval, func);

		if (!df_write32(sock, DF_FICAA_V2, rval)) {
			return (DCMD_ERR);
		}

		if (!df_write32(sock, DF_FICAD_LO_V2, val)) {
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
smn_rw_regdef(const smn_reg_t reg, uint64_t sock, smn_rw_t rw,
    uint32_t *smn_val)
{
	uint32_t df_busctl;
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

	if (!df_read32(sock, DF_CFG_ADDR_CTL_V2, &df_busctl)) {
		mdb_warn("failed to read DF config address\n");
		return (DCMD_ERR);
	}

	if (df_busctl == PCI_EINVAL32) {
		mdb_warn("got back PCI_EINVAL32 when reading from the df\n");
		return (DCMD_ERR);
	}

	smn_busno = DF_CFG_ADDR_CTL_GET_BUS_NUM(df_busctl);
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

	if (sock > 1) {
		mdb_warn("invalid socket ID: %lu", sock);
		return (DCMD_ERR);
	}

	if (addr > UINT32_MAX) {
		mdb_warn("address %lx is out of range [0, 0xffffffff]\n", addr);
		return (DCMD_ERR);
	}

	const smn_reg_t reg = SMN_MAKE_REG_SIZED(addr, len);

	ret = smn_rw_regdef(reg, sock, rw, &smn_val);
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

static boolean_t
df_fetch_masks(void)
{
	uint32_t fid0, fid1;

	if (!df_read32(0, DF_FIDMASK0_V3, &fid0) ||
	    !df_read32(0, DF_FIDMASK1_V3, &fid1)) {
		mdb_warn("failed to read masks register\n");
		return (B_FALSE);
	}


	df_node_mask = DF_FIDMASK0_V3_GET_NODE_MASK(fid0);
	df_comp_mask = DF_FIDMASK0_V3_GET_COMP_MASK(fid0);
	df_node_shift = DF_FIDMASK1_V3_GET_NODE_SHIFT(fid1);

	df_masks_valid = B_TRUE;
	return (B_TRUE);
}

/*
 * Given a data fabric fabric ID (critically not an instance ID), print
 * information about that.
 */
static void
df_print_dest(uint32_t dest)
{
	uint32_t node, comp;
	const char *name;

	if (!df_masks_valid) {
		if (!df_fetch_masks()) {
			mdb_printf("%x", dest);
			return;
		}
	}

	node = (dest & df_node_mask) >> df_node_shift;
	comp = dest & df_comp_mask;
	name = df_comp_name(comp);

	mdb_printf("%#x (%#x/%#x)", dest, node, comp);
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

static int
df_route_buses(uint_t flags, uint64_t sock, uintptr_t inst)
{
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-7s %-8s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINATION");
	}

	for (uint_t i = 0; i < DF_MAX_CFGMAP; i++) {
		uint32_t val;

		if (!df_read32_indirect(sock, inst, DF_CFGMAP_V2(i), &val)) {
			mdb_warn("failed to read cfgmap %u\n", i);
			continue;
		}

		if (val == PCI_EINVAL32) {
			mdb_warn("got back invalid read for cfgmap %u\n", i);
			continue;
		}

		mdb_printf("%-7#x %-7#x %c%c       ",
		    DF_CFGMAP_V2_GET_BUS_BASE(val),
		    DF_CFGMAP_V2_GET_BUS_LIMIT(val),
		    DF_CFGMAP_V2_GET_RE(val) ? 'R' : '-',
		    DF_CFGMAP_V2_GET_WE(val) ? 'W' : '-');
		df_print_dest(DF_CFGMAP_V3_GET_DEST_ID(val));
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
df_route_dram(uint_t flags, uint64_t sock, uintptr_t inst)
{
	uint_t ndram = df_comp_ndram(inst);

	if (ndram == 0) {
		mdb_warn("component 0x%x has no DRAM rules\n", inst);
		return (DCMD_ERR);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-?s %-?s %-7s %-15s %s\n", "BASE", "LIMIT",
		    "FLAGS", "INTERLEAVE", "DESTINATION");
	}

	for (uint_t i = 0; i < ndram; i++) {
		uint32_t breg, lreg;
		uint64_t base, limit;
		const char *chan;
		char ileave[16];

		if (!df_read32_indirect(sock, inst, DF_DRAM_BASE_V2(i),
		    &breg)) {
			mdb_warn("failed to read DRAM port base %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, DF_DRAM_LIMIT_V2(i),
		    &lreg)) {
			mdb_warn("failed to read DRAM port limit %u\n", i);
			continue;
		}

		base = DF_DRAM_BASE_V2_GET_BASE(breg);
		base <<= DF_DRAM_BASE_V2_BASE_SHIFT;
		limit = DF_DRAM_LIMIT_V2_GET_LIMIT(lreg);
		limit <<= DF_DRAM_LIMIT_V2_LIMIT_SHIFT;
		limit += DF_DRAM_LIMIT_V2_LIMIT_EXCL - 1;

		chan = df_chan_ileaves[
		    DF_DRAM_BASE_V3_GET_ILV_CHAN(breg)];
		(void) mdb_snprintf(ileave, sizeof (ileave), "%u/%s/%u/%u",
		    DF_DRAM_BASE_V3_GET_ILV_ADDR(breg) + 8, chan,
		    DF_DRAM_BASE_V3_GET_ILV_DIE(breg) + 1,
		    DF_DRAM_BASE_V3_GET_ILV_SOCK(breg) + 1);

		mdb_printf("%-?#lx %-?#lx %c%c%c     %-15s ", base, limit,
		    DF_DRAM_BASE_V2_GET_VALID(breg) ? 'V' : '-',
		    DF_DRAM_BASE_V2_GET_HOLE_EN(breg) ? 'H' : '-',
		    DF_DRAM_LIMIT_V3_GET_BUS_BREAK(lreg) ?
		    'B' : '-', ileave);
		df_print_dest(DF_DRAM_LIMIT_V3_GET_DEST_ID(lreg));
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
df_route_ioports(uint_t flags, uint64_t sock, uintptr_t inst)
{
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-8s %-8s %-8s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINAION");
	}

	for (uint_t i = 0; i < DF_MAX_IO_RULES; i++) {
		uint32_t breg, lreg, base, limit;

		if (!df_read32_indirect(sock, inst, DF_IO_BASE_V2(i),
		    &breg)) {
			mdb_warn("failed to read I/O port base %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, DF_IO_LIMIT_V2(i),
		    &lreg)) {
			mdb_warn("failed to read I/O port limit %u\n", i);
			continue;
		}

		base = DF_IO_BASE_V2_GET_BASE(breg);
		base <<= DF_IO_BASE_SHIFT;
		limit = DF_IO_LIMIT_V2_GET_LIMIT(lreg);
		limit <<= DF_IO_LIMIT_SHIFT;
		limit += DF_IO_LIMIT_EXCL - 1;

		mdb_printf("%-8#x %-8#x %c%c%c      ", base, limit,
		    DF_IO_BASE_V2_GET_RE(breg) ? 'R' : '-',
		    DF_IO_BASE_V2_GET_WE(breg) ? 'W' : '-',
		    DF_IO_BASE_V2_GET_IE(breg) ? 'I' : '-');
		df_print_dest(DF_IO_LIMIT_V3_GET_DEST_ID(lreg));
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
df_route_mmio(uint_t flags, uint64_t sock, uintptr_t inst)
{
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-?s %-?s %-8s %s\n", "BASE", "LIMIT", "FLAGS",
		    "DESTINAION");
	}

	for (uint_t i = 0; i < DF_MAX_MMIO_RULES; i++) {
		uint32_t breg, lreg, control;
		uint64_t base, limit;

		if (!df_read32_indirect(sock, inst, DF_MMIO_BASE_V2(i),
		    &breg)) {
			mdb_warn("failed to read MMIO base %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, DF_MMIO_LIMIT_V2(i),
		    &lreg)) {
			mdb_warn("failed to read MMIO limit %u\n", i);
			continue;
		}

		if (!df_read32_indirect(sock, inst, DF_MMIO_CTL_V2(i),
		    &control)) {
			mdb_warn("failed to read MMIO control %u\n", i);
			continue;
		}

		base = (uint64_t)breg << DF_MMIO_SHIFT;
		limit = (uint64_t)lreg << DF_MMIO_SHIFT;
		limit += DF_MMIO_LIMIT_EXCL - 1;

		mdb_printf("%-?#lx %-?#lx %c%c%c%c     ", base, limit,
		    DF_MMIO_CTL_GET_RE(control) ? 'R' : '-',
		    DF_MMIO_CTL_GET_WE(control) ? 'W' : '-',
		    DF_MMIO_CTL_V3_GET_NP(control) ? 'N' : '-',
		    DF_MMIO_CTL_GET_CPU_DIS(control) ? 'C' : '-');
		df_print_dest(DF_MMIO_CTL_V3_GET_DEST_ID(control));
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

	if (sock > 1) {
		mdb_warn("invalid socket ID: %lu\n", sock);
		return (DCMD_ERR);
	}

	/*
	 * For DRAM, default to CCM0 (we don't use a UMC because it has very few
	 * rules). For I/O ports, use CCM0 as well as the IOMS entries don't
	 * really have rules here. For MMIO and PCI buses, use IOMS0.
	 */
	if (!inst_set) {
		if (opt_d || opt_I) {
			inst = 0x10;
		} else {
			inst = 0x18;
		}
	}

	if (opt_d) {
		return (df_route_dram(flags, sock, inst));
	} else if (opt_b) {
		return (df_route_buses(flags, sock, inst));
	} else if (opt_I) {
		return (df_route_ioports(flags, sock, inst));
	} else {
		return (df_route_mmio(flags, sock, inst));
	}

	return (DCMD_OK);
}

/*
 * Milan APOB walker. The APOB is placed at a specific physical address and is
 * not currently preserved past the initial kboot_mmu.
 */
int
apob_walk_init(mdb_walk_state_t *wsp)
{
	uint_t khat;
	uint64_t apob_addr = MILAN_APOB_ADDR, start, end;
	milan_apob_header_t hdr;
	const uint8_t milan_apob_sig[4] = { 'A', 'P', 'O', 'B' };

	if (mdb_readvar(&khat, "khat_running") != sizeof (khat)) {
		mdb_warn("failed to read khat_running");
		return (WALK_ERR);
	}

	if (khat != 0) {
		mdb_warn("khat is running, no APOB for you\n");
		return (WALK_ERR);
	}

	if (wsp->walk_addr != 0) {
		apob_addr = wsp->walk_addr;
	}

	if (mdb_pread(&hdr, sizeof (hdr), apob_addr) != sizeof (hdr)) {
		mdb_warn("failed to read APOB header at 0x%lx", apob_addr);
		return (WALK_ERR);
	}

	if (hdr.mah_sig[0] != milan_apob_sig[0] ||
	    hdr.mah_sig[1] != milan_apob_sig[1] ||
	    hdr.mah_sig[2] != milan_apob_sig[2] ||
	    hdr.mah_sig[3] != milan_apob_sig[3]) {
		mdb_warn("Bad APOB signature, found 0x%x 0x%x 0x%x 0x%x\n",
		    hdr.mah_sig[0], hdr.mah_sig[1], hdr.mah_sig[2],
		    hdr.mah_sig[3]);
		return (WALK_ERR);
	}

	start = apob_addr + hdr.mah_off;
	end = apob_addr + hdr.mah_size;
	wsp->walk_data = (void *)end;
	wsp->walk_addr = start;

	return (WALK_NEXT);
}

int
apob_walk_step(mdb_walk_state_t *wsp)
{
	uint64_t addr = wsp->walk_addr;
	uint64_t limit = (uint64_t)wsp->walk_data;
	milan_apob_entry_t entry;
	int ret;

	if (addr + sizeof (milan_apob_entry_t) >= limit) {
		return (WALK_DONE);
	}

	if (mdb_pread(&entry, sizeof (entry), addr) != sizeof (entry)) {
		mdb_warn("failed to read APOB entry at 0x%lx", addr);
		return (WALK_ERR);
	}

	if (entry.mae_size < sizeof (milan_apob_entry_t)) {
		mdb_warn("APOB entry at 0x%lx is smaller than the size of "
		    "the APOB entry structure, found 0x%x bytes\n", addr,
		    entry.mae_size);
		return (WALK_ERR);
	}

	if (addr + entry.mae_size > limit) {
		mdb_warn("APOB entry at 0x%x with size 0x%x extends beyond "
		    "limit address 0x%x\n", addr, entry.mae_size, limit);
		return (WALK_ERR);
	}

	ret = wsp->walk_callback(addr, &entry, wsp->walk_cbdata);
	if (ret != WALK_NEXT) {
		return (ret);
	}

	wsp->walk_addr += entry.mae_size;
	return (WALK_NEXT);
}

static const char *apobhelp =
"Walk the APOB and print all entries that match the specified group and\n"
"type IDs. Both of these are required and will print all instances that\n"
"match right now. The following options are supported:\n"
"\n"
"  -g group	Search for items that match the specified group\n"
"  -t type	Search for items that match the specified type\n";

void
apob_dcmd_help(void)
{
	mdb_printf(apobhelp);
}

typedef struct apoc_dcmd_data {
	uintptr_t add_group;
	uintptr_t add_type;
} apob_dcmd_data_t;

static int
apob_dcmd_cb(uintptr_t addr, const void *apob, void *arg)
{
	const milan_apob_entry_t *ent = apob;
	const apob_dcmd_data_t *data = arg;

	if (ent->mae_group == data->add_group &&
	    ent->mae_type == data->add_type) {
		mdb_printf("0x%lx\n", addr);
	}

	return (WALK_NEXT);
}

int
apob_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	apob_dcmd_data_t data = { 0 };
	boolean_t group_set, type_set;

	if (mdb_getopts(argc, argv,
	    'g', MDB_OPT_UINTPTR_SET, &group_set, &data.add_group,
	    't', MDB_OPT_UINTPTR_SET, &type_set, &data.add_type, NULL) !=
	    argc) {
		return (DCMD_USAGE);
	}

	if (!group_set || !type_set) {
		mdb_warn("both -g and -t must be specified\n");
		return (DCMD_USAGE);
	}

	return (mdb_walk("apob", apob_dcmd_cb, &data));
}


int
pmuerr_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	milan_apob_entry_t ent;
	milan_apob_pmu_tfi_t tfi;
	uint64_t paddr;
	const size_t need = sizeof (milan_apob_entry_t) +
	    sizeof (milan_apob_pmu_tfi_t);

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (mdb_pread(&ent, sizeof (ent), addr) != sizeof (ent)) {
		mdb_warn("failed to read APOB entry 0x%lx", addr);
		return (DCMD_ERR);
	}

	if (ent.mae_group != MILAN_APOB_GROUP_MEMORY ||
	    ent.mae_type != MILAN_APOB_MEMORY_PMU_TRAIN_FAIL) {
		mdb_warn("APOB entry at 0x%lx does not have PMU Training "
		    "Failure data group/type 0x%x/0x%x, found 0x%x/0x%x\n",
		    addr, MILAN_APOB_GROUP_MEMORY,
		    MILAN_APOB_MEMORY_PMU_TRAIN_FAIL, ent.mae_group,
		    ent.mae_type);
		return (DCMD_ERR);
	}

	if (ent.mae_size < need) {
		mdb_warn("APOB entry at 0x%lx is not large enough to contain "
		    "the expected training data size, found 0x%lx bytes, "
		    "needed 0x%lx", addr, ent.mae_size, need);
		return (DCMD_ERR);
	}

	paddr = addr + offsetof(milan_apob_entry_t, mae_data);
	if (mdb_pread(&tfi, sizeof (tfi), paddr) != sizeof (tfi)) {
		mdb_warn("failed to read PMU trainig data");
		return (DCMD_ERR);
	}

	if (tfi.mapt_nvalid == 0) {
		mdb_printf("No PMU failure entries. Either MBIST wasn't "
		    "enabled or the system is clean\n");
		return (DCMD_OK);
	}

	if (tfi.mapt_nvalid > ARRAY_SIZE(tfi.mapt_ents)) {
		mdb_warn("structure claims %u valid events, but only "
		    "%u are possible, limiting to %u\n", tfi.mapt_nvalid,
		    ARRAY_SIZE(tfi.mapt_ents), ARRAY_SIZE(tfi.mapt_ents));
		tfi.mapt_nvalid = ARRAY_SIZE(tfi.mapt_ents);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("SOCK UMC   xD ATTEMPT STAGE ERROR      DATA\n");
	}

	for (uint32_t i = 0; i < tfi.mapt_nvalid; i++) {
		const milan_apob_tfi_ent_t *ent = &tfi.mapt_ents[i];
		const char *sp3chan = milan_chan_map[ent->mapte_umc];

		mdb_printf("%-4u %-1u (%s) %-1uD %-7u %-5u 0x%-08x 0x%x 0x%x "
		    "0x%x 0x%x\n", ent->mapte_sock, ent->mapte_umc, sp3chan,
		    ent->mapte_1d2d + 1, ent->mapte_1dnum, ent->mapte_stage,
		    ent->mapte_error, ent->mapte_data[0], ent->mapte_data[1],
		    ent->mapte_data[2], ent->mapte_data[3]);
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
 * Output in board order, not UMC order (hence umc_order[] below) a summary of
 * training information for each DRAM channel.
 */
static int
dimm_report_dcmd_sock(uint8_t sock)
{
	const uint8_t umc_order[8] = { 0, 1, 3, 2, 6, 7, 5, 4 };

	for (size_t i = 0; i < ARRAY_SIZE(umc_order); i++) {
		const uint8_t umcno = umc_order[i];
		const char *brdchan = milan_chan_map[umcno];
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
	if (!df_read32(1, DF_CFG_ADDR_CTL_V2, &val)) {
		mdb_warn("failed to read DF config address\n");
		return (DCMD_ERR);
	}

	if (val != PCI_EINVAL32) {
		ret = dimm_report_dcmd_sock(1);

	}

	return (ret);
}
