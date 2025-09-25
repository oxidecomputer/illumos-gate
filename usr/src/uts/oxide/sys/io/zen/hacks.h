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

#ifndef	_SYS_IO_ZEN_HACKS_H
#define	_SYS_IO_ZEN_HACKS_H

#include <sys/stdint.h>
#include <sys/io/zen/platform_impl.h>

/*
 * Support for various and sundry hacks that we have had to add for particular
 * quirks in Zen platforms.  Not all of these apply to every microarchitecture.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_fabric zen_fabric_t;
typedef struct zen_pcie_port_info zen_pcie_port_info_t;

/*
 * Setup the SoC so that a single core shutdown (e.g., due to a triple fault)
 * results in a machine reset through A2.
 */
extern void zen_shutdown_detect_init(void);

/*
 * Set up SSC clocking.  This clock hack enables or disables PCIe spread
 * spectrum clocking via the FCH clock generator.
 */
extern bool zen_cgpll_set_ssc(bool);

/*
 * Check for furtive reset and panic according.  A furtive reset is one that
 * cannot be detected by the SP for some reason, such as, on Milan, non-reserved
 * bits set in FCH::PM::S5_RESET_STATUS during a window where RESET_L/PWROK is
 * not toggled.
 */
extern void zen_check_furtive_reset(void);

/*
 * We'd like to open the GPIO driver and do this properly, but we need to
 * manipulate GPIOs before the DDI is fully set up.  So we have this handy
 * function to do it for us directly.
 *
 * This is used to release PERST during the LISM on Ethanol-X, Ruby, etc (but
 * not Gimlet or Cosmo, which uses the GPIO expanders for PERST) and to signal
 * register capture for PCIe debugging via a logic analyzer.
 *
 * The operations are all straightforward and will work on any GPIO that has
 * been configured, whether by us, by firmware, or at power-on reset.  If the
 * mux has not been configured, this will still work but there will be no
 * visible effect outside the processor.
 */
typedef enum zen_hack_gpio_op {
	ZHGOP_RESET,
	ZHGOP_SET,
	ZHGOP_TOGGLE
} zen_hack_gpio_op_t;

extern void zen_hack_gpio(zen_hack_gpio_op_t, uint16_t);
extern void zen_hack_gpio_config(uint16_t, uint8_t);

extern void zen_fabric_hack_bridges(zen_fabric_t *);

extern void zen_gpio_watchdog(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HACKS_H */
