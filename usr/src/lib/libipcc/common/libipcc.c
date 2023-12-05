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
 *
 * When interfacing with the ipcc(4D) driver's inventory capabilities, since
 * the service processor does not cache most of this information per se and it
 * is basically static across our lifetime (the SP cannot update without us
 * going down along for the ride), we provide a facility for consumers to
 * request that we use a cache for this information.
 *
 * Once we complete a successful read of all inventory elements without getting
 * any IPCC-level I/O errors, then we will proceed to cache this data. Any
 * cache that we create is likely to be wrong at some point. Right now we have
 * a forced expiry after a number of hours with some random component to reduce
 * the likelihood that everything does this at the same time.
 *
 * Currently the only thing that expires the cache other than bad data is time.
 * This probably needs to be improved to deal with changes in the SP state or
 * related. It mostly works due to the tied lifetime; however, if there was a
 * flaky connection to a device it means we'll be caching that something is
 * missing or that there was an I/O error at the inventory level for a larger
 * period of time which isn't great. Figuring out a better refresh pattern is an
 * area of future work.
 */

#include <errno.h>
#include <fcntl.h>
#include <libnvpair.h>
#include <librename.h>
#include <priv.h>
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <libipcc.h>
#include <libipcc_priv.h>
#include <libipcc_dt.h>

/*
 * Debug messages can be traced with DTrace using something like the following:
 *
 * dtrace -n 'libipcc$target:::msg
 *     {printf("%s:%s", copyinstr(arg0), copyinstr(arg1))}
 *     ' [-c <command>|-p <pid>]
 */

#define	LIBIPCC_DEBUG(fmt, ...) libipcc_debug(__func__, fmt, ##__VA_ARGS__)

static void __PRINTFLIKE(2)
libipcc_debug(const char *func, const char *fmt, ...)
{
	char message[LIBIPCC_ERR_LEN];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(message, sizeof (message), fmt, ap);
	va_end(ap);

	LIBIPCC_MSG((char *)func, message);
}

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
	case LIBIPCC_ERR_KEY_UNKNOWN:
		return ("LIBIPCC_ERR_KEY_UNKNOWN");
	case LIBIPCC_ERR_KEY_BUFTOOSMALL:
		return ("LIBIPCC_ERR_KEY_BUFTOOSMALL");
	case LIBIPCC_ERR_KEY_READONLY:
		return ("LIBIPCC_ERR_KEY_READONLY");
	case LIBIPCC_ERR_KEY_VALTOOLONG:
		return ("LIBIPCC_ERR_KEY_VALTOOLONG");
	case LIBIPCC_ERR_KEY_ZERR:
		return ("LIBIPCC_ERR_KEY_ZERR");
	case LIBIPCC_ERR_INSUFFMACS:
		return ("LIBIPCC_ERR_INSUFFMACS");
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

static int
libipcc_ioctl(libipcc_handle_t *lih, int cmd, void *arg)
{
	int ret;

	do {
		ret = ioctl(lih->lih_fd, cmd, arg);
		if (ret == 0)
			break;
	} while (errno == EINTR);

	return (ret);
}

bool
libipcc_status(libipcc_handle_t *lih, uint64_t *valp)
{
	ipcc_status_t status;

	if (libipcc_ioctl(lih, IPCC_STATUS, &status) != 0) {
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

	if (libipcc_ioctl(lih, IPCC_STATUS, &status) != 0) {
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

	if (libipcc_ioctl(lih, IPCC_IDENT, ident) != 0) {
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

	if (libipcc_ioctl(lih, IPCC_IMAGEBLOCK, &ib) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_IMAGEBLOCK) failed: %s", strerror(errno));
		return (false);
	}

	*lenp = ib.ii_datalen;

	return (libipcc_success(lih));
}

typedef struct {
	libipcc_mac_group_t ims_group;
	uint16_t ims_count;
} libipcc_mac_split_t;

/*
 * This table defines how the MAC addresses provided by the SP are broken up
 * into groups for host use. It may need extending in the future for different
 * Oxide platforms. Each group's addresses start straight after the previous
 * group's range.
 */
static libipcc_mac_split_t libipcc_mac_splits[] = {
	{ LIBIPCC_MAC_GROUP_NIC,	2 },
	{ LIBIPCC_MAC_GROUP_BOOTSTRAP,	1 },
};

static bool
libipcc_mac_filter(libipcc_handle_t *lih, libipcc_mac_group_t group,
    ipcc_mac_t *mac)
{
	uint16_t count = 0;
	libipcc_mac_split_t *s = NULL;

	if (group == LIBIPCC_MAC_GROUP_ALL)
		return (true);

	for (uint_t i = 0; i < ARRAY_SIZE(libipcc_mac_splits); i++) {
		if (group == libipcc_mac_splits[i].ims_group) {
			s = &libipcc_mac_splits[i];
			break;
		}
		count += libipcc_mac_splits[i].ims_count;
	}

	if (s == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, 0,
		    "unknown MAC address group 0x%x", group));
	}

	if (count >= mac->im_count || mac->im_count - count < s->ims_count) {
		return (libipcc_error(lih, LIBIPCC_ERR_INSUFFMACS, ENOSPC,
		    "Insufficent MAC addresses for group 0x%x", group));
	}

	/*
	 * We now know that there are sufficient remaining MAC addresses to
	 * satisfy this request. Set the count and calculate the base mac
	 * address for the group.
	 */
	mac->im_count = s->ims_count;

	for (uint_t j = mac->im_stride * count; j > 0; j--) {
		for (int i = ETHERADDRL - 1; i >= 0; i--) {
			/*
			 * If incrementing this byte wraps around to zero, we
			 * need to also increment the next byte along,
			 * otherwise we're done.
			 */
			if (++mac->im_base[i] != 0)
				break;
		}
	}

	return (true);
}

static bool
libipcc_mac_fetch(libipcc_handle_t *lih, libipcc_mac_group_t group,
    ipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	mac = calloc(1, sizeof (*mac));
	if (mac == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for MAC address: %s",
		    strerror(errno)));
	}

	if (libipcc_ioctl(lih, IPCC_MACS, mac) != 0) {
		(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
		    "ioctl(IPCC_MACS) failed: %s", strerror(errno));
		free(mac);
		return (false);
	}

	if (!libipcc_mac_filter(lih, group, mac)) {
		/* ipcc_mac_filter will have populated the error information */
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

	if (!libipcc_mac_fetch(lih, LIBIPCC_MAC_GROUP_ALL, &mac))
		return (false);

	*macp = (libipcc_mac_t *)mac;

	return (libipcc_success(lih));
}

bool
libipcc_mac_nic(libipcc_handle_t *lih, libipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	if (!libipcc_mac_fetch(lih, LIBIPCC_MAC_GROUP_NIC, &mac))
		return (false);

	*macp = (libipcc_mac_t *)mac;

	return (libipcc_success(lih));
}

bool
libipcc_mac_bootstrap(libipcc_handle_t *lih, libipcc_mac_t **macp)
{
	ipcc_mac_t *mac;

	if (!libipcc_mac_fetch(lih, LIBIPCC_MAC_GROUP_BOOTSTRAP, &mac))
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

	if (libipcc_ioctl(lih, IPCC_KEYLOOKUP, &kl) != 0) {
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
		    "invalid flag(s) provided - 0x%x",
		    flags & ~LIBIPCC_KEYF_ALL));
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

	if (libipcc_ioctl(lih, IPCC_KEYSET, kset) != 0) {
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

static void
libipcc_inv_nvl_write(const char *data, size_t len)
{
	librename_atomic_t *lra = NULL;
	int ret, fd;
	size_t off;

	/*
	 * Since LIBIPCC_INV_CACHEDIR is owned by root and on tmpfs, we know
	 * that the call to librename_atomic_init() will fail if we are not
	 * either the root user (directory owner), or another user who has a
	 * full privilege set (that is, effectively a privilege unaware root
	 * user). We set the mode of the file 0400 so that it can only be read
	 * by the original creator, or users who are privileged enough to have
	 * FILE_DAC_READ.
	 */
	if ((ret = librename_atomic_init(LIBIPCC_INV_CACHEDIR,
	    LIBIPCC_INV_CACHENAME, NULL, 0400, LIBRENAME_ATOMIC_NOUNLINK,
	    &lra)) != 0) {
		LIBIPCC_DEBUG("librename_atomic_init failed: %s",
		    strerror(errno));
		return;
	}

	fd = librename_atomic_fd(lra);
	off = 0;
	while (len > 0) {
		size_t towrite = MIN(LIBIPCC_INV_CHUNK, len);
		ssize_t sret = write(fd, data + off, towrite);

		if (sret == -1 && errno == EINTR)
			continue;
		if (sret == -1) {
			LIBIPCC_DEBUG(
			    "failed to write 0x%zx bytes to 0x%zx: %s",
			    towrite, off, strerror(errno));
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
		LIBIPCC_DEBUG("librename_atomic_commit failed: %s",
		    strerror(errno));
		(void) librename_atomic_abort(lra);
	} else {
		LIBIPCC_DEBUG("successfully stored inventory cache");
	}

done:
	if (lra != NULL)
		librename_atomic_fini(lra);
}

static void
libipcc_inv_persist(libipcc_inv_handle_t *liih)
{
	int ret;
	nvlist_t *nvl = NULL;
	char *pack_data = NULL;
	size_t pack_len = 0;

	if ((ret = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		LIBIPCC_DEBUG("Failed to allocate nvlist: %s",
		    strerror(errno));
		goto done;
	}

	if (nvlist_add_uint32(nvl, LIBIPCC_INV_NVL_NENTS,
	    liih->liih_ninv) != 0 ||
	    nvlist_add_uint32(nvl, LIBIPCC_INV_NVL_VERS, IPCC_INV_VERS) != 0 ||
	    nvlist_add_int64(nvl, LIBIPCC_INV_NVL_HRTIME, gethrtime()) != 0) {
		LIBIPCC_DEBUG(
		    "Failed to add items to nvlist: %s", strerror(errno));
		goto done;
	}

	for (uint32_t i = 0; i < liih->liih_ninv; i++) {
		char name[32];

		(void) snprintf(name, sizeof (name), "inventory-%u", i);
		ret = nvlist_add_byte_array(nvl, name,
		    (uchar_t *)&liih->liih_inv[i].lii_inv,
		    sizeof (liih->liih_inv[i].lii_inv));
		if (ret != 0) {
			LIBIPCC_DEBUG("Failed to add item %u to nvlist: %s",
			    i, strerror(errno));
			goto done;
		}
	}

	if ((ret = nvlist_pack(nvl, &pack_data, &pack_len, NV_ENCODE_NATIVE,
	    0)) != 0) {
		LIBIPCC_DEBUG("Failed to pack nvlist: %s", strerror(errno));
		goto done;
	}

	libipcc_inv_nvl_write(pack_data, pack_len);

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
libipcc_inv_restore(libipcc_inv_handle_t *liih)
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

	if (snprintf(buf, sizeof (buf), "%s/%s", LIBIPCC_INV_CACHEDIR,
	    LIBIPCC_INV_CACHENAME) >= sizeof (buf)) {
		LIBIPCC_DEBUG("failed to construct cache file path: "
		    "would have overflowed buffer");
		goto err;
	}

	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		LIBIPCC_DEBUG("failed to open inventory cache file: %s\n",
		    strerror(errno));
		goto err;
	}

	if (fstat(fd, &st) < 0) {
		LIBIPCC_DEBUG("failed to stat the inventory cache fd: %s\n",
		    strerror(errno));
		goto err;
	}

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		LIBIPCC_DEBUG("failed to map the inventory cache fd: %s\n",
		    strerror(errno));
		goto err;
	}

	ret = nvlist_unpack(addr, st.st_size, &nvl, 0);
	if (ret != 0) {
		LIBIPCC_DEBUG("failed to unpack the inventory cache: %s",
		    strerror(ret));
		goto err;
	}

	if ((ret = nvlist_lookup_pairs(nvl, 0,
	    LIBIPCC_INV_NVL_NENTS, DATA_TYPE_UINT32, &nents,
	    LIBIPCC_INV_NVL_VERS, DATA_TYPE_UINT32, &vers,
	    LIBIPCC_INV_NVL_HRTIME, DATA_TYPE_INT64, &ctime, NULL)) != 0) {
		LIBIPCC_DEBUG("failed to look up cache data: %s",
		    strerror(ret));
		goto err;
	}

	if (vers != IPCC_INV_VERS) {
		LIBIPCC_DEBUG("cached inventory from unsupported version: %u",
		    vers);
		goto err;
	}

	if (nents != liih->liih_ninv) {
		LIBIPCC_DEBUG("cached inventory has different "
		    "entry count (%u) than expected from SP (%u)",
		    nents, liih->liih_ninv);
		goto err;
	}

	now = gethrtime();
	exp = ctime + LIBIPCC_INV_TIME_BASE +
	    arc4random_uniform(LIBIPCC_INV_TIME_RAND_SEC) * NANOSEC;
	if (now > exp) {
		LIBIPCC_DEBUG(
		    "cached inventory has expired %" PRId64 " > %" PRId64,
		    (uint64_t)now, exp);
		goto err;
	} else {
		LIBIPCC_DEBUG(
		    "cached inventory is current %" PRId64 " <= %" PRId64,
		    (uint64_t)now, exp);
	}

	for (uint32_t i = 0; i < liih->liih_ninv; i++) {
		char name[32];
		uchar_t *data;
		uint_t data_len;

		(void) snprintf(name, sizeof (name), "inventory-%u", i);
		if ((ret = nvlist_lookup_byte_array(nvl, name, &data,
		    &data_len)) != 0) {
			LIBIPCC_DEBUG("cached data did not contain key %s: %s",
			    name, strerror(ret));
			goto err;
		}

		if (data_len != sizeof (liih->liih_inv[i].lii_inv)) {
			LIBIPCC_DEBUG("key %s has wrong length: found "
			    "0x%x, expected 0x%zx", name, data_len,
			    sizeof (liih->liih_inv[i].lii_inv));
			goto err;
		}

		(void) memcpy(&liih->liih_inv[i].lii_inv, data, data_len);
	}

	/*
	 * Now that we have successfully loaded all data from the cache, go
	 * ahead and mark everything valid.
	 */
	for (uint32_t i = 0; i < liih->liih_ninv; i++) {
		liih->liih_inv[i].lii_valid = true;
	}

	bret = true;

	LIBIPCC_DEBUG("successfully loaded inventory cache: %d item(s)",
	    liih->liih_ninv);

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

static bool
libipcc_inv_load(libipcc_handle_t *lih, libipcc_inv_handle_t *liih)
{
	uint32_t nioc_fail;
	int lasterrno = 0;

	liih->liih_inv = recallocarray(NULL, 0, liih->liih_ninv,
	    sizeof (libipcc_invcache_t));
	if (liih->liih_inv == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for inventory cache"));
	}

	if (libipcc_inv_restore(liih))
		return (true);

	nioc_fail = 0;
	for (uint32_t i = 0; i < liih->liih_ninv; i++) {
		libipcc_invcache_t *invc = &liih->liih_inv[i];

		invc->lii_inv.iinv_idx = i;
		if (libipcc_ioctl(lih, IPCC_INVENTORY, &invc->lii_inv) != 0) {
			invc->lii_errno = lasterrno = errno;
			nioc_fail++;
			continue;
		}

		invc->lii_valid = true;
	}

	if (nioc_fail == liih->liih_ninv) {
		free(liih->liih_inv);
		liih->liih_inv = NULL;
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL, lasterrno,
		    "failed to retrieve any inventory items"));
	}

	/*
	 * If we were able to successfully retrieve all items, then we will
	 * store this information in the cache file.
	 */
	if (nioc_fail == 0)
		libipcc_inv_persist(liih);

	return (true);
}

bool
libipcc_inv_hdl_init(libipcc_handle_t *lih, uint32_t *ver,
    uint32_t *nents, libipcc_inv_init_flag_t flags,
    libipcc_inv_handle_t **liihp)
{
	libipcc_inv_handle_t *liih;
	ipcc_inv_key_t key;
	size_t len = sizeof (key);

	if ((flags & ~LIBIPCC_INVF_ALL) != 0) {
		return (libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
		    "invalid flag(s) provided - 0x%x",
		    flags & ~LIBIPCC_INVF_ALL));
	}

	*ver = 0;
	*nents = 0;
	*liihp = NULL;

	if (!libipcc_keylookup_int(lih, IPCC_KEY_INVENTORY,
	    (uint8_t *)&key, &len)) {
		return (false);
	}

	liih = calloc(1, sizeof (*liih));
	if (liih == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for inventory handle: %s",
		    strerror(errno)));
	}

	liih->liih_vers = key.iki_vers;
	liih->liih_ninv = key.iki_nents;

	if ((flags & LIBIPCC_INV_INIT_CACHE) != 0) {
		/*
		 * Since the key lookup above succeeded, we know that the
		 * caller has privileges to access IPCC, so it's ok to go ahead
		 * and attempt to read the cached data.
		 */
		if (!libipcc_inv_load(lih, liih)) {
			free(liih);
			return (false);
		}
	}

	*ver = liih->liih_vers;
	*nents = liih->liih_ninv;
	*liihp = liih;

	return (libipcc_success(lih));
}

void
libipcc_inv_hdl_fini(libipcc_inv_handle_t *liih)
{
	free(liih->liih_inv);
	free(liih);
}

const char *
libipcc_inv_status_str(libipcc_inv_status_t status)
{
	switch (status) {
	case LIBIPCC_INV_STATUS_SUCCESS:
		return ("Success");
	case LIBIPCC_INV_STATUS_INVALID_INDEX:
		return ("Invalid index");
	case LIBIPCC_INV_STATUS_IO_DEV_MISSING:
		return ("I/O error -- device gone?");
	case LIBIPCC_INV_STATUS_IO_ERROR:
		return ("I/O error");
	default:
		break;
	}
	return ("Unknown");
}

bool
libipcc_inv(libipcc_handle_t *lih, libipcc_inv_handle_t *liih, uint32_t idx,
    libipcc_inv_t **invp)
{
	ipcc_inventory_t *inv;

	if (idx >= liih->liih_ninv) {
		return (libipcc_error(lih, LIBIPCC_ERR_INVALID_PARAM, 0,
		    "invalid index provided, valid range is [0,0x%x)",
		    liih->liih_ninv));
	}

	if (liih->liih_inv != NULL && !liih->liih_inv[idx].lii_valid) {
		return (libipcc_error(lih, LIBIPCC_ERR_INTERNAL,
		    liih->liih_inv[idx].lii_errno,
		    "failed to retrieve inventory item 0x%x", idx));
	}

	inv = calloc(1, sizeof (*inv));
	if (inv == NULL) {
		return (libipcc_error(lih, LIBIPCC_ERR_NO_MEM, errno,
		    "failed to allocate memory for inventory item: %s",
		    strerror(errno)));
	}

	if (liih->liih_inv != NULL) {
		bcopy(&liih->liih_inv[idx].lii_inv, inv, sizeof (*inv));
	} else {
		inv->iinv_idx = idx;
		if (libipcc_ioctl(lih, IPCC_INVENTORY, inv) != 0) {
			(void) libipcc_error(lih, LIBIPCC_ERR_INTERNAL, errno,
			    "failed to retrieve inventory item 0x%x: %s",
			    idx, strerror(errno));
			free(inv);
			return (false);
		}
	}

	*invp = (libipcc_inv_t *)inv;

	return (libipcc_success(lih));
}

libipcc_inv_status_t
libipcc_inv_status(libipcc_inv_t *invp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	switch (inv->iinv_res) {
	case IPCC_INVENTORY_SUCCESS:
		return (LIBIPCC_INV_STATUS_SUCCESS);
	case IPCC_INVENTORY_IO_DEV_MISSING:
		return (LIBIPCC_INV_STATUS_IO_DEV_MISSING);
	case IPCC_INVENTORY_IO_ERROR:
		return (LIBIPCC_INV_STATUS_IO_ERROR);
	case IPCC_INVENTORY_INVALID_INDEX:
	default:
		return (LIBIPCC_INV_STATUS_INVALID_INDEX);
	}
}

uint8_t
libipcc_inv_type(libipcc_inv_t *invp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	return (inv->iinv_type);
}

const uint8_t *
libipcc_inv_name(libipcc_inv_t *invp, size_t *lenp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	*lenp = IPCC_INVENTORY_NAMELEN;
	return (inv->iinv_name);
}

const uint8_t *
libipcc_inv_data(libipcc_inv_t *invp, size_t *lenp)
{
	ipcc_inventory_t *inv = (ipcc_inventory_t *)invp;

	*lenp = inv->iinv_data_len;
	return (inv->iinv_data);
}

void
libipcc_inv_free(libipcc_inv_t *invp)
{
	free(invp);
}
