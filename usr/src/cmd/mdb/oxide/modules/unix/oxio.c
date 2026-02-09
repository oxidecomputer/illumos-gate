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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * mdb dcmds for working with oxio data.
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ctf.h>
#include <sys/io/zen/oxio.h>
#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <sys/x86_archext.h>
#include <string.h>
#include "unix.h"

/*
 * Shadow structure for mdb_ctf_vread of oxio_engine_t.  We only need the
 * name pointer to identify engines.
 */
typedef struct {
	const char *oe_name;
} mdb_oxio_engine_t;

void
oxio_unlimit_help(void)
{
	mdb_printf(
	    "Clears the oe_tuning.ot_hw_limit field (resets to\n"
	    "OXIO_SPEED_GEN_MAX) for selected engines in the\n"
	    "oxio_cosmo array, removing the hardware speed limit.\n"
	    "This is intended to be used early in boot from kmdb to\n"
	    "allow devices to train at their maximum supported speed.\n"
	    "\n"
	    "Engines are selected by a substring of their oe_name string.\n"
	    "The special value 'all' matches all U.2 slot bridges.\n"
	    "\n%<b>Examples:%</b>\n"
	    "\t::oxio_unlimit all\n"
	    "\t::oxio_unlimit N0 N5\n");
}

int
oxio_unlimit_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	GElf_Sym arr_sym, n_sym;
	uintptr_t arr_addr;
	size_t nengines;
	ssize_t engine_sz, speed_sz;
	int tuning_off, hw_limit_off, field_off;
	mdb_oxide_board_data_t *board_data = get_board_data();
	mdb_ctf_id_t id, idr;
	uint_t ncleared = 0;

	if (flags & DCMD_ADDRSPEC)
		return (DCMD_USAGE);

	if (board_data->obd_board != OXIDE_BOARD_COSMO) {
		mdb_warn("only available on Oxide Cosmo systems\n");
		return (DCMD_ERR);
	}

	for (int i = 0; i < argc; i++) {
		if (argv[i].a_type != MDB_TYPE_STRING) {
			mdb_warn("argument %d is not a string\n", i + 1);
			return (DCMD_USAGE);
		}
	}

	if (mdb_lookup_by_name("oxio_cosmo", &arr_sym) == -1) {
		mdb_warn("failed to find 'oxio_cosmo' structure");
		return (DCMD_ERR);
	}
	arr_addr = arr_sym.st_value;

	if (mdb_lookup_by_name("oxio_cosmo_nengines", &n_sym) == -1) {
		mdb_warn("failed to find 'oxio_cosmo_nengines'");
		return (DCMD_ERR);
	}
	if (mdb_vread(&nengines, sizeof (nengines), n_sym.st_value) == -1) {
		mdb_warn("failed to read oxio_cosmo_nengines");
		return (DCMD_ERR);
	}

	engine_sz = mdb_ctf_sizeof_by_name("oxio_engine_t");
	if (engine_sz == -1) {
		mdb_warn("failed to look up sizeof (oxio_engine_t)");
		return (DCMD_ERR);
	}

	tuning_off = mdb_ctf_offsetof_by_name("oxio_engine_t", "oe_tuning");
	if (tuning_off == -1) {
		mdb_warn("failed to look up offset of oe_tuning");
		return (DCMD_ERR);
	}

	hw_limit_off = mdb_ctf_offsetof_by_name("oxio_tuning_t",
	    "ot_hw_limit");
	if (hw_limit_off == -1) {
		mdb_warn("failed to look up offset of ot_hw_limit");
		return (DCMD_ERR);
	}

	field_off = tuning_off + hw_limit_off;

	speed_sz = mdb_ctf_sizeof_by_name("oxio_speed_t");
	if (speed_sz == -1) {
		mdb_warn("failed to look up sizeof (oxio_speed_t)");
		return (DCMD_ERR);
	}

	if (mdb_ctf_lookup_by_name("oxio_speed_t", &id) != 0 ||
	    mdb_ctf_type_resolve(id, &idr) != 0) {
		mdb_warn("failed to look up oxide_speed_t enum definition");
	} else if (mdb_ctf_type_kind(idr) != CTF_K_ENUM) {
		mdb_warn("found oxide_speed_t but it is not an enum, "
		    "rather it has type 0x%x\n", mdb_ctf_type_kind(idr));
	}

	for (size_t i = 0; i < nengines; i++) {
		uintptr_t eng_addr = arr_addr + i * engine_sz;
		uintptr_t limit_addr = eng_addr + field_off;
		mdb_oxio_engine_t eng;
		char name[128];
		oxio_speed_t old;

		if (mdb_ctf_vread(&eng, "oxio_engine_t",
		    "mdb_oxio_engine_t", eng_addr, 0) == -1) {
			mdb_warn("failed to read oxio_engine_t at %p",
			    eng_addr);
			continue;
		}

		if (mdb_readstr(name, sizeof (name),
		    (uintptr_t)eng.oe_name) <= 0) {
			mdb_warn("failed to read name from oxio engine %zu", i);
			continue;
		}

		/*
		 * This command is only intended to operate on the bridges
		 * above U.2 drives in an Oxide Cosmo sled. These all have
		 * an `oe_name` field that begins with this string, for example
		 * "U.2 N0 (A)". If this is not such a bridge, move on to the
		 * next.
		 */
		if (strncmp(name, "U.2 N", 5) != 0)
			continue;

		if (mdb_vread(&old, speed_sz, limit_addr) != speed_sz) {
			mdb_warn("failed to read current limit "
			    "for '%s' at %p", name, limit_addr);
			old = OXIO_SPEED_GEN_MAX;
		}

		if (argc == 0) {
			/* Report only */
			mdb_printf("[%s] - %s\n", name,
			    mdb_ctf_enum_name(idr, old));
			continue;
		}

		bool update = false;

		for (int j = 0; j < argc; j++) {
			if (strcmp(argv[j].a_un.a_str, "all") == 0 ||
			    strstr(name, argv[j].a_un.a_str) != NULL) {
				update = true;
				break;
			}
		}

		if (update) {
			const uint64_t zero = 0;

			if (mdb_vwrite(&zero, speed_sz, limit_addr) !=
			    speed_sz) {
				mdb_warn("failed to clear ot_hw_limit "
				    "for '%s' at %p", name, limit_addr);
				return (DCMD_ERR);
			}
			mdb_printf("Removed speed restriction on %s (was %s)\n",
			    name, mdb_ctf_enum_name(idr, old));

			ncleared++;
		}
	}

	if (argc != 0 && ncleared == 0) {
		mdb_warn("no matching engines found in oxio_cosmo\n");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}
