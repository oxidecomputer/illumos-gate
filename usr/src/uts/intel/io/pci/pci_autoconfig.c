/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2022 Oxide Computer Company
 */

/*
 * Determine the PCI configuration mechanism recommended by the BIOS.
 */

#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/pci_impl.h>
#include <sys/ddi_subrdefs.h>
#include <sys/bootconf.h>
#include <sys/psw.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/pci.h>
#include <sys/pci_cfgspace.h>
#include <sys/reboot.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/mutex.h>
#include <sys/plat/pci_prd.h>

extern int pci_boot_debug;
extern int pci_boot_maxbus;

/*
 * Interface routines
 */
void pci_enumerate(int);
void pci_setup_tree(void);
void pci_reprogram(void);
dev_info_t *pci_boot_bus_to_dip(uint32_t);

uint_t pci_autoconfig_detach = 0;

static struct modlmisc modlmisc = {
	&mod_miscops, "PCI BIOS interface"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

static pci_prd_upcalls_t pci_upcalls = {
	.pru_bus2dip_f = pci_boot_bus_to_dip
};

int
_init(void)
{
	int	err;

	if ((err = pci_prd_init(&pci_upcalls)) != 0) {
		return (err);
	}

	if ((err = mod_install(&modlinkage)) != 0) {
		pci_prd_fini();
		return (err);
	}

	impl_bus_add_probe(pci_enumerate);
	return (0);
}

int
_fini(void)
{
	int	err;

	/*
	 * Detach of this module is fairly unsafe and reattach even more so.
	 * Don't detach unless someone has gone out of the way with mdb -kw.
	 */
	if (pci_autoconfig_detach == 0)
		return (EBUSY);

	if ((err = mod_remove(&modlinkage)) != 0)
		return (err);

	impl_bus_delete_probe(pci_enumerate);
	pci_prd_fini();
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * This function is invoked twice: first time, with reprogram=0 to
 * set up the PCI portion of the device tree. The second time is
 * for reprogramming devices not set up by the BIOS.
 */
void
pci_enumerate(int reprogram)
{
	extern void add_pci_fixes(void);
	extern void undo_pci_fixes(void);

	/*
	 * On our first pass through here actually determine what the maximum
	 * bus that we should use is.
	 */
	if (reprogram == 0) {
		pci_boot_maxbus = pci_prd_max_bus();
	}

	add_pci_fixes();

	if (reprogram) {
		pci_reprogram();
		undo_pci_fixes();
		return;
	}

	/* setup device tree */
	pci_setup_tree();
	undo_pci_fixes();
}
