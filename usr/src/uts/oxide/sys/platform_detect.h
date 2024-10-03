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

#ifndef	_SYS_PLATFORM_DETECT_H
#define	_SYS_PLATFORM_DETECT_H

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/dw_apb_uart.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/fch.h>
#include <sys/io/zen/platform.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	OXIDE_BOARD_UNKNOWN = 0,
	OXIDE_BOARD_GIMLET,
	OXIDE_BOARD_ETHANOLX,
	OXIDE_BOARD_COSMO,
	OXIDE_BOARD_RUBY,
	OXIDE_BOARD_RUBYRED,	/* Ruby + Grapefruit */
} oxide_board_t;

typedef enum {
	IPCC_MODE_DISABLED = 0,
	IPCC_MODE_UART1,
} oxide_ipcc_mode_t;

typedef enum {
	IPCC_SPINTR_DISABLED = 0,
	IPCC_SPINTR_SP3_AGPIO139,
} oxide_ipcc_spintr_t;

#define	OXIDE_BOARD_BSU_NUM	2

typedef struct oxide_board_cpuinfo {
	x86_chiprev_t		obc_chiprev;
	const char		*obc_chiprevstr;
	x86_uarchrev_t		obc_uarchrev;
	uint32_t		obc_socket;
	fch_kind_t		obc_fchkind;
} oxide_board_cpuinfo_t;

typedef struct oxide_board_data {
	/*
	 * The derived system board type.
	 */
	const oxide_board_t	obd_board;
	/*
	 * The string used for the `mfg-name` system property. This becomes the
	 * name of the system root nexus.
	 */
	const char		*obd_rootnexus;
	/*
	 * A list of PCIe slots corresponding to different boot storage units.
	 */
	const uint16_t		obd_bsu_slot[OXIDE_BOARD_BSU_NUM];
	/*
	 * The mode in which IPCC should operate. This specifies if IPCC should
	 * be disabled (for boards that don't support it) or the transport that
	 * should be used.
	 */
	oxide_ipcc_mode_t	obd_ipccmode;
	/*
	 * Specifies the mechanism by which the IPCC out of band interrupt line
	 * from the SP operates.
	 */
	oxide_ipcc_spintr_t	obd_ipccspintr;
	/*
	 * The set of system startup options that should be used. This is for
	 * systems that do not support IPCC and replaces the startup options
	 * that would usually be retrieved over that channel.
	 */
	const uint64_t		obd_startupopts;

	/*
	 * The following structure is populated by oxide_derive_platform() once
	 * it has successfully identified the board.
	 */
	oxide_board_cpuinfo_t	obd_cpuinfo;

	/*
	 * Similarly, oxide_derive_platform() will set this pointer to the
	 * appropriate Zen platform structure.
	 */
	const zen_platform_t	*obd_zen_platform;
} oxide_board_data_t;

extern const oxide_board_data_t *oxide_board_data;

extern void oxide_derive_platform(void);
extern void oxide_report_platform(void);
extern bool oxide_board_is_ruby(void);

static inline const zen_platform_t *
oxide_zen_platform(void)
{
	ASSERT3P(oxide_board_data, !=, NULL);
	return (oxide_board_data->obd_zen_platform);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PLATFORM_DETECT_H */
