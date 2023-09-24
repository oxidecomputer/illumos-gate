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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef	_SYS_IO_MILAN_PSP_IMPL_H
#define	_SYS_IO_MILAN_PSP_IMPL_H

#include <sys/bitext.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	PSPIOC (('p' << 8) | ('A' << 16))   /* 'p' represents Psp driver */

#define	PSP_GET_SUPPORTED_FEATURES (PSPIOC | 0x05)
#define	PSP_GET_HSTI_STATE (PSPIOC | 0x14)
#define	PSP_GET_VERSION (PSPIOC | 0x19)
#define	PSP_GET_KVM_INFO (PSPIOC | 0x1e)
#define	PSP_GET_SPI_BLOCK_SIZE (PSPIOC | 0x83)

#define	PSP_DISABLE_PSB (PSPIOC | 0x4a)
#define	PSP_POST_DONE (PSPIOC | 0x06)
#define	PSP_ABORT_COMMAND (PSPIOC | 0xfe)
#define	PSP_INJECT_RAS_ERROR (PSPIOC | 0x22)
#define	PSP_SET_ACPI_EINJ (PSPIOC | 0x41)

#define	PSP_FUSE_PSB (PSPIOC | 0x21)

/*
 * Usage:
 *
 *    1. milan_psp_*_t buffer = {0};
 *    2. buffer.xxx = 5;
 *    3. status = ioctl(PSP_*, MILAN_PSP_PARAM_HEADER_INIT(&buffer));
 *    4. Check status. It can be EFAULT etc, or == 0 (OK),
 *       or > 0 (returned status code from PSP).
 *    5. Access buffer.
 *
 * It is recommended to use PSP_GET_VERSION first in order to make sure that
 * the struct layouts are matching.
 */

typedef struct {
	uint32_t mpph_total_size;
	uint32_t mpph_status;
} milan_psp_param_header_t;
CTASSERT(sizeof (milan_psp_param_header_t) == 8);

#define	PSB_DISABLING_CODE 0x50534244

typedef struct {
	milan_psp_param_header_t mpgvr_header;
	uint32_t mpgvr_psp_fw_version;
	uint32_t mpgvr_agesa_fw_version;
	uint32_t mpgvr_appb_fw_version;
	uint32_t mpgvr_apcb_fw_version;
	uint32_t mpgvr_apob_fw_version;
	uint32_t mpgvr_smu_fw_version;
} milan_psp_get_version_response_t;

typedef struct {
	milan_psp_param_header_t mpgsfr_header;
	uint32_t mpgsfr_features;
} milan_psp_get_supported_features_response_t;

typedef struct {
	milan_psp_param_header_t mpgsbsr_header;
	uint64_t mpgsbsr_target_nv;
	uint64_t mpgsbsr_starting_lba;
	uint64_t mpgsbsr_block_size;
	uint64_t mpgsbsr_block_count;
} milan_psp_get_spi_block_size_response_t;

typedef struct {
	milan_psp_param_header_t mpgkir_header;
	uint32_t mpgkir_dma_addr_mpgkir_lo;
	uint32_t mpgkir_dma_addr_hi;
	uint32_t mpgkir_dma_size;
} milan_psp_get_kvm_info_response_t;

typedef struct {
	milan_psp_param_header_t mpghsr_header;
	uint32_t mpghsr_hsti_state;
} milan_psp_get_hsti_state_response_t;

typedef struct {
	milan_psp_param_header_t mpdp_header;
	uint32_t mpdp_result;
	uint32_t mpdp_psb_disabling_code;
} milan_psp_disable_psb_t;

typedef struct {
	milan_psp_param_header_t mpire_header;
	uint32_t mpire_action;
	// then more stuff iff action == 4
} milan_psp_inject_ras_error_t;

typedef struct {
	milan_psp_param_header_t mpsae_header;
	uint32_t mpsae_action; // 1: on; 2: off
} milan_psp_set_acpi_einj_t;

// dummy
typedef struct {
	milan_psp_param_header_t mpacr_header;
} milan_psp_abort_command_response_t;

typedef struct {
	milan_psp_param_header_t mppd_header;
} milan_psp_post_done_t;

typedef struct {
	milan_psp_param_header_t mpfp_header;
} milan_psp_fuse_psb_t;

static inline void* milan_psp_param_header_init(void* s, size_t size) {
	milan_psp_param_header_t *header = s;
	header->mpph_total_size = size;
	header->mpph_status = 0;
	return (s);
}
#define	MILAN_PSP_PARAM_HEADER_INIT(s) \
	milan_psp_param_header_init((s), sizeof (*(s)))

#ifdef __cplusplus
};
#endif

#endif /* _SYS_IO_MILAN_PSP_IMPL_H */
