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

#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/sysmacros.h>
#include <sys/amdzen/ccx.h>
#include <sys/amdzen/umc.h>
#include <io/amdzen/amdzen.h>
#include <sys/apob.h>
#include <sys/apob_impl.h>
#include <sys/io/zen/apob.h>
#include <milan_apob.h>

#include "apob_mod.h"
#include "unix.h"

extern const char *milan_chan_map[8];
extern const char *genoa_chan_map[12];
extern const char *turin_chan_map[12];

static const char **chan_map;
static size_t chan_map_size;

typedef struct mdb_apob_apob_hdl {
	uintptr_t ah_header;
	size_t ah_len;
} mdb_apob_apob_hdl_t;

/*
 * Called on module init.
 */
void
apob_props_init(void)
{
	x86_chiprev_t chiprev;

	if (!target_chiprev(&chiprev))
		return;

	switch (_X86_CHIPREV_FAMILY(chiprev)) {
	case X86_PF_AMD_MILAN:
		chan_map = milan_chan_map;
		chan_map_size = ARRAY_SIZE(milan_chan_map);
		break;
	case X86_PF_AMD_GENOA:
		chan_map = genoa_chan_map;
		chan_map_size = ARRAY_SIZE(genoa_chan_map);
		break;
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		chan_map = turin_chan_map;
		chan_map_size = ARRAY_SIZE(turin_chan_map);
		break;
	default:
		mdb_warn("%s: unsupported AMD chiprev family: %u\n", __func__,
		    _X86_CHIPREV_FAMILY(chiprev));
	}
}

static const char *
chan_name(size_t chan)
{
	if (chan_map == NULL || chan >= chan_map_size)
		return ("?");
	return chan_map[chan];
}

/*
 * APOB walker.  The APOB is always mapped if mdb or kmdb can run.
 */
int
apob_walk_init(mdb_walk_state_t *wsp)
{
	uintptr_t apob_addr, start, end;
	size_t hdl_len = 0;
	const uint8_t apob_sig[4] = { 'A', 'P', 'O', 'B' };
	GElf_Sym hdlsym;
	mdb_apob_apob_hdl_t hdl;
	apob_header_t hdr;

	if (wsp->walk_addr != 0) {
		apob_addr = wsp->walk_addr;
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

	if (hdr.ah_sig[0] != apob_sig[0] ||
	    hdr.ah_sig[1] != apob_sig[1] ||
	    hdr.ah_sig[2] != apob_sig[2] ||
	    hdr.ah_sig[3] != apob_sig[3]) {
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
		return (mdb_walk("apob", apob_dcmd_cb, &data));
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
"tree-like to show the relationship between entities.\n";

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
	case APOB_EVENT_MEM_RRW_ERROR:
		return ("MBIST error");
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

int
apob_event_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int ret;
	apob_gen_event_log_t log;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if ((ret = apob_read_entry(addr, APOB_GROUP_GENERAL,
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
