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

#ifndef _SYS_IPCC_INVENTORY_H
#define	_SYS_IPCC_INVENTORY_H

/*
 * This file contains all of the different structures that are described by the
 * IPCC inventory subsystem and related. Similar to SMBIOS, different structures
 * have a type which is indicated by a tag and then is followed by a specific
 * structure that is based on that type. These structures have members that are
 * appended to them in subsequent software revisions and the length of the
 * overall data payload is used to indicate what is valid.
 */

#include <sys/stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the current version of the IPCC inventory data structures which is
 * returned through the key lookup mechanisms.
 */
#define	IPCC_INV_VERS	0

/*
 * This is the index and structure of the inventory key that is used to
 * determine the total number of entries and the version.
 */
#define	IPCC_INV_KEYNO	2
typedef struct {
	uint32_t iki_nents;
	uint32_t iki_vers;
} ipcc_inv_key_t;

typedef enum {
	IPCC_INVENTORY_T_DDR4 = 0,
	IPCC_INVENTORY_T_VPDID,
	IPCC_INVENTORY_T_AT24CSW,
	IPCC_INVENTORY_T_STM32H7,
	IPCC_INVENTORY_T_BRM491,
	IPCC_INVENTORY_T_ISL68224,
	IPCC_INVENTORY_T_RAA229618,
	IPCC_INVENTORY_T_TPS546B24A,
	IPCC_INVENTORY_T_FANTRAY,
	IPCC_INVENTORY_T_ADM1272,
	IPCC_INVENTORY_T_TMP117,
	IPCC_INVENTORY_T_8A34XXX,
	IPCC_INVENTORY_T_KSZ8463
} ipcc_inv_type_t;

#pragma pack(1)
typedef struct {
	uint8_t ddr4_spd[512];
} ipcc_inv_ddr4_t;

typedef struct {
	uint8_t vpdid_pn[51];
	uint32_t vpdid_rev;
	uint8_t vpdid_sn[51];
} ipcc_inv_vpdid_t;

typedef struct {
	uint8_t at24_serial[16];
} ipcc_inv_at24csw_t;

typedef struct {
	uint32_t stm_uid[3];
	uint16_t stm_revid;
	uint16_t stm_devid;
} ipcc_inv_stm32h7_t;

typedef struct {
	uint8_t bmr_mfr_id[12];
	uint8_t bmr_mfr_model[20];
	uint8_t bmr_mfr_rev[12];
	uint8_t bmr_mfr_loc[12];
	uint8_t bmr_mfr_date[12];
	uint8_t bmr_mfr_serial[20];
	uint8_t bmr_mfr_fw[20];
} ipcc_inv_bmr491_t;

typedef struct {
	uint8_t isl_mfr_id[4];
	uint8_t isl_mfr_model[4];
	uint8_t isl_mfr_rev[4];
	uint8_t isl_mfr_date[4];
	uint8_t isl_ic_id[4];
	uint8_t isl_ic_rev[4];
} ipcc_inv_isl68224_t;

typedef struct {
	uint8_t raa_mfr_id[4];
	uint8_t raa_mfr_model[4];
	uint8_t raa_mfr_rev[4];
	uint8_t raa_mfr_date[4];
	uint8_t raa_ic_id[4];
	uint8_t raa_ic_rev[4];
} ipcc_inv_raa229618_t;

typedef struct {
	uint8_t tps_mfr_id[3];
	uint8_t tps_mfr_model[3];
	uint8_t tps_mfr_rev[3];
	uint8_t tps_mfr_serial[3];
	uint8_t tps_ic_id[6];
	uint8_t tps_ic_rev[2];
	uint16_t tps_nvm_cksum;
} ipcc_inv_tps546b24a_t;

typedef struct {
	ipcc_inv_vpdid_t ft_id;
	ipcc_inv_vpdid_t ft_board;
	ipcc_inv_vpdid_t ft_fans[4];
} ipcc_inv_fantray_t;

typedef struct {
	uint8_t adm_mfr_id[3];
	uint8_t adm_mfr_model[10];
	uint8_t adm_mfr_rev[2];
	uint8_t adm_mfr_date[6];
} ipcc_inv_adm1272_t;

typedef struct {
	/*
	 * Because these values are always read via i2c they will always be
	 * encoded as a big endian value unlike everything else in the system.
	 */
	uint16_t tmp_id;
	uint16_t tmp_ee1;
	uint16_t tmp_ee2;
	uint16_t tmp_ee3;
} ipcc_inv_tmp11x_t;

typedef struct {
	uint8_t idt_hwrev;
	uint8_t idt_major;
	uint8_t idt_minor;
	uint8_t idt_hotfix;
	uint8_t idt_product;
} ipcc_inv_idt8a34003_t;

typedef struct {
	uint16_t ksz_cider;
} ipcc_inv_ksz8463_t;

#pragma pack() /* pack(1) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IPCC_INVENTORY_H */
