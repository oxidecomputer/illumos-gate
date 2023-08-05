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
 * This file's role is to interface with the ipcc(4D) driver's inventory
 * capabilities. Because the service processor does not cache most of this
 * information per se and it is basically static across our lifetime (the SP
 * cannot update without us going down along for the ride), we opt to cache this
 * information at our end.
 *
 * Once we complete a successful read of all elements in the nvlist_t array
 * without getting any IPCC-level I/O errors, then we will proceed to cache
 * this data. Any cache that we create is likely to be wrong at some point.
 * Right now we have a forced expiry after a number of hours with some random
 * component to reduce the likelihood that everything does this at the same
 * time.
 *
 * Currently the only thing that expires the cache other than bad data is time.
 * This probably needs to be improved to deal with changes in the SP state or
 * related. It mostly works due to the tied lifetime; however, if there was a
 * flaky connection to a device it means we'll be caching that something is
 * missing or that there was an I/O error at the inventory level for a larger
 * period of time which isn't great. Figuring out a better refresh pattern is an
 * area of future work.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stdbool.h>
#include <endian.h>
#include <librename.h>
#include <sys/mman.h>
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

/*
 * Definitions for the packed nvlist cache file.
 */
#define	OXHC_INV_CACHEDIR	"/var/run"
#define	OXHC_INV_CACHENAME	"ipcc_inventory.nvlist"
#define	OXHC_INV_CHUNK		(128 * 1024)
#define	OXHC_INV_NVL_NENTS	"inventory-entries"	/* uint32 */
#define	OXHC_INV_NVL_VERS	"version"		/* uint32 */
#define	OXHC_INV_NVL_HRTIME	"generated-hrtime"	/* int64 */

/*
 * Cache expiration time, fixed and random components. All times are in seconds.
 * The four hour base is pretty arbitrary.
 */
#define	OXHC_INV_TIME_BASE	(NANOSEC * 60ULL * 60ULL * 4ULL)
#define	OXHC_INV_TIME_RAND_SEC	(60 * 30)

static void
topo_oxhc_nvl_write(topo_mod_t *mod, const char *data, size_t len)
{
	int ret, fd;
	librename_atomic_t *lra;
	size_t off;

	if ((ret = librename_atomic_init(OXHC_INV_CACHEDIR, OXHC_INV_CACHENAME,
	    NULL, 0400, LIBRENAME_ATOMIC_NOUNLINK, &lra)) != 0) {
		topo_mod_dprintf(mod, "failed to initialize librename: %s",
		    strerror(ret));
		return;
	}

	fd = librename_atomic_fd(lra);
	off = 0;
	while (len > 0) {
		size_t towrite = MIN(OXHC_INV_CHUNK, len);
		ssize_t sret = write(fd, data + off, towrite);
		if (sret == -1 && errno == EINTR)
			continue;
		if (sret == -1) {
			topo_mod_dprintf(mod, "failed to write to temporary "
			    "file: %s", strerror(errno));
			(void) librename_atomic_abort(lra);
			goto done;
		}

		off += sret;
		len -= sret;
	}

	do {
		ret = librename_atomic_commit(lra);
	} while (ret == EINTR);

	if (ret != 0 && ret != EINTR) {
		(void) librename_atomic_abort(lra);
	} else {
		topo_mod_dprintf(mod, "successfully persisted IPCC inventory "
		    "data");
	}

done:
	librename_atomic_fini(lra);
}

static void
topo_oxhc_inventory_persist(topo_mod_t *mod, oxhc_t *oxhc)
{
	int ret;
	nvlist_t *nvl = NULL;
	char *pack_data = NULL;
	size_t pack_len = 0;

	if ((ret = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		topo_mod_dprintf(mod, "failed to persist data: could not "
		    "allocate nvlist_t: %s", strerror(ret));
		return;
	}

	if (nvlist_add_uint32(nvl, OXHC_INV_NVL_NENTS, oxhc->oxhc_ninv) != 0 ||
	    nvlist_add_uint32(nvl, OXHC_INV_NVL_VERS, IPCC_INV_VERS) !=
	    0 || nvlist_add_int64(nvl, OXHC_INV_NVL_HRTIME, gethrtime()) != 0) {
		topo_mod_dprintf(mod, "failed to persist data: could not add "
		    "basic items to nvlist");
		goto done;
	}

	for (uint32_t i = 0; i < oxhc->oxhc_ninv; i++) {
		char name[32];

		(void) snprintf(name, sizeof (name), "inventory-%u", i);
		ret = nvlist_add_byte_array(nvl, name,
		    (uchar_t *)&oxhc->oxhc_inv[i].oii_ipcc,
		    sizeof (oxhc->oxhc_inv[i].oii_ipcc));
		if (ret != 0) {
			topo_mod_dprintf(mod, "failed to persist data: could "
			    "not add %s data: %s", name, strerror(ret));
			goto done;
		}
	}

	if ((ret = nvlist_pack(nvl, &pack_data, &pack_len, NV_ENCODE_NATIVE,
	    0)) != 0) {
		topo_mod_dprintf(mod, "failed to persist data: could not pack "
		    "data: %s", strerror(ret));
		goto done;
	}

	topo_oxhc_nvl_write(mod, pack_data, pack_len);
done:
	free(pack_data);
	nvlist_free(nvl);
}

/*
 * Attempt to load the data from our cache file if it exists and we consider it
 * still valid. If we fail to do so or we have a version / data count mismatch
 * then we'll ignore the cache.
 */
static bool
topo_oxhc_inventory_restore(topo_mod_t *mod, oxhc_t *oxhc)
{
	int fd = -1, ret;
	char buf[PATH_MAX];
	struct stat st;
	void *addr = MAP_FAILED;
	bool bret = false;
	nvlist_t *nvl = NULL;
	uint32_t nents, vers;
	int64_t ctime, exp;
	hrtime_t now;

	if (snprintf(buf, sizeof (buf), "%s/%s", OXHC_INV_CACHEDIR,
	    OXHC_INV_CACHENAME) >= sizeof (buf)) {
		topo_mod_dprintf(mod, "failed to construct cache file path: "
		    "would have overflowed buffer");
		goto err;
	}

	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		topo_mod_dprintf(mod, "failed to open IPCC cache file: %s\n",
		    strerror(errno));
		goto err;
	}

	if (fstat(fd, &st) < 0) {
		topo_mod_dprintf(mod, "failed to stat the IPCC cache fd: %s\n",
		    strerror(errno));
		goto err;
	}

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		topo_mod_dprintf(mod, "failed to map the IPCC cache fd: %s\n",
		    strerror(errno));
		goto err;
	}

	ret = nvlist_unpack(addr, st.st_size, &nvl, 0);
	if (ret != 0) {
		topo_mod_dprintf(mod, "failed to unpack the IPCC cache: %s",
		    strerror(ret));
		goto err;
	}

	if ((ret = nvlist_lookup_pairs(nvl, 0,
	    OXHC_INV_NVL_NENTS, DATA_TYPE_UINT32, &nents,
	    OXHC_INV_NVL_VERS, DATA_TYPE_UINT32, &vers,
	    OXHC_INV_NVL_HRTIME, DATA_TYPE_INT64, &ctime, NULL)) != 0) {
		topo_mod_dprintf(mod, "failed to look up cache data: %s",
		    strerror(ret));
		goto err;
	}

	if (vers != IPCC_INV_VERS) {
		topo_mod_dprintf(mod, "cached IPCC inventory from unsupported "
		    "version: %u", vers);
		goto err;
	}

	if (nents != oxhc->oxhc_ninv) {
		topo_mod_dprintf(mod, "cached IPCC inventory has different "
		    "entry count (%u) than expected from SP (%u)", nents,
		    oxhc->oxhc_ninv);
		goto err;
	}

	now = gethrtime();
	exp = ctime + OXHC_INV_TIME_BASE +
	    arc4random_uniform(OXHC_INV_TIME_RAND_SEC) * NANOSEC;
	if (now > exp) {
		topo_mod_dprintf(mod, "IPCC cache keep-alive expired (cache "
		    "hrtime: %" PRIx64 ", exp hrtime: %" PRIx64 ", hrtime: %"
		    PRIx64 "): regenerating", ctime, exp, now);
		return (false);
	}

	for (uint32_t i = 0; i < oxhc->oxhc_ninv; i++) {
		char name[32];
		uchar_t *data;
		uint_t data_len;

		(void) snprintf(name, sizeof (name), "inventory-%u", i);
		if ((ret = nvlist_lookup_byte_array(nvl, name, &data,
		    &data_len)) != 0) {
			topo_mod_dprintf(mod, "cached data did not contain "
			    "key %s: %s", name, strerror(ret));
			goto err;
		}

		if (data_len != sizeof (oxhc->oxhc_inv[i].oii_ipcc)) {
			topo_mod_dprintf(mod, "key %s has wrong length: found "
			    "0x%x, expected 0x%zx", name, data_len,
			    sizeof (oxhc->oxhc_inv[i].oii_ipcc));
			goto err;
		}

		(void) memcpy(&oxhc->oxhc_inv[i].oii_ipcc, data, data_len);
	}

	/*
	 * Now that we have successfully loaded all data from the cache, go
	 * ahead and mark everything valid.
	 */
	for (uint32_t i = 0; i < oxhc->oxhc_ninv; i++) {
		oxhc->oxhc_inv[i].oii_valid = true;
	}

	bret = true;

err:
	nvlist_free(nvl);

	if (addr != MAP_FAILED) {
		VERIFY0(munmap(addr, st.st_size));
	}

	if (fd >= 0) {
		VERIFY0(close(fd));
	}

	return (bret);
}

void
topo_oxhc_inventory_fini(topo_mod_t *mod, oxhc_t *oxhc)
{
	if (oxhc->oxhc_ninv > 0) {
		topo_mod_free(mod, oxhc->oxhc_inv, oxhc->oxhc_ninv *
		    sizeof (oxhc_ipcc_inv_t));
		oxhc->oxhc_ninv = 0;
		oxhc->oxhc_inv = NULL;
	}
}

static bool
topo_oxhc_inventory_kinfo(topo_mod_t *mod, int ipcc_fd, ipcc_inv_key_t *inv_key)
{
	ipcc_keylookup_t key;

	(void) memset(inv_key, 0, sizeof (ipcc_inv_key_t));
	(void) memset(&key, 0, sizeof (ipcc_keylookup_t));
	key.ik_key = IPCC_INV_KEYNO;
	key.ik_buflen = sizeof (ipcc_inv_key_t);
	key.ik_buf = (uint8_t *)inv_key;

	if (ioctl(ipcc_fd, IPCC_KEYLOOKUP, &key) != 0) {
		topo_mod_dprintf(mod, "failed to look up inventory key from "
		    "the SP: %s", strerror(errno));
		return (false);
	}

	return (true);
}

int
topo_oxhc_inventory_init(topo_mod_t *mod, int ipcc_fd, oxhc_t *oxhc)
{
	uint32_t nioc_fail;
	ipcc_inv_key_t inv_key;

	if (!topo_oxhc_inventory_kinfo(mod, ipcc_fd, &inv_key)) {
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	if (inv_key.iki_vers != IPCC_INV_VERS) {
		topo_mod_dprintf(mod, "oxhc module does not support IPCC "
		    "inventory version %u\n", inv_key.iki_vers);
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	oxhc->oxhc_inv = topo_mod_zalloc(mod, sizeof (oxhc_ipcc_inv_t) *
	    inv_key.iki_nents);
	if (oxhc->oxhc_inv == NULL) {
		topo_mod_dprintf(mod, "failed to allocate memory for IPCC "
		    "inventory: %s\n", strerror(errno));
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}
	oxhc->oxhc_ninv = inv_key.iki_nents;

	if (topo_oxhc_inventory_restore(mod, oxhc)) {
		topo_mod_dprintf(mod, "loaded IPCC inventory from cache");
		return (0);
	}

	nioc_fail = 0;
	for (uint32_t i = 0; i < inv_key.iki_nents; i++) {
		oxhc_ipcc_inv_t *oinv = &oxhc->oxhc_inv[i];
		oinv->oii_ipcc.iinv_idx = i;

		if (ioctl(ipcc_fd, IPCC_INVENTORY, &oinv->oii_ipcc) != 0) {
			nioc_fail++;
			continue;
		}

		oinv->oii_valid = true;
	}

	topo_mod_dprintf(mod, "loaded %u inventory items (%u failed) via IPCC",
	    inv_key.iki_nents, nioc_fail);

	if (nioc_fail == inv_key.iki_nents) {
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	/*
	 * If we successfully were able to communicate with the SP for
	 * everything, then we will store this information.
	 */
	if (nioc_fail == 0) {
		topo_oxhc_inventory_persist(mod, oxhc);
	}

	return (0);
}

const ipcc_inventory_t *
topo_oxhc_inventory_find(const oxhc_t *oxhc, const char *refdes)
{
	if (refdes == NULL) {
		return (NULL);
	}

	for (size_t i = 0; i < oxhc->oxhc_ninv; i++) {
		if (!oxhc->oxhc_inv[i].oii_valid)
			continue;
		if (strcasecmp(refdes,
		    (const char *)oxhc->oxhc_inv[i].oii_ipcc.iinv_name) == 0) {
			return (&oxhc->oxhc_inv[i].oii_ipcc);
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
topo_oxhc_inventory_bcopy(const ipcc_inventory_t *inv, ipcc_inv_type_t exp_type,
    void *dest, size_t destlen, size_t minlen)
{
	if (inv->iinv_res != IPCC_INVENTORY_SUCCESS) {
		return (false);
	}

	if (inv->iinv_type != exp_type) {
		return (false);
	}

	if (inv->iinv_data_len < minlen) {
		return (false);
	}

	if (destlen > inv->iinv_data_len) {
		(void) memcpy(dest, inv->iinv_data, inv->iinv_data_len);
		(void) memset(dest + inv->iinv_data_len, 0,
		    destlen - inv->iinv_data_len);
	} else {
		(void) memcpy(dest, inv->iinv_data, destlen);
	}

	return (true);
}
