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
 * This file contains various utility functions for the Oxide oxhc topo module.
 */

#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/ccompile.h>
#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>

#include "oxhc.h"
#include "oxhc_util.h"

/*
 * These codes are registered in OANA - https://github.com/oxidecomputer/oana
 */
static struct {
	const char *tov_code;
	const char *tov_name;
} topo_oxhc_vendor_names[] = {
	{ "HPM", "Herold Precision Metals" },
	{ "MIC", "Micron Technology" },
	{ "SND", "Sandisk" },
	{ "SYD", "Sanyo Denki" },
	{ "WDC", "Western Digital Corporation" }
};

const char *
topo_oxhc_vendor_name(const char *key)
{
	for (size_t i = 0; i < ARRAY_SIZE(topo_oxhc_vendor_names); i++) {
		if (strcmp(key, topo_oxhc_vendor_names[i].tov_code) == 0)
			return (topo_oxhc_vendor_names[i].tov_name);
	}
	return (NULL);
}

bool
topo_oxhc_barcode_parse(topo_mod_t *mod, const oxhc_t *oxhc __unused,
    const uint8_t *bstr, size_t blen, oxhc_barcode_t *bar)
{
	char buf[OXHC_MPN1_BARCODE_MAXLEN + 1];
	char *tok, *cursor;

	if (bstr == NULL || blen == 0)
		return (false);

	if (blen >= sizeof (buf)) {
		topo_mod_dprintf(mod, "Barcode too long: '%.*s'", blen, bstr);
		return (false);
	}

	bzero(bar, sizeof (*bar));
	bcopy(bstr, buf, blen);
	buf[blen] = '\0';

	cursor = buf;
	tok = strsep(&cursor, ":");
	if (tok == NULL) {
		topo_mod_dprintf(mod,
		    "Could not extract barcode prefix from '%.*s'", blen, bstr);
		return (false);
	}

	/*
	 * The SP normalises 0XV1 barcodes to 0XV2 format so we don't need to
	 * cater for those. It also normalises the erroneous prefixes that used
	 * the letter O in place of the 0.
	 */
	if (strcmp(tok, OXHC_BARCODE_0XV2_PFX) == 0) {
		bar->ob_type = OXBC_0XV2;
	} else if (strcmp(tok, OXHC_BARCODE_MPN1_PFX) == 0) {
		bar->ob_type = OXBC_MPN1;
	} else {
		topo_mod_dprintf(mod,
		    "Unknown barcode format '%s' found in '%.*s'",
		    tok, blen, bstr);
		return (false);
	}

	if (bar->ob_type == OXBC_MPN1) {
		/* Extract the manufacturer portion */
		tok = strsep(&cursor, ":");
		if (tok == NULL) {
			topo_mod_dprintf(mod,
			    "Could not extract barcode MFG from '%.*s'",
			    blen, bstr);
			return (false);
		}
		if (strlen(tok) != sizeof (bar->ob_mfg)) {
			topo_mod_dprintf(mod,
			    "Barcode MFG field must be %u characters, "
			    "found '%s' in '%.*s'",
			    sizeof (bar->ob_mfg), tok, blen, bstr);
			return (false);
		}
		bcopy(tok, bar->ob_mfg, strlen(tok));
	}

	if (cursor == NULL) {
		topo_mod_dprintf(mod, "Barcode truncated '%.*s'", blen, bstr);
		return (false);
	}

	/* Part "number" */
	tok = strsep(&cursor, ":");
	if (tok == NULL || strlen(tok) > sizeof (bar->ob_pn)) {
		topo_mod_dprintf(mod,
		    "Could not extract barcode PN from '%.*s'", blen, bstr);
		if (bar->ob_type < OXBC_MPN1)
			return (false);
	} else {
		bcopy(tok, bar->ob_pn, strlen(tok));
	}

	if (cursor == NULL) {
		topo_mod_dprintf(mod, "Barcode truncated '%.*s'", blen, bstr);
		return (false);
	}

	/* Part revision */
	tok = strsep(&cursor, ":");
	if (tok == NULL || strlen(tok) > sizeof (bar->ob_rev)) {
		topo_mod_dprintf(mod,
		    "Could not extract barcode REV from '%.*s'", blen, bstr);
		if (bar->ob_type < OXBC_MPN1)
			return (false);
	} else {
		if (bar->ob_type < OXBC_MPN1) {
			if (strlen(tok) != 3 || !isdigit(tok[0]) ||
			    !isdigit(tok[1]) || !isdigit(tok[2])) {
				topo_mod_dprintf(mod,
				    "OXVx barcode REV fields must be 3 numeric "
				    "characters, found '%s' in '%.*s'",
				    tok, blen, bstr);
				return (false);
			}
			/* Elide leading 0 from revisions in 0XVx barcodes */
			while (tok[0] == '0' && tok[1] != '\0')
				tok++;
		}
		bcopy(tok, bar->ob_rev, strlen(tok));
	}

	if (cursor == NULL) {
		topo_mod_dprintf(mod, "Barcode truncated '%.*s'", blen, bstr);
		return (false);
	}

	/* Serial "number" */
	tok = strsep(&cursor, ":");
	if (tok == NULL || strlen(tok) > sizeof (bar->ob_sn)) {
		topo_mod_dprintf(mod,
		    "Could not extract barcode SN from '%.*s'", blen, bstr);
		if (bar->ob_type < OXBC_MPN1)
			return (false);
	} else {
		bcopy(tok, bar->ob_sn, strlen(tok));
	}

	if (cursor != NULL) {
		topo_mod_dprintf(mod,
		    "Trailing data '%s' found at end of barcode '%.*s'",
		    cursor, blen, bstr);
		return (false);
	}

	return (true);
}
