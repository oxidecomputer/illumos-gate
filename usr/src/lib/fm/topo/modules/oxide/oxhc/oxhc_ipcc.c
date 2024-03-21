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
 * This file's role is to interface with libipcc's inventory capabilities.
 * Because the service processor does not cache most of this information per se
 * and it is basically static across our lifetime (the SP cannot update without
 * us going down along for the ride), we ask the library to use a cache for
 * this information.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stdbool.h>
#include <endian.h>
#include <sys/debug.h>

#include "oxhc.h"

/*
 * The IPCC interface is defined as always using little-endian encoding. We
 * are not currently doing any endianness logic and checking in the key lookup
 * or other data structures and just assuming what is here.
 */
#if __BYTE_ORDER != __LITTLE_ENDIAN
#error "This file has not been ported to handle big-endian systems"
#endif

void
topo_oxhc_libipcc_error(topo_mod_t *mod, libipcc_handle_t *lih,
    const char *prefix)
{
	topo_mod_dprintf(mod, "%s: %s: %s (libipcc: 0x%x, sys: %d)\n",
	    prefix, libipcc_errmsg(lih), libipcc_strerror(libipcc_err(lih)),
	    libipcc_err(lih), libipcc_syserr(lih));
}

void
topo_oxhc_inventory_fini(topo_mod_t *mod, oxhc_t *oxhc)
{
	for (size_t i = 0; i < oxhc->oxhc_ninv; i++) {
		libipcc_inv_free(oxhc->oxhc_inv[i]);
	}

	topo_mod_free(mod, oxhc->oxhc_inv, oxhc->oxhc_ninv *
	    sizeof (libipcc_inv_t *));
	oxhc->oxhc_ninv = 0;
	oxhc->oxhc_inv = NULL;
}

int
topo_oxhc_inventory_init(topo_mod_t *mod, libipcc_handle_t *lih, oxhc_t *oxhc)
{
	libipcc_inv_handle_t *liih = NULL;
	uint32_t ver, nents;

	if (!libipcc_inv_hdl_init(lih, &ver, &nents,
	    LIBIPCC_INV_INIT_CACHE, &liih)) {
		topo_oxhc_libipcc_error(mod, lih,
		    "failed to initialize inventory");
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	if (ver != IPCC_INV_VERS) {
		topo_mod_dprintf(mod, "oxhc module does not support IPCC "
		    "inventory version %u\n", ver);
		libipcc_inv_hdl_fini(liih);
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	oxhc->oxhc_inv = topo_mod_zalloc(mod,
	    sizeof (libipcc_inv_t *) * nents);
	if (oxhc->oxhc_inv == NULL) {
		topo_mod_dprintf(mod, "failed to allocate memory for IPCC "
		    "inventory: %s\n", strerror(errno));
		libipcc_inv_hdl_fini(liih);
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}
	oxhc->oxhc_ninv = nents;

	for (uint32_t i = 0; i < nents; i++) {
		if (!libipcc_inv(lih, liih, i, &oxhc->oxhc_inv[i])) {
			char error[1024];

			(void) snprintf(error, sizeof (error),
			    "inventory lookup failure for index %" PRIu32, i);
			topo_oxhc_libipcc_error(mod, lih, error);
		}
	}

	libipcc_inv_hdl_fini(liih);
	return (0);
}

libipcc_inv_t *
topo_oxhc_inventory_find(const oxhc_t *oxhc, const char *refdes)
{
	if (refdes == NULL) {
		return (NULL);
	}

	for (size_t i = 0; i < oxhc->oxhc_ninv; i++) {
		libipcc_inv_t *inv = oxhc->oxhc_inv[i];
		const char *name;
		size_t len;

		if (inv == NULL)
			continue;

		name = (const char *)libipcc_inv_name(inv, &len);

		if (strcasecmp(refdes, name) == 0) {
			return (inv);
		}
	}

	return (NULL);
}

/*
 * This is designed to be similar in spirit to smbios_info_bcopy(). This will
 * copy as much data as it can into dest and zero anything that remains. If this
 * is the wrong type or the data is not considered valid, then we will return an
 * error. The caller may optionally have a minimum required length that it'll
 * accept. This is useful for ensuring that we have all of the basics of a given
 * form of a structure, but as these get extended we'll need to work through a
 * bit more here and this API will probably want to change. Unlike SMBIOS, it is
 * unlikely that all extensions will be able to treat a zero as invalid data.
 */
bool
topo_oxhc_inventory_bcopy(libipcc_inv_t *inv, ipcc_inv_type_t exp_type,
    void *dest, size_t destlen, size_t minlen)
{
	const uint8_t *data;
	size_t len;

	if (inv == NULL ||
	    libipcc_inv_status(inv) != LIBIPCC_INV_STATUS_SUCCESS) {
		return (false);
	}

	if (libipcc_inv_type(inv) != exp_type) {
		return (false);
	}

	data = libipcc_inv_data(inv, &len);
	if (len < minlen) {
		return (false);
	}

	if (destlen > len) {
		(void) memcpy(dest, data, len);
		(void) memset(dest + len, 0, destlen - len);
	} else {
		(void) memcpy(dest, data, destlen);
	}

	return (true);
}

/*
 * This is a variant of our inventory copying that checks to see if a range of
 * bytes starting at a given offset is available. It will copy those and only
 * those into the output buffer. The assumption is that someone already has
 * validated that the types make sense and therefore we can assume that the data
 * offset is valid.
 */
bool
topo_oxhc_inventory_bcopyoff(libipcc_inv_t *inv, void *buf, size_t len,
    size_t off)
{
	const uint8_t *data;
	size_t datalen;

	if (inv == NULL ||
	    libipcc_inv_status(inv) != LIBIPCC_INV_STATUS_SUCCESS) {
		return (false);
	}

	data = libipcc_inv_data(inv, &datalen);
	if (len > datalen || off > datalen || (len + off) > datalen) {
		return (false);
	}

	(void) memcpy(buf, data + off, len);
	return (true);
}
