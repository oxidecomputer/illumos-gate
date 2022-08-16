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
 * Copyright 2022 Oxide Computer Co
 */

#include <sys/types.h>
#include <sys/sunddi.h>

/*
 * This is all stuff that common or x86 code assumes exists, but it's actually
 * specific to PCs or in a couple of cases is used only by/with OBP.  For now
 * it's easier to stub them out than to factor the code properly.
 */

/*
 * Used by openprom(4d) only.
 */
char saved_cmdline[1] = "";

/*
 * Used by pci_boot.c, which is really i86pc-specific.  Setting pseudo_isa
 * causes pci_boot to refrain from creating a device node for any PCI-ISA
 * bridges it encounters, which is desirable as we do not support ISA.
 */
int apic_nvidia_io_max = 0;
int pseudo_isa = 1;

void
read_bootenvrc(void)
{
}

void
progressbar_start(void)
{
}

/*ARGSUSED*/
int
acpi_ddi_setwake(dev_info_t *_d, int _i)
{
	return (0);
}

void
fastboot_update_config(const char *_)
{
}

void
fastboot_update_and_load(int _i, char *_p)
{
}

void
fastboot_post_startup(void)
{
}

void
fastreboot_disable_highpil(void)
{
}

void
ld_ib_prop(void)
{
}
