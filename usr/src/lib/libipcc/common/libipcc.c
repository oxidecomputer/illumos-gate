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
 * Library routines that interface with the Oxide Inter-Processor
 * Communications Channel (IPCC) driver in order to send commands to,
 * and retrieve data from, the service processor in Oxide hardware.
 *
 * The interfaces herein are MT-Safe only if each thread within a
 * multi-threaded caller uses its own library handle.
 */

#include <libipcc.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/ethernet.h>
#include <sys/ipcc.h>
#include <sys/ipcc_inventory.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#define	SUPPORTED_IPCC_VERSION	1

/* All supported key lookup/set flags */
#define	LIBIPCC_KEYF_ALL	LIBIPCC_KEYF_COMPRESSED

/* Maximum size of an internal error message */
#define	LIBIPCC_ERR_LEN	1024

struct libipcc_handle {
	int			lih_fd;
	uint_t			lih_version;
	/* Last error information */
	libipcc_err_t		lih_err;
	int32_t			lih_syserr;
	char			lih_errmsg[LIBIPCC_ERR_LEN];
};

libipcc_err_t
libipcc_err(libipcc_handle_t *lih)
{
	return (lih->lih_err);
}

int32_t
libipcc_syserr(libipcc_handle_t *lih)
{
	return (lih->lih_syserr);
}

const char *
libipcc_errmsg(libipcc_handle_t *lih)
{
	return (lih->lih_errmsg);
}

const char *
libipcc_strerror(libipcc_err_t err)
{
	switch (err) {
	case LIBIPCC_ERR_OK:
		return ("LIBIPCC_ERR_OK");
	case LIBIPCC_ERR_NO_MEM:
		return ("LIBIPCC_ERR_NO_MEM");
	case LIBIPCC_ERR_INVALID_PARAM:
		return ("LIBIPCC_ERR_INVALID_PARAM");
	case LIBIPCC_ERR_INTERNAL:
		return ("LIBIPCC_ERR_INTERNAL");
	default:
		break;
	}
	return ("Unknown error");
}

static bool __PRINTFLIKE(4)
libipcc_error(libipcc_handle_t *lih, libipcc_err_t err, int syserr,
    const char *fmt, ...)
{
	va_list ap;

	lih->lih_err = err;
	lih->lih_syserr = syserr;
	va_start(ap, fmt);
	(void) vsnprintf(lih->lih_errmsg, sizeof (lih->lih_errmsg), fmt, ap);
	va_end(ap);

	return (false);
}

static bool
libipcc_success(libipcc_handle_t *lih)
{
	lih->lih_err = LIBIPCC_ERR_OK;
	lih->lih_syserr = 0;
	lih->lih_errmsg[0] = '\0';

	return (true);
}

static bool __PRINTFLIKE(7)
libipcc_init_error(libipcc_err_t *libipcc_errp, int32_t *syserrp,
    char * const errmsg, size_t errlen,
    libipcc_err_t err, int32_t syserr, const char *fmt, ...)
{
	if (libipcc_errp != NULL)
		*libipcc_errp = err;
	if (syserrp != NULL)
		*syserrp = syserr;
	if (errmsg != NULL) {
		va_list ap;

		va_start(ap, fmt);
		(void) vsnprintf(errmsg, errlen, fmt, ap);
		va_end(ap);
	}

	return (false);
}

bool
libipcc_init(libipcc_handle_t **lihp, libipcc_err_t *libipcc_errp,
    int32_t *syserrp, char * const errmsg, size_t errlen)
{
	libipcc_handle_t *lih;
	int version;

	*lihp = NULL;
	lih = calloc(1, sizeof (*lih));

	if (lih == NULL) {
		return (libipcc_init_error(libipcc_errp, syserrp, errmsg,
		    errlen, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for handle: %s",
		    strerror(errno)));
	}

	lih->lih_fd = open(IPCC_DEV, O_RDWR);
	if (lih->lih_fd == -1) {
		(void) libipcc_init_error(libipcc_errp, syserrp, errmsg,
		    errlen, LIBIPCC_ERR_INTERNAL, errno,
		    "failed to open IPCC device '%s': %s", IPCC_DEV,
		    strerror(errno));
		free(lih);
		return (false);
	}

	version = ioctl(lih->lih_fd, IPCC_GET_VERSION, 0);
	if (version < 0) {
		(void) libipcc_init_error(libipcc_errp, syserrp, errmsg,
		    errlen, LIBIPCC_ERR_INTERNAL, errno,
		    "failed to retrieve kernel IPCC version: %s",
		    strerror(errno));
		free(lih);
		return (false);
	}

	lih->lih_version = (uint_t)version;

	if (lih->lih_version != SUPPORTED_IPCC_VERSION) {
		(void) libipcc_init_error(libipcc_errp, syserrp, errmsg,
		    errlen, LIBIPCC_ERR_INTERNAL, errno,
		    "unsupported kernel IPCC version; got %u, need %u",
		    lih->lih_version, SUPPORTED_IPCC_VERSION);
		free(lih);
		return (false);
	}

	*lihp = lih;
	return (libipcc_success(lih));
}

void
libipcc_fini(libipcc_handle_t *lih)
{
	if (lih == NULL)
		return;

	VERIFY0(close(lih->lih_fd));
	free(lih);
}

bool
libipcc_status(libipcc_handle_t *lih, uint64_t *valp)
{
	ipcc_status_t status;

	if (ioctl(lih->lih_fd, IPCC_STATUS, &status) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_STATUS) failed: %s", strerror(errno)));
	}

	*valp = status.is_status;

	return (libipcc_success(lih));
}

bool
libipcc_startup_options(libipcc_handle_t *lih, uint64_t *valp)
{
	ipcc_status_t status;

	if (ioctl(lih->lih_fd, IPCC_STATUS, &status) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_STATUS) failed: %s", strerror(errno)));
	}

	*valp = status.is_startup;

	return (libipcc_success(lih));
}

bool
libipcc_ident(libipcc_handle_t *lih, libipcc_ident_t **identp)
{
	ipcc_ident_t *ident;

	ident = calloc(1, sizeof (*ident));
	if (ident == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for ident: %s",
		    strerror(errno)));
	}

	if (ioctl(lih->lih_fd, IPCC_IDENT, ident) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_IDENT) failed: %s", strerror(errno));
		free(ident);
		return (false);
	}

	*identp = (libipcc_ident_t *)ident;

	return (libipcc_success(lih));
}

const uint8_t *
libipcc_ident_serial(libipcc_ident_t *identp)
{
	ipcc_ident_t *ident = (ipcc_ident_t *)identp;

	return (ident->ii_serial);
}

const uint8_t *
libipcc_ident_model(libipcc_ident_t *identp)
{
	ipcc_ident_t *ident = (ipcc_ident_t *)identp;

	return (ident->ii_model);
}

uint32_t
libipcc_ident_rev(libipcc_ident_t *identp)
{
	ipcc_ident_t *ident = (ipcc_ident_t *)identp;

	return (ident->ii_rev);
}

void
libipcc_ident_free(libipcc_ident_t *identp)
{
	free(identp);
}

bool
libipcc_imageblock(libipcc_handle_t *lih, uint8_t *hash, size_t hashlen,
    uint64_t offset, uint8_t *buf, size_t *lenp)
{
	ipcc_imageblock_t ib;

	if (hashlen != sizeof (ib.ii_hash)) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
		    "invalid hash length specified, a %zu byte SHA-256 hash is "
		    "required", sizeof (ib.ii_hash));
		return (false);
	}

	ib.ii_buf = buf;
	ib.ii_buflen = *lenp;
	bcopy(hash, ib.ii_hash, sizeof (ib.ii_hash));
	ib.ii_offset = offset;

	if (ioctl(lih->lih_fd, IPCC_IMAGEBLOCK, &ib) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_IMAGEBLOCK) failed: %s", strerror(errno));
		return (false);
	}

	*lenp = ib.ii_datalen;

	return (libipcc_success(lih));
}

/*
 * In the future, once all direct IPCC consumers have been migrated to use
 * libipcc, the logic for splitting MAC addresses up into groups can be moved
 * out of the kernel and into here. For now, we retrieve the requested group
 * from the driver.
 */
static bool
libipcc_mac_fetch(libipcc_handle_t *lih, uint8_t group, ipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	mac = calloc(1, sizeof (*mac));
	if (mac == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for MAC address: %s",
		    strerror(errno)));
	}

	mac->im_group = group;
	if (ioctl(lih->lih_fd, IPCC_MACS, mac) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_MACS) failed: %s", strerror(errno));
		free(mac);
		return (false);
	}

	*macp = mac;

	return (true);
}

bool
libipcc_mac_all(libipcc_handle_t *lih, libipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	if (!libipcc_mac_fetch(lih, IPCC_MAC_GROUP_ALL, &mac))
		return (false);

	*macp = (libipcc_mac_t *)mac;

	return (libipcc_success(lih));
}

bool
libipcc_mac_nic(libipcc_handle_t *lih, libipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	if (!libipcc_mac_fetch(lih, IPCC_MAC_GROUP_NIC, &mac))
		return (false);

	*macp = (libipcc_mac_t *)mac;

	return (libipcc_success(lih));
}

bool
libipcc_mac_bootstrap(libipcc_handle_t *lih, libipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	if (!libipcc_mac_fetch(lih, IPCC_MAC_GROUP_BOOTSTRAP, &mac))
		return (false);

	*macp = (libipcc_mac_t *)mac;

	return (libipcc_success(lih));
}

void
libipcc_mac_free(libipcc_mac_t *mac)
{
	free(mac);
}

const struct ether_addr *
libipcc_mac_addr(libipcc_mac_t *macp)
{
	ipcc_mac_t *mac = (ipcc_mac_t *)macp;
	return ((struct ether_addr *)mac->im_base);
}

uint16_t
libipcc_mac_count(libipcc_mac_t *macp)
{
	ipcc_mac_t *mac = (ipcc_mac_t *)macp;
	return (mac->im_count);
}

uint8_t
libipcc_mac_stride(libipcc_mac_t *macp)
{
	ipcc_mac_t *mac = (ipcc_mac_t *)macp;
	return (mac->im_stride);
}

static bool
libipcc_keylookup_int(libipcc_handle_t *lih, uint8_t ipcc_key,
    uint8_t *buf, size_t *lenp)
{
	ipcc_keylookup_t kl;

	kl.ik_key = ipcc_key;
	kl.ik_buf = buf;
	kl.ik_buflen = *lenp;

	if (ioctl(lih->lih_fd, IPCC_KEYLOOKUP, &kl) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_KEYOOKUP) failed: %s", strerror(errno)));
	}

	switch (kl.ik_result) {
	case IPCC_KEYLOOKUP_BUFFER_TOO_SMALL:
		return (libipcc_error(lih, LIBIPCC_ERR_KEY_BUFTOOSMALL, 0,
		    "key value buffer (length 0x%x) was too small",
		    kl.ik_buflen));
	case IPCC_KEYLOOKUP_UNKNOWN_KEY:
		return (libipcc_error(lih, LIBIPCC_ERR_KEY_UNKNOWN, 0,
		    "key 0x%x was not known to the SP", ipcc_key));
	default:
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, 0,
		    "unknown keylookup result from SP: 0x%x", kl.ik_result));
	case IPCC_KEYLOOKUP_NO_VALUE:
		*lenp = 0;
		break;
	case IPCC_KEYLOOKUP_SUCCESS:
		*lenp = kl.ik_datalen;
		break;
	}

	return (libipcc_success(lih));
}

bool
libipcc_keylookup(libipcc_handle_t *lih, uint8_t key, uint8_t **bufp,
    size_t *lenp, libipcc_key_flag_t flags)
{
	uint8_t *buf = *bufp;
	size_t len = *lenp;

	if ((flags & ~LIBIPCC_KEYF_ALL) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
		    "invalid flags provided - 0x%x", flags));
	}

	if (buf != NULL) {
		if ((flags & LIBIPCC_KEYF_COMPRESSED) != 0) {
			return (libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
			    "decompression is not supported with a "
			    "caller-supplied buffer"));
		}
		/*
		 * In this case we can just use the caller-supplied buffer
		 * directly.
		 */
		VERIFY3U(len, >, 0);
		return (libipcc_keylookup_int(lih, key, buf, lenp));
	}

	VERIFY3U(len, ==, 0);

	len = IPCC_KEYLOOKUP_MAX_PAYLOAD;
	buf = calloc(1, len);
	if (buf == NULL) {
		(void) libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate 0x%zx bytes for key lookup buffer: %s",
		    len, strerror(errno));
		return (false);
	}

	if (!libipcc_keylookup_int(lih, key, buf, &len)) {
		free(buf);
		return (false);
	}

	if ((flags & LIBIPCC_KEYF_COMPRESSED) != 0) {
		size_t dstlen;
		uint8_t *dst;
		int zret;

		if (len <= 2) {
			free(buf);
			return (libipcc_error(lih, LIBIPCC_ERR_KEY_ZERR,
			    Z_STREAM_END,
			    "insufficient data to attempt decompression, "
			    "0x%zx bytes received", len));
		}

		/*
		 * For compressed data, the convention is that the first two
		 * bytes of the value are the original data length as a
		 * little-endian uint16_t.
		 */
		dstlen = *(uint16_t *)buf;

		dst = calloc(1, dstlen);
		if (dst == NULL) {
			int _errno = errno;

			free(buf);
			return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, _errno,
			    "failed to allocate 0x%zx bytes for decompression",
			    dstlen));
		}

		zret = uncompress(dst, (uLongf *)&dstlen,
		    buf + sizeof (uint16_t), len - sizeof (uint16_t));

		if (zret != Z_OK) {
			switch (zret) {
			case Z_MEM_ERROR:
				/*
				 * zlib appears to preserve errno here,
				 * although it is not documented in the manual.
				 */
				(void) libipcc_error(lih, LIBIPCC_ERR_NO_MEM,
				    errno, "could not allocate memory during "
				    "decompression");
				break;
			case Z_BUF_ERROR:
				/*
				 * This should not happen since we have sized
				 * the output buffer to be the original
				 * uncompressed data size.
				 */
				(void) libipcc_error(lih, LIBIPCC_ERR_KEY_ZERR,
				    zret, "output buffer was too small for "
				    "decompression");
				break;
			default:
				(void) libipcc_error(lih, LIBIPCC_ERR_KEY_ZERR,
				    zret, "decompression failure: %s",
				    zError(zret));
				break;
			}

			free(dst);
			free(buf);

			return (false);
		}

		/*
		 * Decompression succeeded, swap in the decompressed data
		 * buffer.
		 */
		free(buf);
		buf = dst;
		len = dstlen;
	}

	*lenp = len;
	*bufp = buf;

	return (libipcc_success(lih));
}

void
libipcc_keylookup_free(uint8_t *buf, size_t buflen __unused)
{
	free(buf);
}

bool
libipcc_inventory_metadata(libipcc_handle_t *lih, uint32_t *ver,
    uint32_t *nents)
{
	ipcc_inv_key_t key;
	size_t len = sizeof (key);

	*ver = 0;
	*nents = 0;

	if (!libipcc_keylookup_int(lih, IPCC_KEY_INVENTORY,
	    (uint8_t *)&key, &len)) {
		return (false);
	}

	*ver = key.iki_vers;
	*nents = key.iki_nents;

	return (libipcc_success(lih));
}

bool
libipcc_keyset(libipcc_handle_t *lih, uint8_t key, uint8_t *buf, size_t len,
    libipcc_key_flag_t flags)
{
	ipcc_keyset_t *kset;
	uint8_t result;

	if ((flags & ~LIBIPCC_KEYF_ALL) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
		    "invalid flags provided - 0x%x", flags));
	}

	kset = calloc(1, sizeof (*kset));
	if (kset == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for keyset structure"));
	}

	kset->iks_key = key;

	if (len > 0 && (flags & LIBIPCC_KEYF_COMPRESSED) != 0) {
		size_t maxlen, dstlen;
		int zret;

		/*
		 * If the data is being stored in a compressed form, we prefix
		 * it with the size of the original uncompressed data as a
		 * uint16_t, and limit the source size to that.
		 */
		if (len > UINT16_MAX) {
			free(kset);
			return (libipcc_error(lih, LIBIPCC_ERR_KEY_VALTOOLONG,
			    0, "value too long: 0x%zx bytes; "
			    "upper bound with compression: 0x%x",
			    len, UINT16_MAX));
		}
		maxlen = dstlen = sizeof (kset->iks_data) - sizeof (uint16_t);
		zret = compress2(kset->iks_data + sizeof (uint16_t),
		    (uLongf *)&dstlen, buf, len, Z_BEST_COMPRESSION);
		if (zret != Z_OK) {
			switch (zret) {
			case Z_MEM_ERROR:
				/*
				 * zlib appears to preserve errno here,
				 * although it is not documented in the manual.
				 */
				(void) libipcc_error(lih, LIBIPCC_ERR_NO_MEM,
				    errno, "could not allocate memory during "
				    "compression");
				break;
			case Z_BUF_ERROR:
				/*
				 * compressBound() provides an upper bound and
				 * it is likely that compression would produce
				 * slightly smaller data, but it's at least an
				 * indication of how far off the data is from
				 * fitting.
				 */
				(void) libipcc_error(lih,
				    LIBIPCC_ERR_KEY_VALTOOLONG, 0,
				    "input data was too large after "
				    "compression ~0x%zx; limit is 0x%zx",
				    (size_t)compressBound(len), maxlen);
				break;
			default:
				(void) libipcc_error(lih, LIBIPCC_ERR_KEY_ZERR,
				    zret, "compression failure: %s",
				    zError(zret));
				break;
			}

			free(kset);
			return (false);
		}

		*(uint16_t *)kset->iks_data = len;
		kset->iks_datalen = dstlen + sizeof (uint16_t);
	} else {
		if (len > sizeof (kset->iks_data)) {
			free(kset);
			return (libipcc_error(lih, LIBIPCC_ERR_KEY_VALTOOLONG,
			    0, "value too long: 0x%zx bytes; "
			    "upper bound without compression: 0x%zx",
			    len, sizeof (kset->iks_data)));
		}
		if (len > 0)
			bcopy(buf, kset->iks_data, len);
		kset->iks_datalen = len;
	}

	if (ioctl(lih->lih_fd, IPCC_KEYSET, kset) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_KEYSET) failed: %s", strerror(errno));
		free(kset);
		return (false);
	}

	result = kset->iks_result;
	free(kset);

	switch (result) {
	case IPCC_KEYSET_SUCCESS:
		break;
	case IPCC_KEYSET_UNKNOWN_KEY:
		return (libipcc_error(lih, LIBIPCC_ERR_KEY_UNKNOWN, 0,
		    "key 0x%x was not known to the SP", key));
	default:
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, 0,
		    "unknown keyset result from SP: 0x%x", result));
	case IPCC_KEYSET_READONLY:
		return (libipcc_error(lih, LIBIPCC_ERR_KEY_READONLY, 0,
		    "key 0x%x is read-only", key));
	case IPCC_KEYSET_TOO_LONG:
		return (libipcc_error(lih, LIBIPCC_ERR_KEY_VALTOOLONG, 0,
		    "value too long for key 0x%x", key));
	}

	return (libipcc_success(lih));
}

const char *
libipcc_inventory_status_str(libipcc_inventory_status_t status)
{
	switch (status) {
	case LIBIPCC_INVENTORY_STATUS_SUCCESS:
		return ("Success");
	case LIBIPCC_INVENTORY_STATUS_INVALID_INDEX:
		return ("Invalid index");
	case LIBIPCC_INVENTORY_STATUS_IO_DEV_MISSING:
		return ("I/O error -- device gone?");
	case LIBIPCC_INVENTORY_STATUS_IO_ERROR:
		return ("I/O error");
	default:
		break;
	}
	return ("Unknown");
}

bool
libipcc_inventory(libipcc_handle_t *lih, uint32_t idx,
    libipcc_inventory_t **invp)
{
	ipcc_inventory_t *inv;

	inv = calloc(1, sizeof (*inv));
	if (inv == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for inventory item: %s",
		    strerror(errno)));
	}

	inv->iinv_idx = idx;
	if (ioctl(lih->lih_fd, IPCC_INVENTORY, inv) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_INVENTORY) failed: %s", strerror(errno));
		free(inv);
		return (false);
	}

	*invp = (libipcc_inventory_t *)inv;

	return (libipcc_success(lih));
}

libipcc_inventory_status_t
libipcc_inventory_status(libipcc_inventory_t *invp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	switch (inv->iinv_res) {
	case IPCC_INVENTORY_SUCCESS:
		return (LIBIPCC_INVENTORY_STATUS_SUCCESS);
	case IPCC_INVENTORY_IO_DEV_MISSING:
		return (LIBIPCC_INVENTORY_STATUS_IO_DEV_MISSING);
	case IPCC_INVENTORY_IO_ERROR:
		return (LIBIPCC_INVENTORY_STATUS_IO_ERROR);
	case IPCC_INVENTORY_INVALID_INDEX:
	default:
		return (LIBIPCC_INVENTORY_STATUS_INVALID_INDEX);
	}
}

uint8_t
libipcc_inventory_type(libipcc_inventory_t *invp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	return (inv->iinv_type);
}

const uint8_t *
libipcc_inventory_name(libipcc_inventory_t *invp, size_t *lenp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	*lenp = IPCC_INVENTORY_NAMELEN;
	return (inv->iinv_name);
}

const uint8_t *
libipcc_inventory_data(libipcc_inventory_t *invp, size_t *lenp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	*lenp = inv->iinv_data_len;
	return (inv->iinv_data);
}

void
libipcc_inventory_free(libipcc_inventory_t *invp)
{
	free(invp);
}
