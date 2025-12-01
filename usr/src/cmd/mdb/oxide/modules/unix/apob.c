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
 * This implements several dcmds for interpreting the contents of the kernel's
 * copy of the APOB (or any APOB the user points us at).
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ctf.h>
#include <sys/sysmacros.h>
#include <sys/apob.h>
#include <sys/apob_impl.h>
#include <sys/io/zen/apob.h>

#include "apob_mod.h"

/*
 * Special value to indicate we should try to discover where the APOB is
 * located on the current system as opposed to using a user-specified address.
 */
#define	DISCOVER_APOB	UINTPTR_MAX

extern const char *milan_chan_map[8];
extern const char *genoa_chan_map[12];
extern const char *turin_chan_map[12];

static const char *apob_target_cpu = "<unknown>";
static const char **chan_map;
static size_t chan_map_size;

typedef struct mdb_apob_apob_hdl {
	uintptr_t ah_header;
	size_t ah_len;
} mdb_apob_apob_hdl_t;

void
apob_set_target(x86_processor_family_t pf)
{
	switch (pf) {
	case X86_PF_AMD_MILAN:
		chan_map = milan_chan_map;
		chan_map_size = ARRAY_SIZE(milan_chan_map);
		apob_target_cpu = "Milan";
		break;
	case X86_PF_AMD_GENOA:
		chan_map = genoa_chan_map;
		chan_map_size = ARRAY_SIZE(genoa_chan_map);
		apob_target_cpu = "Genoa";
		break;
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		chan_map = turin_chan_map;
		chan_map_size = ARRAY_SIZE(turin_chan_map);
		apob_target_cpu = "Turin";
		break;
	default:
		mdb_warn("apob: unsupported AMD chiprev family: %u\n", pf);
	}
}

static const char *
chan_name(size_t chan)
{
	if (chan_map == NULL || chan >= chan_map_size)
		return ("?");
	return (chan_map[chan]);
}

/*
 * APOB walker.  The APOB is always mapped if mdb or kmdb can run.
 */
int
apob_walk_init(mdb_walk_state_t *wsp)
{
	uintptr_t apob_addr, start, end;
	size_t hdl_len = 0;
	GElf_Sym hdlsym;
	mdb_apob_apob_hdl_t hdl;
	apob_header_t hdr;

	if (wsp->walk_addr != DISCOVER_APOB) {
		apob_addr = wsp->walk_addr;
#ifdef	APOB_RAW_DMOD
	} else if (wsp->walk_addr == DISCOVER_APOB) {
		/*
		 * If an explicit address wasn't specified with the raw file
		 * target, assume the APOB starts at 0.
		 */
		apob_addr = 0;
#endif /* APOB_RAW_DMOD */
	} else if (mdb_lookup_by_name("kapob_hdl", &hdlsym) != 0 ||
	    GELF_ST_TYPE(hdlsym.st_info) != STT_OBJECT ||
	    mdb_ctf_vread(&hdl, "apob_hdl_t", "mdb_apob_apob_hdl_t",
	    (uintptr_t)hdlsym.st_value, 0) != 0) {
		mdb_warn("failed to read an APOB handle from the target");
		return (WALK_ERR);
	} else {
		apob_addr = (uintptr_t)hdl.ah_header;
		hdl_len = hdl.ah_len;
	}

	if (mdb_vread(&hdr, sizeof (hdr), apob_addr) != sizeof (hdr)) {
		mdb_warn("failed to read APOB header at 0x%lx", apob_addr);
		return (WALK_ERR);
	}

	if (bcmp(hdr.ah_sig, APOB_SIG, ARRAY_SIZE(APOB_SIG)) != 0) {
		mdb_warn("Bad APOB signature, found 0x%x 0x%x 0x%x 0x%x\n",
		    hdr.ah_sig[0], hdr.ah_sig[1], hdr.ah_sig[2],
		    hdr.ah_sig[3]);
		return (WALK_ERR);
	}

	start = apob_addr + hdr.ah_off;

	if (hdl_len == 0)
		hdl_len = hdr.ah_size;

	if (hdl_len > hdr.ah_size) {
		mdb_warn("kernel APOB handle size 0x%lx exceeds self-reported "
		    "size 0x%lx; using self-reported size",
		    hdl_len, hdr.ah_size);
		hdl_len = hdr.ah_size;
	}

	if (hdl_len < hdr.ah_size) {
		mdb_warn("kernel APOB is truncated from self-reported size "
		    "0x%lx to 0x%lx", hdr.ah_size, hdl_len);
	}

	end = apob_addr + hdl_len;
	wsp->walk_data = (void *)end;
	wsp->walk_addr = start;

	return (WALK_NEXT);
}

int
apob_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr = wsp->walk_addr;
	uintptr_t limit = (uintptr_t)wsp->walk_data;
	apob_entry_t entry;
	int ret;

	if (addr + sizeof (apob_entry_t) > limit) {
		return (WALK_DONE);
	}

	if (mdb_vread(&entry, sizeof (entry), addr) != sizeof (entry)) {
		mdb_warn("failed to read APOB entry at 0x%lx", addr);
		return (WALK_ERR);
	}

	if (entry.ae_size < sizeof (apob_entry_t)) {
		mdb_warn("APOB entry at 0x%lx is smaller than the size of "
		    "the APOB entry structure, found 0x%x bytes\n", addr,
		    entry.ae_size);
		return (WALK_ERR);
	}

	if (addr + entry.ae_size > limit) {
		mdb_warn("APOB entry at 0x%x with size 0x%x extends beyond "
		    "limit address 0x%x\n", addr, entry.ae_size, limit);
		return (WALK_ERR);
	}

	ret = wsp->walk_callback(addr, &entry, wsp->walk_cbdata);
	if (ret != WALK_NEXT) {
		return (ret);
	}

	wsp->walk_addr += entry.ae_size;
	return (WALK_NEXT);
}

static const char *apobhelp =
"Walk the APOB and print all entries. The entries can be filtered by\n"
"group and type IDs.\n"
"The following options are supported:\n"
"\n"
"  -g group	Filter the output to items that match the specified group\n"
"  -t type	Filter the output to items that match the specified type\n";

void
apob_dcmd_help(void)
{
	mdb_printf(apobhelp);
}

typedef struct apob_dcmd_data {
	uintptr_t add_group;
	uintptr_t add_type;
} apob_dcmd_data_t;

static int
apob_dcmd_cb(uintptr_t addr, const void *apob, void *arg)
{
	const apob_entry_t *ent = apob;
	const apob_dcmd_data_t *data = arg;

	if ((data->add_group == 0 || ent->ae_group == data->add_group) &&
	    (data->add_type == 0 || ent->ae_type == data->add_type)) {
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

	if (flags & DCMD_ADDRSPEC) {
		return (mdb_pwalk("apob", apob_dcmd_cb, &data, addr));
	} else {
		return (mdb_pwalk("apob", apob_dcmd_cb, &data, DISCOVER_APOB));
	}
}

static int
apob_read_entry(uintptr_t addr, apob_group_t group, uint32_t type,
    void *data, size_t data_size)
{
	apob_entry_t ent;
	uintptr_t daddr;
	const size_t need = sizeof (apob_entry_t) + data_size;

	if (mdb_vread(&ent, sizeof (ent), addr) != sizeof (ent)) {
		mdb_warn("failed to read APOB entry 0x%lx", addr);
		return (DCMD_ERR);
	}

	if (ent.ae_group != group || ent.ae_type != type) {
		mdb_warn("APOB entry at 0x%lx does not have the expected APOB "
		    "data group/type 0x%x/0x%x: found 0x%x/0x%x\n",
		    addr, group, type, ent.ae_group, ent.ae_type);
		return (DCMD_ERR);
	}

	if (ent.ae_size < need) {
		mdb_warn("APOB entry at 0x%lx is not large enough to contain "
		    "the expected data size, found 0x%lx bytes, needed 0x%lx",
		    addr, ent.ae_size, need);
		return (DCMD_ERR);
	}

	daddr = addr + offsetof(apob_entry_t, ae_data);
	if (mdb_vread(data, data_size, daddr) != data_size) {
		mdb_warn("failed to read APOB entry data");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

int
pmuerr_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int ret;
	apob_pmu_tfi_t tfi;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if ((ret = apob_read_entry(addr, APOB_GROUP_MEMORY,
	    APOB_MEMORY_TYPE_PMU_TRAIN_FAIL, &tfi, sizeof (tfi))) != DCMD_OK) {
		return (ret);
	}

	if (tfi.apt_nvalid == 0) {
		mdb_printf("No PMU failure entries found.\n");
		return (DCMD_OK);
	}

	if (tfi.apt_nvalid > ARRAY_SIZE(tfi.apt_ents)) {
		mdb_warn("structure claims %u valid events, but only "
		    "%u are possible, limiting to %u\n", tfi.apt_nvalid,
		    ARRAY_SIZE(tfi.apt_ents), ARRAY_SIZE(tfi.apt_ents));
		tfi.apt_nvalid = ARRAY_SIZE(tfi.apt_ents);
	}

	for (uint32_t i = 0; i < tfi.apt_nvalid; i++) {
		const apob_tfi_ent_t *ent = &tfi.apt_ents[i];
		uint32_t sock, umc, dim, dnum, dtype;

		/*
		 * The UMC field is three bits for architectures that have 8
		 * channels (Zen3) and four bits for those with more (Zen4+),
		 * with the following fields all being bumped along. We use the
		 * number of channels to select the appropriate variant.
		 */
		if (chan_map_size > 8) {
			sock = ent->l.apte_sock;
			umc = ent->l.apte_umc;
			dim = ent->l.apte_1d2d;
			dnum = ent->l.apte_1dnum;
			dtype = ent->l.apte_dtype;
		} else {
			sock = ent->s.apte_sock;
			umc = ent->s.apte_umc;
			dim = ent->s.apte_1d2d;
			dnum = ent->s.apte_1dnum;
			dtype = ent->s.apte_dtype;
		}

		mdb_printf("%-4u %-1u (%s) %-1uD %-1u %-7u %-5u 0x%-08x "
		    "0x%x 0x%x 0x%x 0x%x\n",
		    sock, umc, chan_name(umc), dim + 1, dtype, dnum,
		    ent->apte_stage, ent->apte_error,
		    ent->apte_data[0], ent->apte_data[1],
		    ent->apte_data[2], ent->apte_data[3]);
	}

	return (DCMD_OK);
}

static const char *apob_eventhelp =
"Decode the APOB Event log. This breaks out each event that occurs and\n"
"where understood, decodes the class, event, and data. If the data is well\n"
"understood, then it will be further decoded. Data is represented as\n"
"tree-like to show the relationship between entities.\n\n"
"Instead of decoding an event log at a given address, one may optionally\n"
"provide specific event, class and (optional) data payloads to decode as a\n"
"synthetic event.\n";

void
apob_event_dcmd_help(void)
{
	mdb_printf(apob_eventhelp);
}

static const char *
apob_event_class_to_name(uint32_t class)
{
	switch (class) {
	case APOB_EVC_ALERT:
		return ("alert");
	case APOB_EVC_WARN:
		return ("warning");
	case APOB_EVC_ERROR:
		return ("error");
	case APOB_EVC_CRIT:
		return ("critical");
	case APOB_EVC_FATAL:
		return ("fatal");
	default:
		return ("unknown");
	}
}

static const char *
apob_event_info_to_name(uint32_t info)
{
	switch (info) {
	case APOB_EVENT_TRAIN_ERROR:
		return ("training error");
	case APOB_EVENT_MEMTEST_ERROR:
		return ("memory test error");
	case APOB_EVENT_PMU_RETRY_TRAIN:
		return ("training retried");
	case APOB_EVENT_MEM_RRW_ERROR:
		return ("MBIST error");
	case APOB_EVENT_MEM_PMIC_ERROR:
		return ("PMIC error");
	case APOB_EVENT_MEM_POP_ORDER:
		return ("non-recommended memory population order");
	case APOB_EVENT_MEM_SPD_CRC_ERROR:
		return ("DIMM SPD checksum error");
	case APOB_EVENT_PMIC_RT_ERROR:
		return ("PMIC real-time error");
	default:
		return ("unknown event");
	}
}

static void
apob_event_dcmd_print_train(uint32_t data0, uint32_t data1)
{
	const uint32_t sock = APOB_EVENT_TRAIN_ERROR_GET_SOCK(data0);
	const uint32_t chan = APOB_EVENT_TRAIN_ERROR_GET_CHAN(data0);

	if (APOB_EVENT_TRAIN_ERROR_GET_PMULOAD(data1) != 0) {
		mdb_printf("    PMU Firmware Loading Error\n");
	}

	if (APOB_EVENT_TRAIN_ERROR_GET_PMUTRAIN(data1) != 0) {
		mdb_printf("    PMU Training Error\n");
	}

	mdb_printf("    Socket: %u\n", sock);
	mdb_printf("    UMC:    %u (%s)\n", chan, chan_name(chan));
	mdb_printf("    DIMMs: ");
	if (APOB_EVENT_TRAIN_ERROR_GET_DIMM0(data0) != 0) {
		mdb_printf(" 0");
	}

	if (APOB_EVENT_TRAIN_ERROR_GET_DIMM1(data0) != 0) {
		mdb_printf(" 1");
	}

	mdb_printf("\n    RANKs: ");
	if (APOB_EVENT_TRAIN_ERROR_GET_RANK0(data0) != 0) {
		mdb_printf(" 0");
	}

	if (APOB_EVENT_TRAIN_ERROR_GET_RANK1(data0) != 0) {
		mdb_printf(" 1");
	}

	if (APOB_EVENT_TRAIN_ERROR_GET_RANK2(data0) != 0) {
		mdb_printf(" 2");
	}

	if (APOB_EVENT_TRAIN_ERROR_GET_RANK3(data0) != 0) {
		mdb_printf(" 3");
	}
	mdb_printf("\n");
}

static void
apob_event_dcmd_print_retry_train(uint32_t data0)
{
	const uint32_t sock = APOB_EVENT_PMU_RETRY_TRAIN_GET_SOCK(data0);
	const uint32_t count = APOB_EVENT_PMU_RETRY_TRAIN_GET_COUNT(data0);
	const uint32_t chans = APOB_EVENT_PMU_RETRY_TRAIN_GET_CHANS(data0);

	mdb_printf("    Socket:   %u\n", sock);
	mdb_printf("    Retries:  %u\n", count);
	mdb_printf("    UMCs:     0x%x\n", chans);
	for (uint32_t ch = 0; ch < APOB_EVENT_PMU_RETRY_TRAIN_CHANS; ch++) {
		if ((chans & (1 << ch)) == 0) {
			continue;
		}
		mdb_printf("      - %u (%s)\n", ch, chan_name(ch));
	}
}

static void
apob_event_dcmd_print_mem_pmic_error(uint32_t data0, uint32_t data1)
{
	const uint32_t sock = APOB_EVENT_MEM_PMIC_ERROR_GET_SOCK(data0);
	const uint32_t chan = APOB_EVENT_MEM_PMIC_ERROR_GET_CHAN(data0);
	const uint32_t dimm = APOB_EVENT_MEM_PMIC_ERROR_GET_DIMM(data0);
	const uint32_t sts = APOB_EVENT_MEM_PMIC_ERROR_GET_CHAN_STATUS(data0);
	const uint32_t preg4 = APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG4(data1);
	const uint32_t preg5 = APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG5(data1);
	const uint32_t preg6 = APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG6(data1);

	mdb_printf("    Socket: %u\n", sock);
	mdb_printf("    UMC:    %u (%s)\n", chan, chan_name(chan));
	mdb_printf("    DIMM:   %d\n", dimm);
	mdb_printf("    Channel %s\n", sts ? "Enabled" : "Disabled");

	mdb_printf("    PMIC:   0x%x 0x%x 0x%x\n", preg4, preg5, preg6);

	mdb_printf("      Errors:\n");
	mdb_printf("        %s error(s) since last erase\n",
	    PMIC_REG4_GET_ERRORS(preg4) ? ">1" : "0-1");
	if (PMIC_REG4_GET_CRITICAL_TEMPERATURE(preg4)) {
		mdb_printf("        - Critical Temperature\n");
	}
	if (PMIC_REG4_GET_VIN_BULK_OVER_VOLTAGE(preg4)) {
		mdb_printf("        - VIN_Bulk Over Voltage\n");
	}
	if (PMIC_REG4_GET_BUCK_OV_OR_UV(preg4)) {
		mdb_printf(
		    "        - Buck Regulator Output Over/Under Voltage\n");
	}

	mdb_printf("      Last Known Power Cycle:   ");
	switch (PMIC_REG5_GET_PMIC_LAST_STATUS(preg5)) {
	case PMIC_REG5_PMIC_LAST_STATUS_NORMAL:
		mdb_printf("Normal Power On\n");
		break;
	case PMIC_REG5_PMIC_LAST_STATUS_BUCK_OV_OR_UV:
		mdb_printf("Buck Regulator Output Over/Under Voltage\n");
		break;
	case PMIC_REG5_PMIC_LAST_STATUS_CRIT_TEMP:
		mdb_printf("Critical Temperature\n");
		break;
	case PMIC_REG5_PMIC_LAST_STATUS_VIN_BULK_OV:
		mdb_printf("VIN_Bulk Input Over Voltage\n");
		break;
	default:
		mdb_printf("<Unknown>\n");
		break;
	}

	mdb_printf("      Previous Power Cycle Switching Regulators Status:\n");
	mdb_printf("        SWA: %c%c%c\n",
	    PMIC_REG5_GET_PMIC_SWA_PWR_NOT_GOOD(preg5) ? 'P' : '-',
	    PMIC_REG6_GET_PMIC_SWA_OVER_VOLTAGE(preg6) ? 'O' : '-',
	    PMIC_REG6_GET_PMIC_SWA_UNDER_VOLTAGE_LOCKOUT(preg6) ? 'U' : '-');
	mdb_printf("        SWB: %c%c%c\n",
	    PMIC_REG5_GET_PMIC_SWB_PWR_NOT_GOOD(preg5) ? 'P' : '-',
	    PMIC_REG6_GET_PMIC_SWB_OVER_VOLTAGE(preg6) ? 'O' : '-',
	    PMIC_REG6_GET_PMIC_SWB_UNDER_VOLTAGE_LOCKOUT(preg6) ? 'U' : '-');
	mdb_printf("        SWC: %c%c%c\n",
	    PMIC_REG5_GET_PMIC_SWC_PWR_NOT_GOOD(preg5) ? 'P' : '-',
	    PMIC_REG6_GET_PMIC_SWC_OVER_VOLTAGE(preg6) ? 'O' : '-',
	    PMIC_REG6_GET_PMIC_SWC_UNDER_VOLTAGE_LOCKOUT(preg6) ? 'U' : '-');
	mdb_printf("        SWD: %c%c%c\n",
	    PMIC_REG5_GET_PMIC_SWD_PWR_NOT_GOOD(preg5) ? 'P' : '-',
	    PMIC_REG6_GET_PMIC_SWD_OVER_VOLTAGE(preg6) ? 'O' : '-',
	    PMIC_REG6_GET_PMIC_SWD_UNDER_VOLTAGE_LOCKOUT(preg6) ? 'U' : '-');
	mdb_printf("             P - Power Not Good, "
	    "O - Over Voltage, U - Under Voltage\n");
}

static void
apob_event_dcmd_print_mem_pop_order(uint32_t data0)
{
	const uint32_t sock = APOB_EVENT_MEM_POP_ORDER_GET_SOCK(data0);

	mdb_printf("    Socket: %u\n", sock);
	if (APOB_EVENT_MEM_POP_ORDER_GET_SYSTEM_HALTED(data0)) {
		mdb_printf("      System Halted!\n");
	}
}

static void
apob_event_dcmd_print_spd_crc(uint32_t data0)
{
	const uint32_t sock = APOB_EVENT_MEM_SPD_CRC_ERROR_GET_SOCK(data0);
	const uint32_t chan = APOB_EVENT_MEM_SPD_CRC_ERROR_GET_CHAN(data0);
	const uint32_t dimm = APOB_EVENT_MEM_SPD_CRC_ERROR_GET_DIMM(data0);

	mdb_printf("    Socket: %u\n", sock);
	mdb_printf("    UMC:    %u (%s)\n", chan, chan_name(chan));
	mdb_printf("    DIMM:   %u\n", dimm);
}

static void
apob_event_dcmd_print_pmic_rt_error(uint32_t data0, uint32_t data1)
{
	const uint32_t sock = APOB_EVENT_PMIC_RT_ERROR_GET_SOCK(data0);
	const uint32_t chan = APOB_EVENT_PMIC_RT_ERROR_GET_CHAN(data0);
	const uint32_t dimm = APOB_EVENT_PMIC_RT_ERROR_GET_DIMM(data0);
	const uint32_t sts = APOB_EVENT_PMIC_RT_ERROR_GET_CHAN_STATUS(data0);
	const uint32_t preg33 = APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG33(data0);
	const uint32_t preg8 = APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG8(data1);
	const uint32_t preg9 = APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG9(data1);
	const uint32_t prega = APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REGA(data1);
	const uint32_t pregb = APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REGB(data1);

	mdb_printf("    Socket: %u\n", sock);
	mdb_printf("    UMC:    %u (%s)\n", chan, chan_name(chan));
	mdb_printf("    DIMM:   %d\n", dimm);
	mdb_printf("    Channel %s\n", sts ? "Enabled" : "Disabled");

	mdb_printf("    PMIC:   0x%x 0x%x 0x%x 0x%x 0x%x\n", preg8, preg9,
	    prega, pregb, preg33);

	mdb_printf("      Errors:\n");
	if (PMIC_REG8_GET_CRIT_TEMP_SHUTDOWN(preg8)) {
		mdb_printf("        - PMIC temp. above shutdown threshold\n");
	}
	if (PMIC_REG9_GET_HIGH_TEMP_WARNING(preg9)) {
		mdb_printf("        - PMIC high temp. warning\n");
	}
	if (PMIC_REGA_GET_PENDING_IBI_OR_OUTSTANDING(prega)) {
		mdb_printf("        - Pending IBI or Outstanding Status\n");
	}
	if (PMIC_REGA_GET_PARITY_ERROR(prega)) {
		mdb_printf("        - Parity Error\n");
	}
	if (PMIC_REGA_GET_PEC_ERROR(prega)) {
		mdb_printf("        - PEC Error\n");
	}
	mdb_printf("\n");

	mdb_printf("      Power Rails Status:\n");
	mdb_printf("        VOUT_1.0V   : %c\n",
	    PMIC_REG33_GET_VOUT_1P0V_PWR_NOT_GOOD(preg33) ? 'P' : '-');
	mdb_printf("        VOUT_1.8V   : %c\n",
	    PMIC_REG9_GET_VOUT_1P8V_PWR_NOT_GOOD(preg9) ? 'P' : '-');
	mdb_printf("        VBias       : %c%c\n",
	    PMIC_REG9_GET_VBIAS_PWR_NOT_GOOD(preg9) ? 'P' : '-',
	    PMIC_REG33_GET_VBIAS_VIN_BULK_UNDER_VOLTAGE_LOCKOUT(preg33) ?
	    'U' : '-');
	if (!PMIC_REG9_GET_VIN_MGMT_VIN_BULK_SWITCHOVER(preg9)) {
		mdb_printf("        VIN_Mgmt    : %c%c\n",
		    !PMIC_REG33_GET_VIN_MGMT_PWR_GOOD_SWITCHOVER_MODE(preg33) ?
		    'P' : '-',
		    PMIC_REG8_GET_VIN_MGMT_INPUT_OVER_VOLTAGE(preg8) ? 'O' :
		    '-');
	}
	mdb_printf("        VIN_Bulk    : %c%c%c\n",
	    PMIC_REG8_GET_VIN_BULK_PWR_NOT_GOOD(preg8) ? 'P' : '-',
	    PMIC_REG8_GET_VIN_BULK_INPUT_OVER_VOLTAGE(preg8) ? 'O' : '-',
	    PMIC_REG33_GET_VBIAS_VIN_BULK_UNDER_VOLTAGE_LOCKOUT(preg33) ?
	    'U' : '-');
	mdb_printf("\n");

	mdb_printf("      Switching Regulators Status:\n");
	mdb_printf("        SWA: %c%c%c%c\n",
	    PMIC_REG8_GET_SWA_PWR_NOT_GOOD(preg8) ? 'P' : '-',
	    PMIC_REGA_GET_SWA_OVER_VOLTAGE(prega) ? 'O' : '-',
	    PMIC_REGB_GET_SWA_UNDER_VOLTAGE_LOCKOUT(pregb) ? 'U' : '-',
	    PMIC_REGB_GET_SWA_CURRENT_LIMITER_WARN(pregb) ? 'C' :
	    (PMIC_REG9_GET_SWA_HIGH_OUTPUT_CURRENT_WARN(preg9) ? 'c' : '-'));
	mdb_printf("        SWB: %c%c%c%c\n",
	    PMIC_REG8_GET_SWB_PWR_NOT_GOOD(preg8) ? 'P' : '-',
	    PMIC_REGA_GET_SWB_OVER_VOLTAGE(prega) ? 'O' : '-',
	    PMIC_REGB_GET_SWB_UNDER_VOLTAGE_LOCKOUT(pregb) ? 'U' : '-',
	    PMIC_REGB_GET_SWB_CURRENT_LIMITER_WARN(pregb) ? 'C' :
	    (PMIC_REG9_GET_SWB_HIGH_OUTPUT_CURRENT_WARN(preg9) ? 'c' : '-'));
	mdb_printf("        SWC: %c%c%c%c\n",
	    PMIC_REG8_GET_SWC_PWR_NOT_GOOD(preg8) ? 'P' : '-',
	    PMIC_REGA_GET_SWC_OVER_VOLTAGE(prega) ? 'O' : '-',
	    PMIC_REGB_GET_SWC_UNDER_VOLTAGE_LOCKOUT(pregb) ? 'U' : '-',
	    PMIC_REGB_GET_SWC_CURRENT_LIMITER_WARN(pregb) ? 'C' :
	    (PMIC_REG9_GET_SWC_HIGH_OUTPUT_CURRENT_WARN(preg9) ? 'c' : '-'));
	mdb_printf("        SWD: %c%c%c%c\n",
	    PMIC_REG8_GET_SWD_PWR_NOT_GOOD(preg8) ? 'P' : '-',
	    PMIC_REGA_GET_SWD_OVER_VOLTAGE(prega) ? 'O' : '-',
	    PMIC_REGB_GET_SWD_UNDER_VOLTAGE_LOCKOUT(pregb) ? 'U' : '-',
	    PMIC_REGB_GET_SWD_CURRENT_LIMITER_WARN(pregb) ? 'C' :
	    (PMIC_REG9_GET_SWD_HIGH_OUTPUT_CURRENT_WARN(preg9) ? 'c' : '-'));

	mdb_printf("\n");
	mdb_printf("             P - Power Not Good, "
	    "O - Over Voltage, U - Under Voltage\n");
	mdb_printf("             c - High Output Current Consumption, "
	    "C - Current Limited\n");
}

int
apob_event_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int ret;
	apob_gen_event_log_t log = { 0 };
	uint64_t class = APOB_EVC_ALERT;
	uintptr_t event, data_a, data_b;
	boolean_t event_set = FALSE, data_a_set = FALSE, data_b_set = FALSE;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_getopts(argc, argv,
		    'c', MDB_OPT_UINT64, &class,
		    'e', MDB_OPT_UINTPTR_SET, &event_set, &event,
		    'a', MDB_OPT_UINTPTR_SET, &data_a_set, &data_a,
		    'b', MDB_OPT_UINTPTR_SET, &data_b_set, &data_b, NULL) !=
		    argc) {
			return (DCMD_USAGE);
		}

		if (!event_set) {
			mdb_warn("event type must be given\n");
			return (DCMD_USAGE);
		} else if (event > UINT32_MAX) {
			mdb_warn("event type out of range\n");
			return (DCMD_USAGE);
		}
		log.agevl_events[0].aev_info = (uint32_t)event;

		if (class > UINT32_MAX) {
			mdb_warn("event class out of range\n");
			return (DCMD_USAGE);
		}
		log.agevl_events[0].aev_class = (uint32_t)class;

		if (data_a_set) {
			if (data_a > UINT32_MAX) {
				mdb_warn("event data 0 payload out of range\n");
				return (DCMD_USAGE);
			}
			log.agevl_events[0].aev_data0 = (uint32_t)data_a;
		}
		if (data_b_set) {
			if (data_b > UINT32_MAX) {
				mdb_warn("event data 1 payload out of range\n");
				return (DCMD_USAGE);
			}
			log.agevl_events[0].aev_data1 = (uint32_t)data_b;
		}

		log.agevl_count = 1;
	} else if (argc != 0) {
		mdb_warn("synthetic event log form does not take address\n");
		return (DCMD_USAGE);
	} else if ((ret = apob_read_entry(addr, APOB_GROUP_GENERAL,
	    APOB_GENERAL_TYPE_EVENT_LOG, &log, sizeof (log))) != DCMD_OK) {
		return (DCMD_ERR);
	}

	if (log.agevl_count > ARRAY_SIZE(log.agevl_events)) {
		mdb_warn("structure claims %u valid events, but only "
		    "%u are possible, limiting to %u\n", log.agevl_count,
		    ARRAY_SIZE(log.agevl_events), ARRAY_SIZE(log.agevl_events));
		log.agevl_count = ARRAY_SIZE(log.agevl_events);
	}

	for (uint16_t i = 0; i < log.agevl_count; i++) {
		const apob_event_t *event = &log.agevl_events[i];

		mdb_printf("EVENT %u\n", i);
		mdb_printf("  CLASS: %s (0x%x)\n",
		    apob_event_class_to_name(event->aev_class),
		    event->aev_class);
		mdb_printf("  EVENT: %s (0x%x)\n",
		    apob_event_info_to_name(event->aev_info), event->aev_info);
		mdb_printf("  DATA:  0x%x 0x%x\n", event->aev_data0,
		    event->aev_data1);

		switch (event->aev_info) {
		case APOB_EVENT_TRAIN_ERROR:
			apob_event_dcmd_print_train(event->aev_data0,
			    event->aev_data1);
			break;
		case APOB_EVENT_PMU_RETRY_TRAIN:
			apob_event_dcmd_print_retry_train(event->aev_data0);
			break;
		case APOB_EVENT_MEM_PMIC_ERROR:
			apob_event_dcmd_print_mem_pmic_error(event->aev_data0,
			    event->aev_data1);
			break;
		case APOB_EVENT_MEM_POP_ORDER:
			apob_event_dcmd_print_mem_pop_order(event->aev_data0);
			break;
		case APOB_EVENT_MEM_SPD_CRC_ERROR:
			apob_event_dcmd_print_spd_crc(event->aev_data0);
			break;
		case APOB_EVENT_PMIC_RT_ERROR:
			apob_event_dcmd_print_pmic_rt_error(event->aev_data0,
			    event->aev_data1);
			break;
		default:
			break;
		}
	}

	return (DCMD_OK);
}

static const char *apob_entryhelp =
"Print a summary of an APOB entry.  If known, the group name corresponding\n"
"to the apob_group_t enum variant is displayed.  The type, instance, data\n"
"size, and unknown group numbers are displayed in the default radix.\n"
"The following options are supported:\n"
"\n"
"  -r	Raw: do not interpret group and type in a cancelled entry\n"
"  -x	Do not print a cancelled entry\n"
"\n"
"Flags are displayed as follows:\n"
"\n"
"  C	Entry is cancelled by firmware\n"
"  S	Entry's data size is too short to be valid\n";

void
apob_entry_dcmd_help(void)
{
	mdb_printf(apob_entryhelp);
}

int
apob_entry_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	apob_entry_t ent;
	mdb_ctf_id_t id, idr;
	const char pfx[] = "APOB_GROUP_";
	const char *gname;
	size_t data_off = offsetof(apob_entry_t, ae_data);
	bool cancel = false;
	bool short_data = false;
	uint_t opts = 0;
	const uint_t OPT_RAW = (1U << 0);
	const uint_t OPT_EXCLUDE = (1U << 1);

	if ((flags & DCMD_ADDRSPEC) == 0)
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'r', MDB_OPT_SETBITS, OPT_RAW, &opts,
	    'x', MDB_OPT_SETBITS, OPT_EXCLUDE, &opts, NULL) != argc) {
		return (DCMD_USAGE);
	}

	if ((opts & (OPT_RAW | OPT_EXCLUDE)) == (OPT_RAW | OPT_EXCLUDE))
		return (DCMD_USAGE);

	if (mdb_vread(&ent, sizeof (ent), addr) != sizeof (ent)) {
		mdb_warn("failed to read APOB entry 0x%lx", addr);
		return (DCMD_ERR);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %2s %-10s %10s %10s %10s %?s%</u>\n",
		    "ADDR", "FL", "GROUP", "TYPE", "INSTANCE", "DATA SIZE",
		    "DATA ADDR");
	}

	/*
	 * Firmware seems to do an odd and of course undocumented thing where
	 * sometimes it wants to cancel an entry altogether, presumably after it
	 * has already laid out the APOB and reserved space for it.  When this
	 * happens, we see the upper 16 bits of both the group and type set to
	 * 0xffff and the contents of the data region filled with 'Z'.  By
	 * default we will detect this and report what has happened; optionally,
	 * the user can request raw output which leaves this exactly as it
	 * really is in memory.
	 */
	if (bitx32(ent.ae_group, 31, 16) == 0xffff &&
	    bitx32(ent.ae_type, 31, 16) == 0xffff) {
		cancel = true;
		if ((opts & OPT_RAW) == 0) {
			ent.ae_group = bitset32(ent.ae_group, 31, 16, 0);
			ent.ae_type = bitset32(ent.ae_type, 31, 16, 0);
		}
	}

	if (cancel && (opts & OPT_EXCLUDE) != 0)
		return (DCMD_OK);

	if (mdb_ctf_lookup_by_name("apob_group_t", &id) == 0 &&
	    mdb_ctf_type_resolve(id, &idr) == 0 &&
	    mdb_ctf_type_kind(idr) == CTF_K_ENUM) {
		gname = mdb_ctf_enum_name(idr, ent.ae_group);
	} else {
		gname = NULL;
	}

	if (ent.ae_size < data_off)
		short_data = true;

	if (gname != NULL && strncmp(gname, pfx, sizeof (pfx) - 1) == 0) {
		mdb_printf("%?p %c%c %-10s %10r %10r ",
		    addr, cancel ? 'C' : ' ', short_data ? 'S' : ' ',
		    gname + sizeof (pfx) - 1, ent.ae_type, ent.ae_inst);
	} else {
		mdb_printf("%?p %c%c %-10r %10r %10r ",
		    addr, cancel ? 'C' : ' ', short_data ? 'S' : ' ',
		    ent.ae_group, ent.ae_type, ent.ae_inst);
	}

	if (ent.ae_size > data_off) {
		mdb_printf("%10r %?p\n",
		    ent.ae_size - data_off, addr + data_off);
	} else if (ent.ae_size == data_off) {
		mdb_printf("%10r %?s\n", 0, "-");
	} else {
		mdb_printf("%10s %?s\n", "-", "-");
	}

	return (DCMD_OK);
}

static const char *apob_target_help =
"Some APOB structures differ between processor families (e.g., max number of\n"
"memory channels). In cases where we can't determine the target's CPU, e.g.,\n"
"while inspecting a previously saved APOB or a dump from a different system,\n"
"this command may be used to set an override.\n"
"The following are the currently supported CPUs:\n"
"\n"
"  - Milan\n"
"  - Genoa\n"
"  - Turin\n"
"\n"
"Passing no argument will print the current target.\n";

void
apob_target_dcmd_help(void)
{
	mdb_printf(apob_target_help);
}

int
apob_target_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc == 0) {
		mdb_printf("%s\n", apob_target_cpu);
		return (DCMD_OK);
	}

	if (argc != 1 || argv[0].a_type != MDB_TYPE_STRING ||
	    (flags & DCMD_ADDRSPEC)) {
		return (DCMD_USAGE);
	}

	if ((strcasecmp(argv[0].a_un.a_str, "milan")) == 0) {
		apob_set_target(X86_PF_AMD_MILAN);
	} else if ((strcasecmp(argv[0].a_un.a_str, "genoa")) == 0) {
		apob_set_target(X86_PF_AMD_GENOA);
	} else if ((strcasecmp(argv[0].a_un.a_str, "turin")) == 0) {
		apob_set_target(X86_PF_AMD_TURIN);
	} else {
		return (DCMD_USAGE);
	}
	return (DCMD_OK);
}

#ifdef	APOB_RAW_DMOD
static const mdb_dcmd_t dcmds[] = {
	APOB_DCMDS,
	{ NULL }
};

static const mdb_walker_t walkers[] = {
	APOB_WALKERS,
	{ NULL }
};

static const mdb_modinfo_t modinfo = {
	.mi_dvers	= MDB_API_VERSION,
	.mi_dcmds	= dcmds,
	.mi_walkers	= walkers,
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
#endif	/* APOB_RAW_DMOD */
