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

#ifndef _LIBT6MFG_H
#define	_LIBT6MFG_H

/*
 * Library routines for interacting with the T6.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <libdevinfo.h>
#include <sys/ethernet.h>

typedef enum {
	T6_MFG_ERR_OK	= 0,
	/*
	 * Indicates that a file descriptor argument was invalid. Generaly, this
	 * means a value less than 0.
	 */
	T6_MFG_ERR_BAD_FD,
	/*
	 * Indicates an invalid t6_mfg_flash_base_t argument was used.
	 */
	T6_MFG_ERR_UNKNOWN_FLASH_BASE,
	/*
	 * Indicates that the base has not been set for this operation.
	 */
	T6_MFG_ERR_BASE_NOT_SET,
	/*
	 * Indicates that an unknown t6_mfg_source_t argument was used.
	 */
	T6_MFG_ERR_UNKNOWN_SOURCE,
	/*
	 * Indicates that the specified source was requested; however, there is
	 * no current file descriptor assosciated with it.
	 */
	T6_MFG_ERR_SOURCE_NOT_SET,
	/*
	 * Indicates that while the requested source is valid, it is not
	 * currently supported.
	 */
	T6_MFG_ERR_SOURCE_NOT_SUP,
	/*
	 * Indicates that the output file descriptor has not been set.
	 */
	T6_MFG_ERR_OUTPUT_NOT_SET,
	/*
	 * Indicates that a specified file is too small to be used as the
	 * primary SPI flash file.
	 */
	T6_MFG_ERR_FLASH_FILE_TOO_SMALL,
	/*
	 * Indicates that a system I/O error occurred trying to perform I/O
	 * information. The system error will contain an errno.
	 */
	T6_MFG_ERR_SYSTEM_IO,
	/*
	 * Indicates that the function was terminated due to a user request from
	 * a callback.
	 */
	T6_MFG_ERR_USER_CB,
	/*
	 * Indicates that we expected to read more from a file, but ultimately
	 * came up short.
	 */
	T6_MFG_ERR_FILE_TOO_SHORT,
	/*
	 * Indicates a condition that is likely an internal library error.
	 */
	T6_MFG_ERR_INTERNAL,
	/*
	 * Indicates that an issue occurred with the use of the devinfo library.
	 * The system error may be meaningful in this case for additoinal
	 * information.
	 */
	T6_MFG_ERR_LIBDEVINFO,
	/*
	 * Indicates an error trying to open a device file.
	 */
	T6_MFG_ERR_OPEN_DEV,
	/*
	 * Indicates an error occurred trying to use the underlying ispi
	 * library.
	 */
	T6_MFG_ERR_LIBISPI,
	/*
	 * Indicates that no such device instance exists.
	 */
	T6_MFG_ERR_UNKNOWN_DEV,
	/*
	 * Indicates that the VPD data was invalid in some form. For example, it
	 * was too long, used an invalid character, etc.
	 */
	T6_MFG_ERR_INVALID_VPD,
	/*
	 * Indicates that the requested flags are not supported.
	 */
	T6_MFG_ERR_BAD_FLAGS,
	/*
	 * Generally indicates we got an invalid pointer argument, e.g. it was
	 * NULL.
	 */
	T6_MFG_ERR_BAD_PTR
} t6_mfg_err_t;

typedef struct t6_mfg t6_mfg_t;

/*
 * Handle initialization and cleanup routines.
 */
extern t6_mfg_t *t6_mfg_init(void);
extern void t6_mfg_fini(t6_mfg_t *);

/*
 * Error Information
 */
extern t6_mfg_err_t t6_mfg_err(t6_mfg_t *);
extern int32_t t6_mfg_syserr(t6_mfg_t *);
extern const char *t6_mfg_errmsg(t6_mfg_t *);
extern const char *t6_mfg_err2str(t6_mfg_t *, t6_mfg_err_t);

/*
 * These structs and functions allow one to discover the set of T6 devices in
 * manufacturing mode on the system.
 */
typedef struct t6_mfg_disc_info {
	di_node_t tmdi_di;
	const char *tmdi_path;
	uint16_t tmdi_vendid;
	uint16_t tmdi_devid;
	int32_t tmdi_inst;
} t6_mfg_disc_info_t;

typedef boolean_t (*t6_mfg_disc_f)(t6_mfg_disc_info_t *, void *);
void t6_mfg_discover(t6_mfg_t *, t6_mfg_disc_f, void *);

/*
 * General ways of setting what devices we're operating on, input, and output
 * files. In general, a 'base' is something that seeds information for
 * verification or writing. An output is the destination of a read.
 */
extern boolean_t t6_mfg_set_dev(t6_mfg_t *, int32_t);
extern boolean_t t6_mfg_set_output(t6_mfg_t *, int);
extern boolean_t t6_mfg_srom_set_base(t6_mfg_t *, int);
extern boolean_t t6_mfg_srom_set_file(t6_mfg_t *, int);
extern boolean_t t6_mfg_flash_set_file(t6_mfg_t *, int);

/*
 * As the T6 flash contains multiple different regions, we allow one to set a
 * base file for a particular region or the entire device. Regions which do not
 * have a base file will be assumed to be written with or we should find an all
 * 1s value. Bases which do not cover the entire region will be filled with all
 * 0s (e.g. requiring that a NOR erase be performed). Only one base can be set
 * at a time right now.
 */
typedef enum {
	T6_MFG_FLASH_BASE_ALL,
	T6_MFG_FLASH_BASE_FW
} t6_mfg_flash_base_t;

extern boolean_t t6_mfg_flash_set_base(t6_mfg_t *, t6_mfg_flash_base_t, int);

/*
 * The source indicates what we are operating against. So for a read, this is
 * what we read from, for a write, this is where we're going. For a verify, work
 * against this.
 */
typedef enum t6_mfg_source {
	T6_MFG_SOURCE_DEVICE,
	T6_MFG_SOURCE_FILE
} t6_mfg_source_t;

/*
 * SROM specific definitions. In particular, the SROM contains a number of
 * different VPD areas. These are broken into repeating regions with different
 * expectations for what is valid or invalid. In general, we allow one to set
 * the base device product name/id, an explicit part number, serial number, and
 * MAC address as we assume that these will vary.
 */
#define	T6_PART_LEN	16
#define	T6_SERIAL_LEN	24
#define	T6_ID_LEN	16

typedef enum {
	T6_REGION_F_CKSUM_VALID	= 1 << 0,
	T6_REGION_F_ID_INFO	= 1 << 1,
	T6_REGION_F_PN_INFO	= 1 << 2,
	T6_REGION_F_SN_INFO	= 1 << 3,
	T6_REGION_F_MAC_INFO	= 1 << 4
} t6_mfg_region_flags_t;

typedef struct t6_mfg_region_data {
	uint32_t treg_base;
	t6_mfg_region_flags_t treg_flags;
	t6_mfg_region_flags_t treg_exp;
	uint8_t treg_id[T6_ID_LEN + 1];
	uint8_t treg_part[T6_PART_LEN + 1];
	uint8_t treg_serial[T6_SERIAL_LEN + 1];
	uint8_t treg_mac[ETHERADDRL];
} t6_mfg_region_data_t;

typedef boolean_t (*t6_mfg_srom_region_f)(const t6_mfg_region_data_t *,
    void *);
extern boolean_t t6_mfg_srom_region_iter(t6_mfg_t *, t6_mfg_source_t,
    t6_mfg_srom_region_f, void *);

extern boolean_t t6_mfg_srom_set_id(t6_mfg_t *, const char *);
extern boolean_t t6_mfg_srom_set_pn(t6_mfg_t *, const char *);
extern boolean_t t6_mfg_srom_set_sn(t6_mfg_t *, const char *);
extern boolean_t t6_mfg_srom_set_mac(t6_mfg_t *, const uint8_t *);

typedef enum {
	T6_VALIDATE_F_OK		= 0,
	T6_VALIDATE_F_ERR_OPAQUE	= 1 << 0,
	T6_VALIDATE_F_ERR_VPD_ERR	= 1 << 1,
	T6_VALIDATE_F_ERR_VPD_CKSUM	= 1 << 2,
	T6_VALIDATE_F_ERR_ID		= 1 << 3,
	T6_VALIDATE_F_ERR_PN		= 1 << 4,
	T6_VALIDATE_F_ERR_SN		= 1 << 5,
	T6_VALIDATE_F_ERR_MAC		= 1 << 6
} t6_mfg_validate_flags_t;

typedef struct t6_mfg_validate_data {
	uint32_t		tval_addr;
	uint32_t		tval_range;
	t6_mfg_validate_flags_t	tval_flags;
	uint32_t		tval_opaque_err;
} t6_mfg_validate_data_t;

typedef boolean_t (*t6_mfg_srom_validate_f)(const t6_mfg_validate_data_t *,
    void *);
extern boolean_t t6_mfg_srom_validate(t6_mfg_t *, t6_mfg_source_t,
    t6_mfg_srom_validate_f, void *);

/*
 * Flags to control how we read/write data. This is generally here to provide
 * flexibility for future revisions of the library. At the moment, the
 * assumption is all or nothing.
 */
typedef enum {
	T6_SROM_READ_F_ALL	= 0
} t6_srom_read_flags_t;

typedef enum {
	T6_SROM_WRITE_F_ALL	= 0
} t6_srom_write_flags_t;

typedef enum {
	T6_FLASH_READ_F_ALL	= 0
} t6_flash_read_flags_t;

typedef enum {
	T6_FLASH_WRITE_F_ALL	= 0
} t6_flash_write_flags_t;

typedef enum {
	T6_FLASH_ERASE_F_ALL	= 0
} t6_flash_erase_flags_t;

extern boolean_t t6_mfg_srom_read(t6_mfg_t *, t6_mfg_source_t,
    t6_srom_read_flags_t);
extern boolean_t t6_mfg_srom_write(t6_mfg_t *, t6_mfg_source_t,
    t6_srom_write_flags_t);
extern boolean_t t6_mfg_flash_read(t6_mfg_t *, t6_mfg_source_t,
    t6_flash_read_flags_t);
extern boolean_t t6_mfg_flash_write(t6_mfg_t *, t6_mfg_source_t,
    t6_flash_write_flags_t);
extern boolean_t t6_mfg_flash_erase(t6_mfg_t *, t6_mfg_source_t,
    t6_flash_erase_flags_t);

typedef enum {
	T6_MFG_FLASH_F_FW_VERS_INFO	= 1 << 0,
	T6_MFG_FLASH_F_BS_VERS_INFO	= 1 << 2,
	T6_MFG_FLASH_F_EXP_VERS_INFO	= 1 << 3
} t6_mfg_flash_flags_t;

typedef struct {
	uint8_t	tmfv_major;
	uint8_t	tmfv_minor;
	uint8_t	tmfv_micro;
	uint8_t	tmfv_build;
} t6_mfg_flash_vers_t;

typedef struct {
	t6_mfg_flash_flags_t	tmff_flags;
	t6_mfg_flash_vers_t	tmff_fw_vers;
	t6_mfg_flash_vers_t	tmff_uc_vers;
	t6_mfg_flash_vers_t	tmff_bs_vers;
	t6_mfg_flash_vers_t	tmff_exp_vers;
} t6_mfg_flash_info_t;

extern const t6_mfg_flash_info_t *t6_mfg_flash_img_info(t6_mfg_t *,
    t6_mfg_source_t);

typedef enum {
	T6_FLASH_VALIDATE_F_ERR		= 1 << 0,
	T6_FLASH_VALIDATE_F_NO_SOURCE	= 1 << 1
} t6_mfg_flash_vflags_t;

typedef struct {
	uint64_t		tfv_addr;
	uint32_t		tfv_range;
	t6_mfg_flash_vflags_t	tfv_flags;
	uint32_t		tfv_err;
} t6_mfg_flash_vdata_t;

typedef boolean_t (*t6_mfg_flash_validate_f)(const t6_mfg_flash_vdata_t *,
    void *);
extern boolean_t t6_mfg_flash_validate(t6_mfg_t *, t6_mfg_source_t,
    t6_mfg_flash_validate_f, void *);

/*
 * The following is used to track progress that has occurred during operations
 * and is optional. Note, progress events are not supported for all operations.
 * It is currently supported for srom and flash reads and writes. There are
 * three separate types of events right now. A general I/O progress indicator,
 * which leads to the event information containing the current read/write offset
 * out of the total and then a separate pair of events for SPI flash erasure. If
 * we end up supporting a partial erasure (e.g. to just update the firmware
 * section), then we should go back and add an erasure progress tracker for how
 * many sectors we have erased.
 *
 * XXX figure out if we should even do full chip erase
 */
typedef enum {
	T6_MFG_PROG_ERROR,
	T6_MFG_PROG_ERASE_BEGIN,
	T6_MFG_PROG_ERASE_END,
	T6_MFG_PROG_IO_START,
	T6_MFG_PROG_IO,
	T6_MFG_PROG_IO_END
} t6_mfg_progress_event_t;

typedef struct {
	t6_mfg_progress_event_t tmp_type;
	uint64_t tmp_offset;
	uint64_t tmp_total;
} t6_mfg_progress_t;

typedef void (*t6_mfg_progress_f)(const t6_mfg_progress_t *, void *);
extern boolean_t t6_mfg_set_progress_cb(t6_mfg_t *, t6_mfg_progress_f, void *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBT6MFG_H */
