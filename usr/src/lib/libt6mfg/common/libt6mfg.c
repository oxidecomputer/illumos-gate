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
 * Copyright 2022 Oxide Computer Company
 */

/*
 * Routines to interact with and drive T6 manufacturing.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdevinfo.h>
#include <sys/sysmacros.h>
#include <locale.h>
#include <ctype.h>
#include <libispi.h>
#include <endian.h>
#include <stdarg.h>
#include <upanic.h>
#include <sys/debug.h>

#include <libt6mfg.h>

#define	T6_MFG_SROM_VPD_REGION	0x400
#define	T6_MFG_BUFSIZE	0x2000

/*
 * The T6 SROM is a 32 KiB EEPROM; however, the last 4 bytes are used to control
 * and manipulate the device itself. This means that we can check at most up to
 * 32 KiB - 4 bytes, e.g. the value below.
 */
#define	T6_SROM_LEN	0x7ffc

/*
 * This is the length of the T6 MAC region in bytes (each part of the MAC is an
 * ASCII character with no ':' to separate things).
 */
#define	T6_MAC_LEN	12

/*
 * Maximum size of an internal error message
 */
#define	T6_ERR_LEN	1024

/*
 * This is the size of the T6 SPI flash in bytes. In general, it may vary but
 * for the moment, we believe it'll generally be a 16 MiB device before we parse
 * an SFDP. The T6 expects 64 KiB sectors.
 */
#define	T6_SPI_LEN	(16 * 1024 * 1024)
#define	T6_SPI_SECTOR	(64 * 1024)

struct t6_mfg {
	uint8_t t6m_data_buf[T6_MFG_BUFSIZE];
	uint8_t t6m_base_buf[T6_MFG_BUFSIZE];
	t6_mfg_err_t t6m_err;
	int32_t t6m_syserr;
	char t6m_errmsg[T6_ERR_LEN];
	di_node_t t6m_devinfo;
	ispi_t *t6m_ispi;
	int32_t t6m_inst;
	int t6m_out_fd;
	int t6m_srom_fd;
	int t6m_flash_fd;
	int t6m_srom_base_fd;
	int t6m_flash_base_fds[2];
	int t6m_srom_file_fd;
	int t6m_flash_file_fd;
	locale_t t6m_c_loc;
	t6_mfg_region_flags_t t6m_srom_set;
	uint8_t t6m_id[T6_ID_LEN];
	uint8_t t6m_pn[T6_PART_LEN];
	uint8_t t6m_sn[T6_SERIAL_LEN];
	uint8_t t6m_mac[T6_MAC_LEN];
	t6_mfg_flash_info_t t6m_finfo;
	t6_mfg_progress_f t6m_pfunc;
	void *t6m_pfunc_arg;
};

/*
 * There are two primary T6 VPD regions represented by the following two
 * structures. Because these are relatively constant things and the layout does
 * not need to change, we don't really parse the VPD entirely, but basically
 * sanity check that the keywords are where we are (allowing the rest of the
 * general validation to occur to check them (as that should match the input
 * base image).
 */
#pragma pack(1)
typedef struct t6_vpd {
	uint8_t tv_vpd_init[3];
	uint8_t tv_prod[T6_ID_LEN];
	uint8_t tv_vpd_decl[3];
	uint8_t tv_pn_kw[3];
	uint8_t tv_pn[T6_PART_LEN];
	uint8_t tv_ec_kw[3];
	uint8_t tv_ec[0x10];
	uint8_t tv_sn_kw[3];
	uint8_t tv_sn[T6_SERIAL_LEN];
	uint8_t tv_rv_kw[3];
	uint8_t tv_rc_cksum;
} t6_vpd_t;

typedef struct t6_vpd_ext {
	uint8_t tv_vpd_init[3];
	uint8_t tv_prod[T6_ID_LEN];
	uint8_t tv_vpd_decl[3];
	uint8_t tv_pn_kw[3];
	uint8_t tv_pn[T6_PART_LEN];
	uint8_t tv_ec_kw[3];
	uint8_t tv_ec[0x10];
	uint8_t tv_sn_kw[3];
	uint8_t tv_sn[T6_SERIAL_LEN];
	uint8_t tv_mac_kw[3];
	uint8_t tv_mac[T6_MAC_LEN];
	uint8_t tv_opaque[0x2c6];
	uint8_t tv_rv_kw[3];
	uint8_t tv_rc_cksum;
} t6_vpd_ext_t;
#pragma pack()

/*
 * These arrays are the values we expect for the various keywords. Note the RV
 * keyword is not here, because the last byte is actually a checksum.
 */
const uint8_t t6_vpd_exp_vpd_init[3] = { 0x82, T6_ID_LEN, 0x00 };
const uint8_t t6_vpd_exp_pn_kw[3] = { 'P', 'N', T6_PART_LEN }; 
const uint8_t t6_vpd_exp_sn_kw[3] = { 'S', 'N', T6_SERIAL_LEN }; 
const uint8_t t6_vpd_exp_mac_kw[3] = { 'N', 'A', T6_MAC_LEN };

/*
 * This represents a given T6 VPD region. There are two VPD formats that we can
 * encounter. One has a basic format (t6_vpd_t) and the other has an extended
 * set of information (t6_vpd_ext_t). This determines what we expect to be
 * present, e.g. which information is valid and which structure is present.
 *
 * To simplify the library impelmentation, a region must be no more than 
 * T6_MFG_BUSZIZE bytes in size.
 */
typedef enum {
	T6_SROM_R_OPAQUE,
	T6_SROM_R_VPD,
	T6_SROM_R_VPD_EXT
} t6_srom_region_type_t;

typedef struct t6_srom_region {
	uint32_t reg_offset;
	uint32_t reg_len;
	t6_srom_region_type_t reg_type;
} t6_srom_region_t;

static const t6_srom_region_t t6_srom_regions[] = {
	{ 0x0000, T6_MFG_SROM_VPD_REGION, T6_SROM_R_OPAQUE },
	{ 0x0400, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x0800, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x0c00, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x1000, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x1400, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x1800, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x1c00, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x2000, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x2400, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x2800, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x2c00, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x3000, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x3400, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x3800, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x3c00, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD },
	{ 0x4000, T6_MFG_SROM_VPD_REGION, T6_SROM_R_VPD_EXT },
	{ 0x4400, T6_MFG_BUFSIZE, T6_SROM_R_OPAQUE },
	{ 0x6400, T6_SROM_LEN - 0x6400, T6_SROM_R_OPAQUE }
};

/*
 * Flash region information.
 *
 * There are several different portions of a SPI NOR flash that are dedicated to
 * different purposes in the device. We concern ourselves with a subset of
 * these here. We only concern ourselves enough to actually write the primary
 * firmware image. In addition, we have enough logic to grab out information
 * about the expansion ROM and the bootstrap version information for ourselves,
 * the rest of the flash is treated as opaque (though there is more on this in
 * the actual t4nex driver). All of the following offsets and region lengths are
 * in bytes.
 */
#define	T6_MFG_SEC_SIZE	(64 * 1024)
#define	T6_MFG_FLASH_EXP_START		(0 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_EXP_LEN		(6 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_EXP_CFG_START	(7 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_EXP_CFG_LEN	(1 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_FW_START		(8 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_FW_LEN		(16 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_BS_START		(27 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_BS_LEN		(1 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_FW_CFG_START	(31 * T6_MFG_SEC_SIZE)
#define	T6_MFG_FLASH_FW_CFG_LEN		(1 * T6_MFG_SEC_SIZE)

/*
 * This is the primary firmware header data. Note, this is all supposedly in big
 * endian data. This is a subset of what is present.
 */
typedef struct t6_mfg_fw_hdr {
	/*
	 * This appears to be a version of the header of the chip, followed by
	 * something to identify which chip it is for.
	 */
	uint8_t	tmfh_vers;
	uint8_t tmfh_chip;
	/*
	 * This length is in 512-byte chunks.
	 */
	uint16_t tmfh_len;
	uint32_t tmfh_fw_vers;
	uint32_t tmfh_uc_vers;
	uint8_t tmfh_ifaces[8];
	uint8_t tmfh_rsvd[8];
	uint32_t tmfh_magic;
	uint32_t tmfh_flags;
} t6_mfg_fw_hdr_t;

#define	T6_MFG_FLASH_MAGIC_FW	0x00000000
#define	T6_MFG_FLASH_MAGIC_BS	0x626f6f74

#define	T6_MFG_FLASH_CHIP_T4	0
#define	T6_MFG_FLASH_CHIP_T5	1
#define	T6_MFG_FLASH_CHIP_T6	2

typedef struct t6_mfg_rom_hdr {
	uint8_t tmrh_hdr[16];
	uint8_t tmrh_vers[4];
} t6_mfg_rom_hdr_t;

typedef struct t6_mfg_flash_region {
	uint64_t freg_start;
	uint32_t freg_len;
	t6_mfg_flash_base_t freg_base;
	boolean_t freg_bigend;
} t6_mfg_flash_region_t;

/*
 * This contains the different set of regions that we consider to exist on the
 * T6. While we don't support programming all of these (and there are in fact
 * theoretically a few more), we have to call out some of these because of the
 * endian issue. In particular, when dealing with things replated to the
 * BIOS/UEFI PXE Option/Expansion ROM the endianness is actually reversed from
 * what seems to otherwise exist. More specifically, some items are treated as
 * somewhat word/byte oriented. These translates into some of the endianness
 * issues as:
 *
 *   o 'word oriented' items are swapped on write to flash, but not on read
 *   o 'byte oriented' items are kept the same on write, but swapped on read
 *
 * This leads to a generally confusing set of things. Everything that is used
 * for the expansion ROM is treated as the 'word' oriented data (probably
 * because x86 PXE/UEFI is little endian), but everything else is 'byte'
 * oriented (probably because internal things are big endian).
 *
 * In a flash chip the minimum size is 8 MiB and there is up to 16 MiB of data.
 * we have called out all 16 MiB regions of the flash chip that may exist. XXX
 * This needs better handling.
 */
static const t6_mfg_flash_region_t t6_flash_regions[] = {
	{ T6_MFG_FLASH_EXP_START, T6_MFG_FLASH_EXP_LEN, T6_MFG_FLASH_BASE_ALL,
	    B_FALSE },
	{ 6 * T6_MFG_SEC_SIZE, T6_MFG_SEC_SIZE, T6_MFG_FLASH_BASE_ALL, B_TRUE },
	{ T6_MFG_FLASH_EXP_CFG_START, T6_MFG_FLASH_EXP_CFG_LEN,
	    T6_MFG_FLASH_BASE_ALL, B_FALSE },
	{ T6_MFG_FLASH_FW_START, T6_MFG_FLASH_FW_LEN, T6_MFG_FLASH_BASE_FW,
	    B_TRUE },
	{ 24 * T6_MFG_SEC_SIZE, 3 * T6_MFG_SEC_SIZE, T6_MFG_FLASH_BASE_ALL,
	    B_TRUE },
	{ T6_MFG_FLASH_BS_START, T6_MFG_FLASH_BS_LEN, T6_MFG_FLASH_BASE_ALL,
	    B_TRUE },
	{ 28 * T6_MFG_SEC_SIZE, 3 * T6_MFG_SEC_SIZE, T6_MFG_FLASH_BASE_ALL,
	    B_TRUE },
	{ T6_MFG_FLASH_FW_CFG_START, T6_MFG_FLASH_FW_CFG_LEN,
	    T6_MFG_FLASH_BASE_ALL, B_TRUE },
	{ 32 * T6_MFG_SEC_SIZE, 224 * T6_MFG_SEC_SIZE, T6_MFG_FLASH_BASE_ALL,
	    B_TRUE }
};

typedef boolean_t (*t6_mfg_flash_read_f)(t6_mfg_t *, size_t, size_t,
    const t6_mfg_flash_region_t *);
typedef boolean_t (*t6_mfg_flash_write_f)(t6_mfg_t *, size_t, size_t,
    const t6_mfg_flash_region_t *);

t6_mfg_err_t
t6_mfg_err(t6_mfg_t *t6mfg)
{
	return (t6mfg->t6m_err);
}

int32_t
t6_mfg_syserr(t6_mfg_t *t6mfg)
{
	return (t6mfg->t6m_syserr);
}

const char *
t6_mfg_errmsg(t6_mfg_t *t6mfg)
{
	return (t6mfg->t6m_errmsg);
}

const char *
t6_mfg_err2str(t6_mfg_t *t6mfg, t6_mfg_err_t err)
{
	switch (err) {
	case T6_MFG_ERR_OK:
		return ("T6_MFG_ERR_OK");
	case T6_MFG_ERR_BAD_FD:
		return ("T6_MFG_ERR_BAD_FD");
	case T6_MFG_ERR_UNKNOWN_FLASH_BASE:
		return ("T6_MFG_ERR_UNKNOWN_FLASH_BASE");
	case T6_MFG_ERR_BASE_NOT_SET:
		return ("T6_MFG_ERR_BASE_NOT_SET");
	case T6_MFG_ERR_UNKNOWN_SOURCE:
		return ("T6_MFG_ERR_UNKNOWN_SOURCE");
	case T6_MFG_ERR_SOURCE_NOT_SET:
		return ("T6_MFG_ERR_SOURCE_NOT_SET");
	case T6_MFG_ERR_FLASH_FILE_TOO_SMALL:
		return ("T6_MFG_ERR_FLASH_FILE_TOO_SMALL");
	case T6_MFG_ERR_SYSTEM_IO:
		return ("T6_MFG_ERR_SYSTEM_IO");
	case T6_MFG_ERR_USER_CB:
		return ("T6_MFG_ERR_USER_CB");
	default:
		return ("unknown error");
	}
}

static boolean_t t6_mfg_error(t6_mfg_t *, t6_mfg_err_t, int32_t, const char *,
    ...) __PRINTFLIKE(4);

static boolean_t
t6_mfg_error(t6_mfg_t *t6mfg, t6_mfg_err_t err, int32_t sys, const char *fmt,
    ...)
{
	va_list ap;

	t6mfg->t6m_err = err;
	t6mfg->t6m_syserr = sys;
	va_start(ap, fmt);
	(void) vsnprintf(t6mfg->t6m_errmsg, sizeof (t6mfg->t6m_errmsg), fmt,
	    ap);
	va_end(ap);
	return (B_FALSE);
}

static boolean_t
t6_mfg_success(t6_mfg_t *t6mfg)
{
	t6mfg->t6m_err = T6_MFG_ERR_OK;
	t6mfg->t6m_syserr = 0;
	t6mfg->t6m_errmsg[0] = '\0';
	return (B_TRUE);
}

static void
t6_mfg_progress(t6_mfg_t *t6mfg, t6_mfg_progress_event_t event, uint64_t off,
    uint64_t total)
{
	t6_mfg_progress_t cb;

	if (t6mfg->t6m_pfunc == NULL)
		return;

	(void) memset(&cb, 0, sizeof (cb));
	cb.tmp_type = event;
	cb.tmp_offset = off;
	cb.tmp_total = total;
	t6mfg->t6m_pfunc(&cb, t6mfg->t6m_pfunc_arg);
}

/*
 * Provide a means for discovering instances of devices in T6 manufacturing
 * mode. We don't want to actually pick up cxgbe instances if we can avoid it.
 */
void
t6_mfg_discover(t6_mfg_t *t6mfg, t6_mfg_disc_f func, void *arg)
{
	di_node_t dn;

	for (dn = di_drv_first_node("t6mfg", t6mfg->t6m_devinfo);
	    dn != DI_NODE_NIL; dn = di_drv_next_node(dn)) {
		char *dpath;
		t6_mfg_disc_info_t info;
		boolean_t ret;
		int *prop;

		dpath = di_devfs_path(dn);
		info.tmdi_di = dn;
		info.tmdi_path = dpath;
		info.tmdi_inst = di_instance(dn);
		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "device-id",
		    &prop) == 1) {
			info.tmdi_devid = (uint16_t)*prop;
		} else {
			info.tmdi_devid = UINT16_MAX;
		}

		if (di_prop_lookup_ints(DDI_DEV_T_ANY, dn, "vendor-id",
		    &prop) == 1) {
			info.tmdi_vendid = (uint16_t)*prop;
		} else {
			info.tmdi_vendid = UINT16_MAX;
		}

		ret = func(&info, arg);
		di_devfs_path_free(dpath);
		if (!ret) {
			return;
		}
	}
}

boolean_t
t6_mfg_set_output(t6_mfg_t *t6mfg, int fd)
{
	if (fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FD, 0, "invalid "
		    "output file descriptor: %d", fd));
	}

	t6mfg->t6m_out_fd = fd;
	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_set_dev(t6_mfg_t *t6mfg, int32_t inst)
{
	di_node_t dn;
	int srom_fd, flash_fd;

	for (dn = di_drv_first_node("t6mfg", t6mfg->t6m_devinfo);
	    dn != DI_NODE_NIL; dn = di_drv_next_node(dn)) {
		char buf[PATH_MAX], *dpath = NULL;

		if (di_instance(dn) != inst) {
			continue;
		}

		dpath = di_devfs_path(dn);
		if (dpath == NULL) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBDEVINFO, e,
			    "failed to obtain devfs path for instance %d: %s",
			    inst, strerror(e)));
		}

		if (snprintf(buf, sizeof (buf), "/devices%s:srom", dpath) >=
		    sizeof (buf)) {
			di_devfs_path_free(dpath);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_INTERNAL, 0,
			    "creating srom path would have overflown internal "
			    "buffer"));
		}

		srom_fd = open(buf, O_RDWR);
		if (srom_fd < 0) {
			int e = errno;
			di_devfs_path_free(dpath);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_OPEN_DEV, e,
			    "failed to open srom device %s: %s", buf,
			    strerror(e)));
		}

		if (snprintf(buf, sizeof (buf), "/devices%s:spidev", dpath) >=
		    sizeof (buf)) {
			(void) close(srom_fd);
			di_devfs_path_free(dpath);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_INTERNAL, 0,
			    "creating flash path would have overflown internal "
			    "buffer"));
		}

		flash_fd = open(buf, O_RDWR);
		if (flash_fd < 0) {
			int e = errno;
			(void) close(srom_fd);
			di_devfs_path_free(dpath);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_OPEN_DEV, e,
			    "failed to open srom device %s: %s", buf,
			    strerror(e)));
		}
		di_devfs_path_free(dpath);

		if (!ispi_set_dev(t6mfg->t6m_ispi, flash_fd)) {
			(void) close(srom_fd);
			(void) close(flash_fd);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI, 0,
			    "failed to set %s as libispi device: %s (0x%x/%d)",
			    buf, ispi_errmsg(t6mfg->t6m_ispi),
			    ispi_err(t6mfg->t6m_ispi),
			    ispi_syserr(t6mfg->t6m_ispi)));
		}

		if (t6mfg->t6m_srom_fd != -1) {
			(void) close(t6mfg->t6m_srom_fd);
		}

		if (t6mfg->t6m_flash_fd != -1) {
			(void) close(t6mfg->t6m_flash_fd);
		}

		t6mfg->t6m_srom_fd = srom_fd;
		t6mfg->t6m_flash_fd = flash_fd;
		return (t6_mfg_success(t6mfg));
	}

	return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_DEV, 0,
	    "failed to find t6mfg%d in devinfo snapshot", inst));
}

boolean_t
t6_mfg_srom_set_base(t6_mfg_t *t6mfg, int fd)
{
	if (fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FD, 0, "invalid "
		    "srom base file descriptor: %d", fd));
	}
	t6mfg->t6m_srom_base_fd = fd;
	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_srom_set_file(t6_mfg_t *t6mfg, int fd)
{
	if (fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FD, 0, "invalid "
		    "srom file file descriptor: %d", fd));
	}
	t6mfg->t6m_srom_file_fd = fd;
	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_flash_set_base(t6_mfg_t *t6mfg, t6_mfg_flash_base_t base, int fd)
{
	if (fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FD, 0, "invalid "
		    "flash base file descriptor: %d", fd));
	}

	if (base >= ARRAY_SIZE(t6mfg->t6m_flash_base_fds)) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_FLASH_BASE, 0,
		    "unknown flash base file type: 0x%x", base));
	}

	t6mfg->t6m_flash_base_fds[base] = fd;
	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_flash_set_file(t6_mfg_t *t6mfg, int fd)
{
	if (fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FD, 0, "invalid "
		    "flash file file descriptor: %d", fd));
	}
	t6mfg->t6m_flash_file_fd = fd;
	return (t6_mfg_success(t6mfg));
}

/*
 * The MAC address is actually encoded as a series of ASCII bytes to presumably
 * fit in with the PCI VPD. Here we check if this is valid and convert it to a
 * standard binary encoding for standard use by consumers. As this function is
 * only used internally, it does not set error information about what was
 * invalid about the MAC at this time.
 */
static boolean_t
t6_mfg_vpd_convert_to_mac(t6_mfg_t *t6mfg, const uint8_t vpd[T6_MAC_LEN],
    uint8_t mac[ETHERADDRL])
{
	(void) memset(mac, 0, sizeof (uint8_t) * ETHERADDRL);

	for (uint_t i = 0; i < T6_MAC_LEN; i++) {
		uint8_t val = 0;
		uint_t index, shift;

		if (isxdigit_l(vpd[i], t6mfg->t6m_c_loc) == 0) {
			return (B_FALSE);
		}

		if (vpd[i] >= '0' && vpd[i] <= '9') {
			val = vpd[i] - '0';
		} else if (vpd[i] >= 'A' && vpd[i] <= 'F') {
			val = vpd[i] - 'A' + 0xa;
		} else if (vpd[i] >= 'a' && vpd[i] <= 'f') {
			val = vpd[i] - 'a' + 0xa;
		} else {
			return (B_FALSE);
		}

		index = i / 2;
		if (i % 2 == 0) {
			shift = 4;
		} else {
			shift = 0;
		}

		mac[index] |= (val & 0xf) << shift;
	}

	return (B_TRUE);
}

static boolean_t
t6_mfg_vpd_convert_to_str(t6_mfg_t *t6mfg, const uint8_t *src, uint8_t *dest,
    size_t srclen)
{
	for (size_t i = 0; i < srclen; i++) {
		if (isgraph_l(src[i], t6mfg->t6m_c_loc) == 0 && src[i] != ' ') {
			return (B_FALSE);
		}
	}

	(void) memcpy(dest, src, srclen);
	dest[srclen] = '\0';

	/*
	 * We may have some trailing whitespace, so trim that off in case we do.
	 * Note, we will still compare this when validating internally, but for
	 * users this is probably more useful right now.
	 */
	srclen--;
	while (dest[srclen] == ' ') {
		dest[srclen] = '\0';
		if (srclen == 0)
			break;
		srclen--;
	}

	return (B_TRUE);
}

/*
 * Wrapper around pwrite(2) that makes sure we get the entire buffer out or
 * fail. A partial write is a failure.
 */
static boolean_t
t6_mfg_write(t6_mfg_t *t6mfg, int fd, const uint8_t *buf, size_t foff,
    size_t nbytes)
{
	size_t bufoff = 0;

	while (nbytes > 0) {
		ssize_t ret = pwrite(fd, buf + bufoff, nbytes, foff);
		if (ret == -1) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
			    "failed to write %zu bytes to fd %d at offset "
			    "%zu: %s", nbytes, fd, foff, strerror(e)));
		} 

		bufoff += (size_t)ret;
		foff += (size_t)ret;
		nbytes -= (size_t)ret;
	}

	return (B_TRUE);

}

/*
 * Similar to the above, but instead read-based. We want to read everything. If
 * we don't, there's a problem.
 */
static boolean_t
t6_mfg_read(t6_mfg_t *t6mfg, int fd, uint8_t *buf, size_t foff, size_t nbytes)
{
	size_t bufoff = 0;

	while (nbytes > 0) {
		ssize_t ret = pread(fd, buf + bufoff, nbytes, foff);
		if (ret == -1) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
			    "failed to read %zu bytes from fd %d at offset "
			    "%zu: %s", nbytes, fd, foff, strerror(e)));
		} else if (ret == 0 && nbytes != 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_FILE_TOO_SHORT,
			    0, "got EOF on fd %d, but still wanted to read %zu "
			    "bytes", fd, nbytes));
		}

		bufoff += (size_t)ret;
		foff += (size_t)ret;
		nbytes -= (size_t)ret;
	}

	return (B_TRUE);
}

/*
 * A PCI VPD region is supposed to checksum to zero. This function generates the
 * running checksum. This explicitly relies on unsigned overflow behavior.
 */
static uint8_t
t6_mfg_srom_vpd_cksum(const uint8_t *data, size_t len)
{
	uint8_t ret = 0;

	for (size_t i = 0; i < len; i++) {
		ret = (ret + data[i]) & 0xff;
	}

	return (ret);
}

static void
t6_mfg_srom_region_parse_vpd(t6_mfg_t *t6mfg, t6_mfg_region_data_t *data)
{
	t6_vpd_t vpd;

	data->treg_exp = T6_REGION_F_CKSUM_VALID | T6_REGION_F_ID_INFO |
	    T6_REGION_F_PN_INFO | T6_REGION_F_SN_INFO;

	(void) memcpy(&vpd, t6mfg->t6m_data_buf, sizeof (vpd));
	if (memcmp(t6_vpd_exp_vpd_init, vpd.tv_vpd_init,
	    sizeof (vpd.tv_vpd_init)) == 0 && t6_mfg_vpd_convert_to_str(t6mfg,
	    vpd.tv_prod, data->treg_id, sizeof (vpd.tv_prod))) {
		data->treg_flags |= T6_REGION_F_ID_INFO;
	}

	if (memcmp(t6_vpd_exp_pn_kw, vpd.tv_pn_kw, sizeof (vpd.tv_pn_kw)) ==
	    0 && t6_mfg_vpd_convert_to_str(t6mfg, vpd.tv_pn, data->treg_part,
	    sizeof (vpd.tv_pn))) {
		data->treg_flags |= T6_REGION_F_PN_INFO;
	}

	if (memcmp(t6_vpd_exp_sn_kw, vpd.tv_sn_kw, sizeof (vpd.tv_sn_kw)) ==
	    0 && t6_mfg_vpd_convert_to_str(t6mfg, vpd.tv_sn, data->treg_serial,
	    sizeof (vpd.tv_sn))) {
		data->treg_flags |= T6_REGION_F_SN_INFO;
	}

	if (vpd.tv_rv_kw[0] == 'R' && vpd.tv_rv_kw[1] == 'V' &&
	   t6_mfg_srom_vpd_cksum((void *)&vpd, sizeof (vpd)) == 0) {
		data->treg_flags |= T6_REGION_F_CKSUM_VALID;
	}
}

static void
t6_mfg_srom_region_parse_vpd_ext(t6_mfg_t *t6mfg, t6_mfg_region_data_t *data)
{
	t6_vpd_ext_t vpd;

	data->treg_exp = T6_REGION_F_CKSUM_VALID | T6_REGION_F_ID_INFO |
	    T6_REGION_F_PN_INFO | T6_REGION_F_SN_INFO | T6_REGION_F_MAC_INFO;

	(void) memcpy(&vpd, t6mfg->t6m_data_buf, sizeof (vpd));
	if (memcmp(t6_vpd_exp_vpd_init, vpd.tv_vpd_init,
	    sizeof (vpd.tv_vpd_init)) == 0 && t6_mfg_vpd_convert_to_str(t6mfg,
	    vpd.tv_prod, data->treg_id, sizeof (vpd.tv_prod))) {
		data->treg_flags |= T6_REGION_F_ID_INFO;
	}

	if (memcmp(t6_vpd_exp_pn_kw, vpd.tv_pn_kw, sizeof (vpd.tv_pn_kw)) ==
	    0 && t6_mfg_vpd_convert_to_str(t6mfg, vpd.tv_pn, data->treg_part,
	    sizeof (vpd.tv_pn))) {
		data->treg_flags |= T6_REGION_F_PN_INFO;
	}

	if (memcmp(t6_vpd_exp_sn_kw, vpd.tv_sn_kw, sizeof (vpd.tv_sn_kw)) ==
	    0 && t6_mfg_vpd_convert_to_str(t6mfg, vpd.tv_sn, data->treg_serial,
	    sizeof (vpd.tv_sn))) {
		data->treg_flags |= T6_REGION_F_SN_INFO;
	}

	if (memcmp(t6_vpd_exp_mac_kw, vpd.tv_mac_kw, sizeof (vpd.tv_mac_kw)) ==
	    0 && t6_mfg_vpd_convert_to_mac(t6mfg, vpd.tv_mac, data->treg_mac)) {
		data->treg_flags |= T6_REGION_F_MAC_INFO;
	}

	if (vpd.tv_rv_kw[0] == 'R' && vpd.tv_rv_kw[1] == 'V' &&
	   t6_mfg_srom_vpd_cksum((void *)&vpd, sizeof (vpd)) == 0) {
		data->treg_flags |= T6_REGION_F_CKSUM_VALID;
	}
}

static boolean_t
t6_mfg_srom_source_validate(t6_mfg_t *t6mfg, t6_mfg_source_t src, int *fdp)
{
	struct stat64 st;

	switch (src) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_srom_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}

		*fdp = t6mfg->t6m_srom_fd;
		break;
	case T6_MFG_SOURCE_FILE:
		if (t6mfg->t6m_srom_file_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 srom file has been set"));
		}
		if (fstat64(t6mfg->t6m_srom_file_fd, &st) != 0) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
			    "failed to fstat srom file fd %d: %s",
			    t6mfg->t6m_srom_fd, strerror(errno)));
		}

		if (st.st_size < T6_SROM_LEN) {
			return (t6_mfg_error(t6mfg,
			    T6_MFG_ERR_FLASH_FILE_TOO_SMALL, 0,
			    "T6 srom fd is too small: found %" PRId64 "bytes, "
			    "expected at least %u bytes", st.st_size,
			    T6_SROM_LEN));
		}

		*fdp = t6mfg->t6m_srom_file_fd;
		break;
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", src));
	}

	return (B_TRUE);
}

boolean_t
t6_mfg_srom_region_iter(t6_mfg_t *t6mfg, t6_mfg_source_t src,
    t6_mfg_srom_region_f func, void *arg)
{
	int fd;

	if (!t6_mfg_srom_source_validate(t6mfg, src, &fd)) {
		return (B_FALSE);
	}

	if (func == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "encountered invalid function pointer: %p", func));
	}

	/*
	 * Read each region that we have. Each is T6_MFG_SROM_VPD_REGION bytes
	 * long. These may be spread out, so seek to each one, if possible
	 * before beginning to read. After that, we must then parse it based on
	 * which region type this is.
	 */
	for (uint_t i = 0; i < ARRAY_SIZE(t6_srom_regions); i++) {
		const t6_srom_region_t *reg = &t6_srom_regions[i];
		t6_mfg_region_data_t data;

		if (reg->reg_type == T6_SROM_R_OPAQUE) {
			continue;
		}

		(void) memset(&data, 0, sizeof (data));

		if (!t6_mfg_read(t6mfg, fd, t6mfg->t6m_data_buf, reg->reg_offset,
		    reg->reg_len)) {
			return (B_FALSE);
		}

		data.treg_base = reg->reg_offset;
		t6_mfg_srom_region_parse_vpd(t6mfg, &data);
		if (reg->reg_type == T6_SROM_R_VPD_EXT) {
			t6_mfg_srom_region_parse_vpd_ext(t6mfg, &data);
		}

		if (!func(&data, arg)) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_USER_CB, 0,
			    "srom iteration terminated due to callback "
			    "failure for " "region [0x%x,0x%x)",
			    reg->reg_offset, reg->reg_offset + reg->reg_len));
		}
	}

	return (B_TRUE);
}

static boolean_t
t6_mfg_srom_str_convert_to_vpd(t6_mfg_t *t6mfg, const char *str, uint8_t *dest,
    size_t t6len)
{
	size_t len;
	size_t cur;

	if (str == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "passed an invalid string pointer: %p", str));
	}

	len = strlen(str);
	if (len > t6len) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_INVALID_VPD, 0, "input "
		    "string exceeded VPD size (%zu bytes)", t6len));
	}

	for (cur = 0; cur < len; cur++) {
		/*
		 * We have a pretty constrained set of characters that we're
		 * allowing as valid for our purposes right now. Basically
		 * alphanumeric a '-' characters.
		 */
		if (isalnum_l(str[cur], t6mfg->t6m_c_loc) == 0 &&
		    str[cur] != '-') {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_INVALID_VPD, 0,
			    "character at offset %zu (0x%x) is not a valid "
			    "ASCII alphanumeric character or a hyhen", cur,
			    str[cur]));
		}

		dest[cur] = str[cur];
	}

	/*
	 * All unused characters are filled with spaces to pad this out because
	 * we don't try to play games with '\0' here.
	 */
	for (; cur < t6len; cur++) {
		dest[cur] = ' ';
	}

	return (B_TRUE);
}

boolean_t
t6_mfg_srom_set_id(t6_mfg_t *t6mfg, const char *id)
{
	uint8_t buf[T6_ID_LEN];

	if (t6_mfg_srom_str_convert_to_vpd(t6mfg, id, buf, sizeof (buf))) {
		(void) memcpy(t6mfg->t6m_id, buf, sizeof (buf));
		t6mfg->t6m_srom_set |= T6_REGION_F_ID_INFO;
		return (t6_mfg_success(t6mfg));
	}

	return (B_FALSE);
}

boolean_t
t6_mfg_srom_set_pn(t6_mfg_t *t6mfg, const char *pn)
{
	uint8_t buf[T6_PART_LEN];

	if (t6_mfg_srom_str_convert_to_vpd(t6mfg, pn, buf, sizeof (buf))) {
		(void) memcpy(t6mfg->t6m_pn, buf, sizeof (buf));
		t6mfg->t6m_srom_set |= T6_REGION_F_PN_INFO;
		return (t6_mfg_success(t6mfg));
	}

	return (B_FALSE);
}

boolean_t
t6_mfg_srom_set_sn(t6_mfg_t *t6mfg, const char *sn)
{
	uint8_t buf[T6_SERIAL_LEN];

	if (t6_mfg_srom_str_convert_to_vpd(t6mfg, sn, buf, sizeof (buf))) {
		(void) memcpy(t6mfg->t6m_sn, buf, sizeof (buf));
		t6mfg->t6m_srom_set |= T6_REGION_F_SN_INFO;
		return (t6_mfg_success(t6mfg));
	}

	return (B_FALSE);
}

boolean_t
t6_mfg_srom_set_mac(t6_mfg_t *t6mfg, const uint8_t *mac)
{
	if (mac == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "passed an invalid MAC address pointer: %p", mac));
	}

	for (uint_t i = 0; i < ETHERADDRL; i++) {
		uint_t bidx = i * 2;
		uint8_t first, second;

		first = mac[i] >> 4;
		second = mac[i] & 0xf;

		if (first >= 0xa) {
			first = 'A' + first - 0xa;
		} else {
			first = '0' + first;
		}

		if (second >= 0xa) {
			second = 'A' + second - 0xa;
		} else {
			second = '0' + second;
		}

		t6mfg->t6m_mac[bidx] = first;
		t6mfg->t6m_mac[bidx + 1] = second;
	}

	t6mfg->t6m_srom_set |= T6_REGION_F_MAC_INFO;
	return (t6_mfg_success(t6mfg));
}

static void
t6_mfg_srom_fill_vpd(const t6_mfg_t *t6mfg, t6_vpd_t *vpd)
{
	(void) memcpy(vpd, t6mfg->t6m_base_buf, sizeof (*vpd));

	/*
	 * If we have not modified any fields, there's nothing to do here and we
	 * don't want to recalculate the VPD checksum. It is as right or as
	 * wrong as it is in the base file.
	 */
	if (t6mfg->t6m_srom_set == 0) {
		return;
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_ID_INFO) != 0) {
		(void) memcpy(vpd->tv_prod, t6mfg->t6m_id,
		    sizeof (t6mfg->t6m_id));
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_PN_INFO) != 0) {
		(void) memcpy(vpd->tv_pn, t6mfg->t6m_pn,
		    sizeof (t6mfg->t6m_pn));
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_SN_INFO) != 0) {
		(void) memcpy(vpd->tv_sn, t6mfg->t6m_sn,
		    sizeof (t6mfg->t6m_sn));
	}

	vpd->tv_rc_cksum = (0x100 - t6_mfg_srom_vpd_cksum((uint8_t *)vpd,
	    sizeof (*vpd) - 1)) & 0xff;
}

static void
t6_mfg_srom_fill_vpd_ext(const t6_mfg_t *t6mfg, t6_vpd_ext_t *vpd)
{
	(void) memcpy(vpd, t6mfg->t6m_base_buf, sizeof (*vpd));

	/*
	 * If we have not modified any fields, there's nothing to do here and we
	 * don't want to recalculate the VPD checksum. It is as right or as
	 * wrong as it is in the base file.
	 */
	if (t6mfg->t6m_srom_set == 0) {
		return;
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_ID_INFO) != 0) {
		(void) memcpy(vpd->tv_prod, t6mfg->t6m_id,
		    sizeof (t6mfg->t6m_id));
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_PN_INFO) != 0) {
		(void) memcpy(vpd->tv_pn, t6mfg->t6m_pn,
		    sizeof (t6mfg->t6m_pn));
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_SN_INFO) != 0) {
		(void) memcpy(vpd->tv_sn, t6mfg->t6m_sn,
		    sizeof (t6mfg->t6m_sn));
	}

	if ((t6mfg->t6m_srom_set & T6_REGION_F_MAC_INFO) != 0) {
		(void) memcpy(vpd->tv_mac, t6mfg->t6m_mac,
		    sizeof (t6mfg->t6m_mac));
	}

	vpd->tv_rc_cksum = (0x100 - t6_mfg_srom_vpd_cksum((uint8_t *)vpd,
	    sizeof (*vpd) - 1)) & 0xff;
}

static void
t6_mfg_srom_vpd_find_diff(const t6_vpd_t *src, const t6_vpd_t *base,
    t6_mfg_validate_data_t *data)
{
	if (memcmp(src->tv_prod, base->tv_prod, sizeof (base->tv_prod)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_ID;
	}

	if (memcmp(src->tv_pn, base->tv_pn, sizeof (base->tv_pn)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_PN;
	}

	if (memcmp(src->tv_sn, base->tv_sn, sizeof (base->tv_sn)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_SN;
	}

	if (src->tv_rc_cksum != base->tv_rc_cksum) {
		data->tval_flags |= T6_VALIDATE_F_ERR_VPD_CKSUM;
	}
}

static void
t6_mfg_srom_vpd_ext_find_diff(const t6_vpd_ext_t *src, const t6_vpd_ext_t *base,
    t6_mfg_validate_data_t *data)
{
	if (memcmp(src->tv_prod, base->tv_prod, sizeof (base->tv_prod)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_ID;
	}

	if (memcmp(src->tv_pn, base->tv_pn, sizeof (base->tv_pn)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_PN;
	}

	if (memcmp(src->tv_sn, base->tv_sn, sizeof (base->tv_sn)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_SN;
	}

	if (memcmp(src->tv_mac, base->tv_mac, sizeof (base->tv_mac)) != 0) {
		data->tval_flags |= T6_VALIDATE_F_ERR_MAC;
	}

	if (src->tv_rc_cksum != base->tv_rc_cksum) {
		data->tval_flags |= T6_VALIDATE_F_ERR_VPD_CKSUM;
	}
}

/*
 * Validating VPD regions is a little more involved. We basically need to cons
 * up the appropriate vpd section that covers our stuff to compare. Then we need
 * to compare all the remaining static data.
 *
 * We first do two memcmps, one for the opaque region and one for the vpd
 * region. If both pass, we're done. If not, we then go back and do some more
 * involved checking.
 */
static void
t6_mfg_srom_verify_vpd(const t6_mfg_t *t6mfg, const t6_srom_region_t *reg,
    t6_mfg_validate_data_t *data)
{
	t6_vpd_t vpd;
	t6_vpd_ext_t vpd_ext;
	size_t verify_off, verify_len;
	void *dataptr;

	if (reg->reg_type == T6_SROM_R_VPD_EXT) {
		verify_off = sizeof (vpd_ext);
		t6_mfg_srom_fill_vpd_ext(t6mfg, &vpd_ext);
		dataptr = &vpd_ext;
	} else {
		verify_off = sizeof (vpd);
		t6_mfg_srom_fill_vpd(t6mfg, &vpd);
		dataptr = &vpd;
	}
	verify_len = reg->reg_len - verify_off;

	if (memcmp(t6mfg->t6m_data_buf, dataptr, verify_off) != 0) {
		/*
		 * OK, this is the annoying part. We want to give consumers a
		 * hint as to what went wrong. So this means we need to actually
		 * go compare the individual VPD sections as to point out what
		 * went wrong. Note, we don't try to find where the first byte
		 * is in this right now. it is possible it is in an opaque
		 * section, but this should hopefully at least give us a
		 * reasonable starting point.
		 */
		data->tval_flags |= T6_VALIDATE_F_ERR_VPD_ERR;

		if (reg->reg_type == T6_SROM_R_VPD_EXT) {
			const t6_vpd_ext_t *other =
			    (const t6_vpd_ext_t *)t6mfg->t6m_data_buf;
			t6_mfg_srom_vpd_ext_find_diff(other, &vpd_ext, data);
		} else {
			const t6_vpd_t *other =
			    (const t6_vpd_t *)t6mfg->t6m_data_buf;
			t6_mfg_srom_vpd_find_diff(other, &vpd, data);
		}
	}

	if (memcmp(t6mfg->t6m_base_buf + verify_off,
	    t6mfg->t6m_data_buf + verify_off, verify_len) != 0) {
		uint32_t off;

		/*
		 * If we alrady hit an opaque error, don't bother updating the
		 * first byte found, etc.
		 */
		if ((data->tval_flags & T6_VALIDATE_F_ERR_OPAQUE) != 0) {
			return;
		}

		data->tval_flags |= T6_VALIDATE_F_ERR_OPAQUE;
		for (off = verify_off; off < reg->reg_len; off++) {
			if (t6mfg->t6m_data_buf[off] !=
			    t6mfg->t6m_base_buf[off]) {
				break;
			}

			data->tval_opaque_err = off;
		}
	}
}

boolean_t
t6_mfg_srom_validate(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_mfg_srom_validate_f func, void *arg)
{
	int fd;

	if (!t6_mfg_srom_source_validate(t6mfg, source, &fd)) {
		return (B_FALSE);
	}

	if (t6mfg->t6m_srom_base_fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BASE_NOT_SET, 0,
		    "the validate operation requires a valid srom base file "
		    "to be set"));
	}

	if (func == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "encountered invalid function pointer: %p", func));
	}

	for (uint_t i = 0; i < ARRAY_SIZE(t6_srom_regions); i++) {
		const t6_srom_region_t *reg = &t6_srom_regions[i];
		t6_mfg_validate_data_t data;

		(void) memset(&data, 0, sizeof (data));

		if (!t6_mfg_read(t6mfg, fd, t6mfg->t6m_data_buf, reg->reg_offset,
		    reg->reg_len)) {
			return (B_FALSE);
		}

		if (!t6_mfg_read(t6mfg, t6mfg->t6m_srom_base_fd, t6mfg->t6m_base_buf,
		    reg->reg_offset, reg->reg_len)) {
			return (B_FALSE);
		}

		data.tval_addr = reg->reg_offset;
		data.tval_range = reg->reg_len;
		/*
		 * If there is no VPD data, then this means that we're in an
		 * opaque section. Simply compare the memory to determine
		 * success or failure.
		 */
		if (reg->reg_type == T6_SROM_R_OPAQUE) {
			if (memcmp(t6mfg->t6m_data_buf, t6mfg->t6m_base_buf,
			    reg->reg_len) != 0) {
				uint32_t off;
				/*
				 * We've failed. Help future us out by finding
				 * the first byte that mismatches.
				 */
				for (off = 0; off < reg->reg_len;
				    off++) {
					if (t6mfg->t6m_data_buf[off] !=
					    t6mfg->t6m_base_buf[off]) {
						break;
					}
				}
				data.tval_opaque_err = off;
				data.tval_flags |= T6_VALIDATE_F_ERR_OPAQUE;
			}
		} else {
			t6_mfg_srom_verify_vpd(t6mfg, reg, &data);
		}

		if (!func(&data, arg)) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_USER_CB, 0,
			    "srom verification terminated due to callback "
			    "failure for " "region [0x%x,0x%x)",
			    reg->reg_offset, reg->reg_offset + reg->reg_len));
		}
	}

	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_srom_read(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_srom_read_flags_t flags)
{
	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_srom_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		break;
	case T6_MFG_SOURCE_FILE:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SUP, 0,
		    "reading from a file is not currently supported"));
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	if (flags != T6_SROM_READ_F_ALL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FLAGS, 0,
		    "encountered unsupported flags value: 0x%x", flags));
	}

	if (t6mfg->t6m_out_fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_OUTPUT_NOT_SET, 0,
		    "an output file must be specified for reading"));
	}

	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_START, 0, T6_SROM_LEN);

	for (uint_t i = 0; i < ARRAY_SIZE(t6_srom_regions); i++) {
		const t6_srom_region_t *reg = &t6_srom_regions[i];

		if (!t6_mfg_read(t6mfg, t6mfg->t6m_srom_fd, t6mfg->t6m_data_buf,
		    reg->reg_offset, reg->reg_len)) {
			t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR,
			    reg->reg_offset, T6_SROM_LEN);
			return (B_FALSE);
		}

		if (!t6_mfg_write(t6mfg, t6mfg->t6m_out_fd, t6mfg->t6m_data_buf,
		    reg->reg_offset, reg->reg_len)) {
			t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR,
			    reg->reg_offset, T6_SROM_LEN);
			return (B_FALSE);
		}

		t6_mfg_progress(t6mfg, T6_MFG_PROG_IO, reg->reg_offset,
		    T6_SROM_LEN);
	}

	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_END, T6_SROM_LEN, T6_SROM_LEN);

	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_srom_write(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_srom_write_flags_t flags)
{
	int outfd;

	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_srom_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		outfd = t6mfg->t6m_srom_fd;
		break;
	case T6_MFG_SOURCE_FILE:
		if (t6mfg->t6m_srom_file_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 srom file has been set"));
		}
		outfd = t6mfg->t6m_srom_file_fd;
		break;
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	if (flags != T6_SROM_WRITE_F_ALL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FLAGS, 0,
		    "encountered unsupported flags value: 0x%x", flags));
	}

	for (uint_t i = 0; i < ARRAY_SIZE(t6_srom_regions); i++) {
		const t6_srom_region_t *reg = &t6_srom_regions[i];

		if (!t6_mfg_read(t6mfg, t6mfg->t6m_srom_base_fd, t6mfg->t6m_base_buf,
		    reg->reg_offset, reg->reg_len)) {
			return (B_FALSE);
		}

		/*
		 * For VPD based sections, modify data based on what we have
		 * here and then write out the entire region. We always use an
		 * intermediate buffer just to simplify our lives and ensure we
		 * don't clobber ourselves before writing. For non-VPD we don't
		 * modify things and just let it go.
		 */
		if (reg->reg_type == T6_SROM_R_VPD_EXT) {
			t6_vpd_ext_t vpd_ext;

			t6_mfg_srom_fill_vpd_ext(t6mfg, &vpd_ext);
			(void) memcpy(t6mfg->t6m_base_buf, &vpd_ext,
			    sizeof (vpd_ext));
		} else if (reg->reg_type == T6_SROM_R_VPD) {
			t6_vpd_t vpd;

			t6_mfg_srom_fill_vpd(t6mfg, &vpd);
			(void) memcpy(t6mfg->t6m_base_buf, &vpd, sizeof (vpd));
		}

		if (!t6_mfg_write(t6mfg, outfd, t6mfg->t6m_base_buf,
		    reg->reg_offset, reg->reg_len)) {
			return (B_FALSE);
		}

	}

	return (t6_mfg_success(t6mfg));
}

/*
 * When we're performing reads from the T6, we have a bit of an endian problem.
 * Effectively, for some reason when we are performing the reads here, vs.
 * what's on disk, they somehow have been treated as uint32_t words and were
 * swapped. Somehow this only applies to bulk reads and not other commands such
 * as when we read the ID. As such, we need to correct for this here by swapping
 * it back as though it were big endian (the exact set of endianness
 * transformations is not entirely clear in part because the registers require
 * DDI translation to little endian, but this is how the T6 driver normally
 * works when not operating via spidev).
 */
static void
t6_mfg_endian_swap(void *data, size_t len)
{
	uint32_t *u32p = data;

	VERIFY0(len % 4);
	for (size_t i = 0; i < len / 4; i++) {
		u32p[i] = htobe32(u32p[i]);
	}
}

boolean_t
t6_mfg_flash_read(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_flash_read_flags_t flags)
{
	size_t buflen = sizeof (t6mfg->t6m_data_buf);

	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_flash_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		break;
	case T6_MFG_SOURCE_FILE:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SUP, 0,
		    "reading from a file is not currently supported"));
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	if (flags != T6_FLASH_READ_F_ALL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FLAGS, 0,
		    "encountered unsupported flags value: 0x%x", flags));
	}

	if (t6mfg->t6m_out_fd < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_OUTPUT_NOT_SET, 0,
		    "an output file must be specified for reading"));
	}

	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_START, 0, T6_SPI_LEN);

	/*
	 * Walk through things one region at a time. We need to be aware of the
	 * region that we're reading from so we can adjust for endianness of the
	 * region. Note the regions are meant to cover 100% of the flash chip
	 * that we know about. It does not currently cover the total possible
	 * size of the flash chip.
	 */
	for (uint_t i = 0; i < ARRAY_SIZE(t6_flash_regions); i++) {
		const t6_mfg_flash_region_t *region = &t6_flash_regions[i];
		uint64_t off = region->freg_start;
		uint32_t len = region->freg_len;

		while (len > 0) {
			uint32_t toread = MIN(len, buflen);

			if (!ispi_read(t6mfg->t6m_ispi, off, toread,
			    t6mfg->t6m_data_buf)) {
				t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR, off,
				    T6_SPI_LEN);
				return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI,
				    0, "failed to read from SPI device %d at "
				    "offset %" PRIu64 ": %s (0x%x/%d)",
				    t6mfg->t6m_inst, off,
				    ispi_errmsg(t6mfg->t6m_ispi),
				    ispi_err(t6mfg->t6m_ispi),
				    ispi_syserr(t6mfg->t6m_ispi)));
			}

			if (region->freg_bigend) {
				t6_mfg_endian_swap(t6mfg->t6m_data_buf, toread);
			}

			if (!t6_mfg_write(t6mfg, t6mfg->t6m_out_fd,
			    t6mfg->t6m_data_buf, off, toread)) {
				t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR, off,
				    T6_SPI_LEN);
				return (B_FALSE);
			}

			t6_mfg_progress(t6mfg, T6_MFG_PROG_IO, off, T6_SPI_LEN);

			len -= toread;
			off += toread;
		}
	}

	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_END, T6_SPI_LEN, T6_SPI_LEN);
	return (t6_mfg_success(t6mfg));
}

static boolean_t
t6_mfg_flash_file_read(t6_mfg_t *t6mfg, size_t foff, size_t nbytes,
    const t6_mfg_flash_region_t *reg)
{
	if (nbytes > sizeof (t6mfg->t6m_data_buf)) {
		const char *msg = "internal error: asked to read beyond buffer";
		upanic(msg, strlen(msg));
	}

	/*
	 * Note, while the SPI backend does byte swapping, our assumption is
	 * that everything we're reading from the file is already correct (word
	 * oriented regions are only swapped on read from the device).
	 */

	return (t6_mfg_read(t6mfg, t6mfg->t6m_flash_file_fd,
	    t6mfg->t6m_data_buf, foff, nbytes));
}

static boolean_t
t6_mfg_flash_file_write(t6_mfg_t *t6mfg, size_t foff, size_t nbytes,
    const t6_mfg_flash_region_t *reg)
{
	if (nbytes > sizeof (t6mfg->t6m_data_buf)) {
		const char *msg = "internal error: asked to write beyond buffer";
		upanic(msg, strlen(msg));
	}

	return (t6_mfg_write(t6mfg, t6mfg->t6m_flash_file_fd,
	    t6mfg->t6m_data_buf, foff, nbytes));
}

static boolean_t
t6_mfg_flash_spi_read(t6_mfg_t *t6mfg, size_t foff, size_t nbytes,
    const t6_mfg_flash_region_t *reg)
{
	if (nbytes > sizeof (t6mfg->t6m_data_buf)) {
		const char *msg = "internal error: asked to read beyond buffer";
		upanic(msg, strlen(msg));
	}

	if (!ispi_read(t6mfg->t6m_ispi, foff, nbytes, t6mfg->t6m_data_buf)) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI, 0,
		    "failed to read %zu bytes from SPI device %d at offset "
		    "%zu: %s (0x%x/%d)", nbytes, t6mfg->t6m_inst, foff,
		    ispi_errmsg(t6mfg->t6m_ispi), ispi_err(t6mfg->t6m_ispi),
		    ispi_syserr(t6mfg->t6m_ispi)));
	}

	/*
	 * Each region of the SPI flash has an associated endianness. When this
	 * is designated by us as 'big-endian' this corresponds to the Chelsio
	 * driver as 'byte-oriented' and 'little-endian' as 'word-oriented'.
	 * When reading from the SPI flash, callers are expected to handle the
	 * hardware translation that's going on around what is likely an endian
	 * exercise. So if we are here, then we need to potentially transform
	 * this.
	 */
	if (reg->freg_bigend) {
		t6_mfg_endian_swap(t6mfg->t6m_data_buf, nbytes);
	}

	return (B_TRUE);
}

static boolean_t
t6_mfg_flash_spi_write(t6_mfg_t *t6mfg, size_t foff, size_t nbytes,
    const t6_mfg_flash_region_t *reg)
{
	if (nbytes > sizeof (t6mfg->t6m_data_buf)) {
		const char *msg = "internal error: asked to read beyond buffer";
		upanic(msg, strlen(msg));
	}

	/*
	 * This is the corresponding change we're expected to make for a
	 * 'word-oriented' region as discussed in read. Here we byte swap before
	 * we get out there.
	 */
	if (reg->freg_bigend) {
		t6_mfg_endian_swap(t6mfg->t6m_data_buf, nbytes);
	}

	if (!ispi_write(t6mfg->t6m_ispi, foff, nbytes, t6mfg->t6m_data_buf)) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI, 0,
		    "failed to write %zu bytes from SPI device %d at offset "
		    "%zu: %s (0x%x/%d)", nbytes, t6mfg->t6m_inst, foff,
		    ispi_errmsg(t6mfg->t6m_ispi), ispi_err(t6mfg->t6m_ispi),
		    ispi_syserr(t6mfg->t6m_ispi)));
	}

	return (B_TRUE);
}

static void
t6_mfg_vers_decode(t6_mfg_flash_vers_t *vers, uint32_t raw,
    t6_mfg_source_t source)
{
	/*
	 * The data in the T6 files is intended to be in big-endian words. When
	 * reading from the file, this is maintained, but the kernel version is
	 * renormalized into the same format. As such, if we're not on a
	 * big-endian CPU (surprise, we're not), we need to swap this around.
	 */
	raw = be32toh(raw);

	vers->tmfv_major = (raw >> 24) & 0xff;
	vers->tmfv_minor = (raw >> 16) & 0xff;
	vers->tmfv_micro = (raw >> 8) & 0xff;
	vers->tmfv_build = raw & 0xff;
}

static boolean_t
t6_mfg_flash_read_args_setup(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_mfg_flash_read_f *readf)
{
	struct stat64 st;

	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_flash_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		*readf = t6_mfg_flash_spi_read;
		break;
	case T6_MFG_SOURCE_FILE:
		if (t6mfg->t6m_flash_file_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 flash file has been set"));
		}

		if (fstat64(t6mfg->t6m_flash_file_fd, &st) != 0) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
			    "failed to fstat flash file fd %d: %s",
			    t6mfg->t6m_flash_fd, strerror(errno)));
		}

		if (st.st_size < T6_SPI_LEN) {
			return (t6_mfg_error(t6mfg,
			    T6_MFG_ERR_FLASH_FILE_TOO_SMALL, 0,
			    "T6 flash fd is too small: found %" PRId64 "bytes, "
			    "expected at least %u bytes", st.st_size,
			    T6_SPI_LEN));
		}

		*readf = t6_mfg_flash_file_read;
		break;
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	return (B_TRUE);
}

/*
 * The goal here is to snapshot all of the information that we're looking for
 * about the flash and related.
 */
const t6_mfg_flash_info_t *
t6_mfg_flash_img_info(t6_mfg_t *t6mfg, t6_mfg_source_t source)
{
	t6_mfg_flash_read_f readf;

	if (!t6_mfg_flash_read_args_setup(t6mfg, source, &readf)) {
		return (NULL);
	}

	/*
	 * Always reset this data between runs to help deal with source
	 * information.
	 */
	(void) memset(&t6mfg->t6m_finfo, 0, sizeof (t6mfg->t6m_finfo));

	/*
	 * As each region of the flash is a little different, we unroll this
	 * rather than abstract it right now. Note, we're calling into readf
	 * directly here, so we need to account for endianness considerations
	 * here. The flash and bootstrap regions come from big-endian regions so
	 * we'll need to swap data around.
	 */
	if (readf(t6mfg, T6_MFG_FLASH_FW_START, sizeof (t6_mfg_fw_hdr_t),
	    &t6_flash_regions[3])) {
		t6_mfg_fw_hdr_t hdr;

		(void) memcpy(&hdr, t6mfg->t6m_data_buf,
		    sizeof (t6_mfg_fw_hdr_t));
		if (hdr.tmfh_fw_vers != UINT32_MAX &&
		    hdr.tmfh_uc_vers != UINT32_MAX &&
		    hdr.tmfh_magic == T6_MFG_FLASH_MAGIC_FW) {
			t6_mfg_vers_decode(&t6mfg->t6m_finfo.tmff_fw_vers,
			    hdr.tmfh_fw_vers, source);
			t6_mfg_vers_decode(&t6mfg->t6m_finfo.tmff_uc_vers,
			    hdr.tmfh_uc_vers, source);
			t6mfg->t6m_finfo.tmff_flags |=
			    T6_MFG_FLASH_F_FW_VERS_INFO;
		}
	}

	/*
	 * The bootstrap version often isn't here, so we check the magic and see
	 * if it makes sense.
	 */
	if (readf(t6mfg, T6_MFG_FLASH_BS_START, sizeof (t6_mfg_fw_hdr_t),
	    &t6_flash_regions[5])) {
		t6_mfg_fw_hdr_t hdr;

		(void) memcpy(&hdr, t6mfg->t6m_data_buf,
		    sizeof (t6_mfg_fw_hdr_t));
		if (hdr.tmfh_fw_vers != UINT32_MAX &&
		    hdr.tmfh_magic == T6_MFG_FLASH_MAGIC_BS) {
			t6_mfg_vers_decode(&t6mfg->t6m_finfo.tmff_bs_vers,
			    hdr.tmfh_fw_vers, source);
			t6mfg->t6m_finfo.tmff_flags |=
			    T6_MFG_FLASH_F_BS_VERS_INFO;
		}
	}

	/*
	 * The expansion ROM has a different layout. It uses a magic of 0x55 and
	 * 0xaa in the first two bytes. When we're reading data from a file,
	 * we're getting data that is meant to be in little-endian format 
	 */
	if (readf(t6mfg, T6_MFG_FLASH_EXP_START, sizeof (t6_mfg_rom_hdr_t),
	    &t6_flash_regions[0])) {
		t6_mfg_rom_hdr_t hdr;

		(void) memcpy(&hdr, t6mfg->t6m_data_buf,
		    sizeof (t6_mfg_rom_hdr_t));
		if (hdr.tmrh_hdr[0] == 0x55 && hdr.tmrh_hdr[1] == 0xaa) {
			t6_mfg_flash_vers_t *vers;

		       	vers = &t6mfg->t6m_finfo.tmff_exp_vers;
			vers->tmfv_major = hdr.tmrh_vers[0];
			vers->tmfv_minor = hdr.tmrh_vers[1];
			vers->tmfv_micro = hdr.tmrh_vers[2];
			vers->tmfv_build = hdr.tmrh_vers[3];
			t6mfg->t6m_finfo.tmff_flags |=
			    T6_MFG_FLASH_F_EXP_VERS_INFO;
		}
	}

	return (&t6mfg->t6m_finfo);
}

static boolean_t
t6_mfg_flash_validate_region(t6_mfg_t *t6mfg,
    const t6_mfg_flash_region_t *region, t6_mfg_flash_read_f readf,
    t6_mfg_flash_validate_f cbfunc, void *arg)
{
	t6_mfg_flash_vdata_t cbdata;
	uint32_t off = 0, len = region->freg_len;
	uint32_t valid_len;
	uint64_t base_start;
	int fd;

	(void) memset(&cbdata, 0, sizeof (cbdata));

	/*
	 * We need to figure out if we have anything to read for this region. If
	 * this region is for the firmware itself, then we first check if that
	 * fd is present. If not, we fall back to checking if someone gave us
	 * the entire base file. If that's here, we use that. Otherwise we treat
	 * this as an unspecified region and compare it with unwritten data.
	 *
	 * The next complication is that the actual size of our file that we're
	 * comparing with may be less than the actual region here. We assume
	 * that any bytes in the region beyond the file will actually be filled
	 * with 1s.
	 */
	if (region->freg_base == T6_MFG_FLASH_BASE_FW &&
	    t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW] != -1) {
		struct stat64 st;
		fd = t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW];
		base_start = 0;

		if (fstat64(fd, &st) != 0) {
			int e = errno;
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
			    "failed to fstat firmware region fd %d: %s", fd,
			    strerror(errno)));
		}

		valid_len = MIN(region->freg_len, st.st_size);
	} else if (t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_ALL] != -1) {

		fd = t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_ALL];
		base_start = region->freg_start;
		valid_len = region->freg_len;
	} else {
		fd = -1;
		valid_len = 0;
		base_start = 0;
		cbdata.tfv_flags |= T6_FLASH_VALIDATE_F_NO_SOURCE;
	}

	while (len > 0) {
		uint32_t toread = MIN(len, T6_MFG_BUFSIZE);
		uint32_t baseread, basefill;

		/*
		 * This logic here is meant to determine what portion of the
		 * base file, if any, we can use here (baseread) and which
		 * portion needs to be filled with 1s in the buffer (basefill).
		 */
		if (valid_len > 0) {
			if (valid_len > off) {
				baseread = MIN(toread, valid_len - off);
				basefill = toread - baseread;
			} else {
				baseread = 0;
				basefill = toread;
			}
		} else {
			baseread = 0;
			basefill = toread;
		}

		if (!readf(t6mfg, region->freg_start + off, toread, region)) {
			return (B_FALSE);
		}

		if (baseread > 0 && !t6_mfg_read(t6mfg, fd, t6mfg->t6m_base_buf,
		    base_start + off, baseread)) {
			return (B_FALSE);
		}

		/*
		 * Account for anything we need to read beyond the end of the
		 * file.
		 */
		if (basefill > 0) {
			(void) memset(t6mfg->t6m_base_buf + baseread, 0xff,
			    basefill);
		}

		if (memcmp(t6mfg->t6m_base_buf, t6mfg->t6m_data_buf, toread) !=
		    0) {
			cbdata.tfv_flags |= T6_FLASH_VALIDATE_F_ERR;

			for (uint32_t i = 0; i < toread; i++) {
				if (t6mfg->t6m_base_buf[i] !=
				    t6mfg->t6m_data_buf[i]) {
					cbdata.tfv_err = i;
					break;
				}
			}

			/*
			 * No point continuing to diff everything else given
			 * that we've found an error. So we break out of the
			 * broader loop as well.
			 */
			break;
		}

		off += toread;
		len -= toread;
	}

	cbdata.tfv_addr = region->freg_start;
	cbdata.tfv_range = region->freg_len;

	if (!cbfunc(&cbdata, arg)) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_USER_CB, 0,
		    "flash validation terminated due to callback failure for "
		    "region [0x%" PRIx64 ", 0x%" PRIx64 ")", region->freg_start,
		    region->freg_start + region->freg_len));
	}

	return (B_TRUE);
}

boolean_t
t6_mfg_flash_validate(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_mfg_flash_validate_f func, void *arg)
{
	t6_mfg_flash_read_f readf;

	if (t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_ALL] < 0 &&
	    t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW] < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BASE_NOT_SET, 0,
		    "the validate operation requires a valid flash base file "
		    "to be set"));
	}

	if (!t6_mfg_flash_read_args_setup(t6mfg, source, &readf)) {
		return (B_FALSE);
	}

	if (func == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "encountered invalid function pointer: %p", func));
	}

	for (uint_t i = 0; i < ARRAY_SIZE(t6_flash_regions); i++) {
		if (!t6_mfg_flash_validate_region(t6mfg, &t6_flash_regions[i],
		    readf, func, arg)) {
			return (B_FALSE);
		}
	}

	return (t6_mfg_success(t6mfg));
}

/*
 * Simulate the erasure of a SPI NOR that is being backed by a file by writing
 * all 1s.
 */
static boolean_t
t6_mfg_flash_erase_file(t6_mfg_t *t6mfg)
{
	size_t buflen = sizeof (t6mfg->t6m_data_buf);
	(void) memset(t6mfg->t6m_data_buf, 0xff, buflen);
	for (size_t off = 0; off < T6_SPI_LEN; off += buflen) {
		if (!t6_mfg_write(t6mfg, t6mfg->t6m_flash_file_fd,
		    t6mfg->t6m_data_buf, off, buflen)) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static boolean_t
t6_mfg_flash_write_region(t6_mfg_t *t6mfg, const t6_mfg_flash_region_t *region,
    t6_mfg_flash_write_f writef)
{
	int fd;
	struct stat64 st;
	uint32_t valid_len, file_off;

	/*
	 * Our first challenge here is what do we read for this region and if
	 * there's anything to write at all. Given that we currently only
	 * support writing the core firmware right now, unless we're in that
	 * region, then we're basically done and don't have anything to do right
	 * now.
	 */
	if (region->freg_base != T6_MFG_FLASH_BASE_FW) {
		return (B_TRUE);
	} else {
		fd = t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW];
	}

	if (fstat64(fd, &st) != 0) {
		int e = errno;
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_SYSTEM_IO, e,
		    "failed to fstat firmware region fd %d: %s", fd,
		    strerror(errno)));
	}

	valid_len = MIN(region->freg_len, st.st_size);
	file_off = 0;
	while (valid_len > 0) {
		uint32_t toread = MIN(valid_len, T6_MFG_BUFSIZE);
		size_t dev_off = region->freg_start + file_off;

		if (!t6_mfg_read(t6mfg, fd, t6mfg->t6m_data_buf, file_off,
		    toread)) {
			return (B_FALSE);
		}

		if (!writef(t6mfg, region->freg_start + file_off, toread,
		    region)) {
			return (B_FALSE);
		}

		t6_mfg_progress(t6mfg, T6_MFG_PROG_IO, dev_off, T6_SPI_LEN);
		valid_len -= toread;
		file_off += toread;
	}

	return (B_TRUE);
}

boolean_t
t6_mfg_flash_erase(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_flash_erase_flags_t flags)
{
	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_flash_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		break;
	case T6_MFG_SOURCE_FILE:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SUP, 0,
		    "erasing a file is not currently supported"));
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	if (flags != T6_FLASH_ERASE_F_ALL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FLAGS, 0,
		    "encountered unsupported flags value: 0x%x", flags));
	}

	t6_mfg_progress(t6mfg, T6_MFG_PROG_ERASE_BEGIN, 0, 0);
	if (!ispi_chip_erase(t6mfg->t6m_ispi)) {
		t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR, 0, 0);
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI, 0,
		    "failed to erase SPI device: %s (0x%x/%d)",
		    ispi_errmsg(t6mfg->t6m_ispi), ispi_err(t6mfg->t6m_ispi),
		    ispi_syserr(t6mfg->t6m_ispi)));

	}
	t6_mfg_progress(t6mfg, T6_MFG_PROG_ERASE_END, 0, 0);

	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_flash_write(t6_mfg_t *t6mfg, t6_mfg_source_t source,
    t6_flash_write_flags_t flags)
{
	t6_mfg_flash_write_f writef;

	if (t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW] < 0) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BASE_NOT_SET, 0,
		    "the write operation requires a valid flash firmware file "
		    "to be set"));
	}

	switch (source) {
	case T6_MFG_SOURCE_DEVICE:
		if (t6mfg->t6m_flash_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 device has been set"));
		}
		writef = t6_mfg_flash_spi_write;
		break;
	case T6_MFG_SOURCE_FILE:
		if (t6mfg->t6m_flash_file_fd < 0) {
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_SOURCE_NOT_SET,
			    0, "no T6 flash file has been set"));
		}
		writef = t6_mfg_flash_file_write;
		break;
	default:
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_UNKNOWN_SOURCE, 0,
		    "encountered unknown source type: 0x%x", source));
	}

	if (flags != T6_FLASH_WRITE_F_ALL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_FLAGS, 0,
		    "encountered unsupported flags value: 0x%x", flags));
	}

	/*
	 * Because we're writing to SPI NOR, we are going to ultimately going to
	 * have to erase the chip and we'll simulate this when we're going to a
	 * file by writing all 1s for the full range. When going to a file we
	 * always treat this as using the larger 16 MiB size right now.
	 */
	t6_mfg_progress(t6mfg, T6_MFG_PROG_ERASE_BEGIN, 0, 0);
	if (source == T6_MFG_SOURCE_DEVICE) {
		if (!ispi_chip_erase(t6mfg->t6m_ispi)) {
			t6_mfg_progress(t6mfg, T6_MFG_PROG_ERROR, 0, 0);
			return (t6_mfg_error(t6mfg, T6_MFG_ERR_LIBISPI, 0,
			    "failed to erase SPI device: %s (0x%x/%d)",
			    ispi_errmsg(t6mfg->t6m_ispi),
			    ispi_err(t6mfg->t6m_ispi),
			    ispi_syserr(t6mfg->t6m_ispi)));

		}
	} else if (!t6_mfg_flash_erase_file(t6mfg)) {
		return (B_FALSE);
	}
	t6_mfg_progress(t6mfg, T6_MFG_PROG_ERASE_END, 0, 0);

	/*
	 * SPI NOR is not as convenient as a simple file. If we're going to it,
	 * start off with a big, expensive, timely bang, that is a chip erase.
	 */
	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_START, 0, T6_SPI_LEN);
	for (uint_t i = 0; i < ARRAY_SIZE(t6_flash_regions); i++) {
		if (!t6_mfg_flash_write_region(t6mfg, &t6_flash_regions[i],
		    writef)) {
			return (B_FALSE);
		}
	}
	t6_mfg_progress(t6mfg, T6_MFG_PROG_IO_END, T6_SPI_LEN, T6_SPI_LEN);

	return (t6_mfg_success(t6mfg));
}

boolean_t
t6_mfg_set_progress_cb(t6_mfg_t *t6mfg, t6_mfg_progress_f func, void *arg)
{
	if (func == NULL) {
		return (t6_mfg_error(t6mfg, T6_MFG_ERR_BAD_PTR, 0,
		    "encountered invalid function pointer: %p", func));
	}

	t6mfg->t6m_pfunc = func;
	t6mfg->t6m_pfunc_arg = arg;
	return (t6_mfg_success(t6mfg));
}

void
t6_mfg_fini(t6_mfg_t *t6mfg)
{
	if (t6mfg == NULL) {
		return;
	}

	if (t6mfg->t6m_ispi != NULL) {
		ispi_fini(t6mfg->t6m_ispi);
	}

	if (t6mfg->t6m_srom_fd != -1) {
		(void) close(t6mfg->t6m_srom_fd);
		t6mfg->t6m_srom_fd = -1;
	}

	if (t6mfg->t6m_flash_fd != -1) {
		(void) close(t6mfg->t6m_flash_fd);
		t6mfg->t6m_flash_fd = -1;
	}

	if (t6mfg->t6m_devinfo != DI_NODE_NIL) {
		di_fini(t6mfg->t6m_devinfo);
		t6mfg->t6m_devinfo = NULL;
	}

	if (t6mfg->t6m_c_loc != NULL) {
		freelocale(t6mfg->t6m_c_loc);
		t6mfg->t6m_c_loc = NULL;
	}

	free(t6mfg);
}

t6_mfg_t *
t6_mfg_init(void)
{
	t6_mfg_t *t6mfg;

	t6mfg = calloc(1, sizeof (t6_mfg_t));
	if (t6mfg == NULL) {
		return (NULL);
	}

	t6mfg->t6m_srom_fd = -1;
	t6mfg->t6m_flash_fd = -1;
	t6mfg->t6m_inst = -1;
	t6mfg->t6m_srom_base_fd = -1;
	t6mfg->t6m_srom_file_fd = -1;
	t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_ALL] = -1;
	t6mfg->t6m_flash_base_fds[T6_MFG_FLASH_BASE_FW] = -1;
	t6mfg->t6m_flash_file_fd = -1;
	t6mfg->t6m_out_fd = -1;

	t6mfg->t6m_devinfo = di_init("/", DINFOCPYALL);
	if (t6mfg->t6m_devinfo == DI_NODE_NIL) {
		t6_mfg_fini(t6mfg);
		return (NULL);
	}

	t6mfg->t6m_c_loc = newlocale(LC_ALL_MASK, "C", NULL);
	if (t6mfg->t6m_c_loc == NULL) {
		t6_mfg_fini(t6mfg);
		return (NULL);
	}

	t6mfg->t6m_ispi = ispi_init();
	if (t6mfg->t6m_ispi == NULL) {
		t6_mfg_fini(t6mfg);
		return (NULL);
	}

	if (!ispi_set_size(t6mfg->t6m_ispi, T6_SPI_LEN)) {
		t6_mfg_fini(t6mfg);
		return (NULL);
	}

	return (t6mfg);
}
