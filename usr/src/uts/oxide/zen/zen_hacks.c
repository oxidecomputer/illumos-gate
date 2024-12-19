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
 * Various and sundry hacks used across the various Zen microarchitectures
 * supported by the Oxide architecture.
 */

#include <sys/types.h>
#include <sys/stdbool.h>

#include <sys/amdzen/mmioreg.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/zen/hacks.h>
#include <sys/io/zen/uarch.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>

/*
 * It is an unfortunate reality that the reset and shutdown conditions of an
 * x86 microprocessor are ill-defined and dependent upon implicit interactions
 * between many different components:  the core inducing the shutdown or
 * reset, the other cores on the die, the hidden computer that is effectively
 * contained within that die (euphemistically called a system-on-a-chip), the
 * lowest level software running on each those components, and the surrounding
 * machine itself (replete with its own historical artifacts).  Each of these
 * is poorly documented and strictly proprietary; it is no surprise that their
 * confluence works by accident such as it works at all.  In short, it is a
 * midden pit of computing:  interesting, perhaps, to future anthropoligists
 * -- but consisting only of refuse, it was never designed at all, let alone
 *  to serve as foundation.
 *
 * The problem in front of us -- ludicrous as it may seem -- is to make sure
 * that a core shutdown properly induces a machine reset (that is, we wish
 * to transition the machine from A0 to A2).
 *
 * The first issue is even more basic:  assuring that a single core shutdown
 * in fact shuts down all cores.  (Amazingly, this is not the default
 * disposition, and a single core shutdown will just result in a chunk of the
 * system silently disappearing, with the rest of the system left to discover
 * its absence only through the prescribed work that it is apparently no
 * longer doing.)
 *
 * Experimentation has revealed that this issue can be resolved by setting
 * en_shutdown_msg in FCH::PM::ACPICONFIG: when this bit is set, a shutdown on
 * a single core results in a SHUTDOWN message being sent in such a way that
 * all cores shutdown.  This is important, but it is insufficent: the shutdown
 * message will result in all cores entering the shutdown state, but there
 * isn't further activity (that is, there is no reset, externally visible or
 * otherwise).
 *
 * Fortunately, there is an additional register, FCH::PM::PCICONTROL that has
 * a shutdownoption field; this is defined to "Generate Pci (sic) reset when
 * receiving shutdown message." The type of reset is itself not defined, but
 * it has been empirically determined that setting this bit does result in a
 * shutdown message inducing behavior consistent with a Warm Reset.
 * (Specifically: we see RESET_L become de-asserted for ~60 milliseconds while
 * PWROK remains asserted.) Note that the CPU itself appears to go back to ABL
 * under this condition, and retrains DIMMs, etc.
 *
 * Importantly, the SoC resets under this condition, but the FCH is not reset.
 * Specifically, FCH::PM::S5_RESET_STATUS does correctly reflect the reset
 * reason (namely, shutdown_msg is set). On the one hand, this is helpful in
 * that it gives us a potential backstop, but on the other hand it is chilling:
 * if there were any lingering doubts that the state of the system is too
 * ill-defined after a reset to depend on, this should eliminate them!
 *
 * Finally: setting rsttocpupwrgden in FCH::PM::RESETCONTROL1 results in what
 * appears to be closer to a cold reset, in that in addition to RESET_L being
 * asserted, PWROK is also de-asserted (for ~6 milliseconds).
 *
 * The below code takes these three actions, and together with modifications
 * to the boarder system to detect any change in RESET_L/PWROK, assures that
 * a single core shutdown (e.g., due to a triple fault) results in our
 * desired semantics:  a machine reset through A2.
 */
void
zen_shutdown_detect_init(void)
{
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
	mmio_reg_t reg;
	uint64_t val;

	reg = FCH_PMIO_ACPICONFIG_MMIO(fch_pmio);
	val = mmio_reg_read(reg);
	val = FCH_PMIO_ACPICONFIG_SET_EN_SHUTDOWN_MSG(val, 1);
	mmio_reg_write(reg, val);

	reg = FCH_PMIO_PCICONTROL_MMIO(fch_pmio);
	val = mmio_reg_read(reg);
	val = FCH_PMIO_PCICONTROL_SET_SHUTDOWNOPTION(val, 1);
	mmio_reg_write(reg, val);

	reg = FCH_PMIO_RESETCONTROL1_MMIO(fch_pmio);
	val = mmio_reg_read(reg);
	val = FCH_PMIO_RESETCONTROL1_SET_RSTTOCPUPWRGDEN(val, 1);
	mmio_reg_write(reg, val);

	mmio_reg_block_unmap(&fch_pmio);
}

/*
 * Calls the microarchitecture-specific PLL SSC (spread spectrum clock) setup
 * function.
 */
bool
zen_cgpll_set_ssc(bool enable)
{
	const zen_hack_ops_t *ops = oxide_zen_hack_ops();
	VERIFY3P(ops->zho_cgpll_set_ssc, !=, NULL);
	return (ops->zho_cgpll_set_ssc(enable));
}

/*
 * A null operation for PLL SSC setup, used by microarchitectures that don't
 * need a special hack for SSC setup.
 */
bool
zen_null_cgpll_set_ssc(bool enable)
{
	return (true);
}

/*
 * Check for furtive reset, which is a window where a reset could happen without
 * toggling a pin such as RESET_L/PWROK on Milan, that could be detected by the
 * SP.
 */
void
zen_check_furtive_reset(void)
{
	const zen_hack_ops_t *ops = oxide_zen_hack_ops();
	VERIFY3P(ops->zho_check_furtive_reset, !=, NULL);
	ops->zho_check_furtive_reset();
}

/*
 * A no-op check for furtive reset for microarchitectures that have no special
 * handling needs.
 */
void
zen_null_check_furtive_reset(void)
{
}

/*
 * Hack the GPIO!
 */
void
zen_hack_gpio(zen_hack_gpio_op_t op, uint16_t gpio)
{
	const zen_hack_ops_t *ops = oxide_zen_hack_ops();
	VERIFY3P(ops->zho_hack_gpio, !=, NULL);
	ops->zho_hack_gpio(op, gpio);
}

typedef struct zen_pci_bus_counter {
	zen_ioms_t *zpbc_ioms;
	uint8_t zpbc_busoff;
} zen_pci_bus_counter_t;

static int
zen_fabric_hack_bridges_cb(zen_pcie_port_t *port, void *arg)
{
	uint8_t bus, secbus;
	zen_pci_bus_counter_t *zpbc = arg;
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();

	/*
	 * Assign bus numbers for the internal NBIF bridges.  This only happens
	 * on the large IOHC types, as those are the only ones that have NBIFs.
	 * We only want to do this once per IOMS, and the check below implies
	 * that this always happens on PCIe core 0.
	 */
	bus = ioms->zio_pci_busno;
	if (zpbc->zpbc_ioms != ioms) {
		const zen_iohc_nbif_ports_t *int_ports =
		    &consts->zpc_pcie_int_ports[ioms->zio_iohcnum];
		zpbc->zpbc_busoff = 1 + int_ports->zinp_count;
		zpbc->zpbc_ioms = ioms;
		for (uint_t i = 0; i < int_ports->zinp_count; i++) {
			const zen_pcie_port_info_t *info =
			    &int_ports->zinp_ports[i];
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_PRIBUS, bus);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SECBUS, bus + 1 + i);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SUBBUS, bus + 1 + i);
		}
	}

	if ((port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN) != 0)
		return (0);

	secbus = bus + zpbc->zpbc_busoff;

	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_PRIBUS, bus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SECBUS, secbus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SUBBUS, secbus);
	zpbc->zpbc_busoff++;

	return (0);
}

/*
 * Work around deficiencies in software and emulate parts of the PCI firmware
 * spec. The OS should natively handle this.
 *
 * We program a single downstream bus onto each root port. We can only get away
 * with this because we know there are no other bridges right now.
 *
 * The logic in pci_boot.c really ought to take care of this.
 */
void
zen_fabric_hack_bridges(zen_fabric_t *fabric)
{
	zen_pci_bus_counter_t c;
	bzero(&c, sizeof (c));
	zen_fabric_walk_pcie_port(fabric, zen_fabric_hack_bridges_cb, &c);
}
