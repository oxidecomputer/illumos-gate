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
 * This implements the interfaces required to get PCI resource discovery out to
 * the rest of the system. This is effectively a thin veneer around
 * genoa_fabric.c and related pieces of our platform's unix.
 */

#include <sys/plat/pci_prd.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/io/genoa/fabric.h>

/*
 * We always just tell the system to scan all PCI buses.
 */
uint32_t
pci_prd_max_bus(void)
{
	return (PCI_MAX_BUS_NUM - 1);
}

struct memlist *
pci_prd_find_resource(uint32_t bus, pci_prd_rsrc_t rsrc)
{
	struct memlist *ret;
	switch (rsrc) {
	case PCI_PRD_R_IO:
	case PCI_PRD_R_MMIO:
	case PCI_PRD_R_BUS:
	case PCI_PRD_R_PREFETCH:
		/*
		 * XXX The traditional memlists that the kernel builds via
		 * memlist_new.c use both the forward and rear links in the
		 * pointers for ease of management. However, the pci_memlist.c
		 * implementation only uses the forward pointers. As such, we go
		 * through and NULL out all the previous pointers here to just
		 * keep things what PCI expects and so as not to confuse someone
		 * who is debugging later.
		 */
		ret = genoa_fabric_pci_subsume(bus, rsrc);
		if (ret != NULL) {
			struct memlist *fix = ret;
			while (fix != NULL) {
				fix->ml_prev = NULL;
				fix = fix->ml_next;
			}
		}
		return (ret);
	default:
		return (NULL);
	}
}

/*
 * No broken BIOS here!
 */
boolean_t
pci_prd_multi_root_ok(void)
{
	return (B_TRUE);
}

int
pci_prd_init(pci_prd_upcalls_t *upcalls)
{
	return (0);
}

void
pci_prd_fini(void)
{

}

/*
 * XXX we should probably implement these soon. Punting for the moment.
 */
void
pci_prd_root_complex_iter(pci_prd_root_complex_f func, void *arg)
{

}

/*
 * We have no alternative slot naming here. So this is a no-op and thus empty
 * function.
 */
void
pci_prd_slot_name(uint32_t bus, dev_info_t *dip)
{

}

/*
 * This indicates to the system what naming compatibility we would like to have.
 * On the Oxide platform, ISA free is the way to be, hence we don't need that.
 * Similarly we don't need bridge compatibility because we know all the systems
 * that the Oxide architecture runs on and we are not subject to issues there.
 * We still set PCI_PRD_COMPAT_PCI_NODE_NAME not for a principled reason, but
 * mostly to minimize risk late in the cycle here. XXX I am a chicken.
 */
pci_prd_compat_flags_t
pci_prd_compat_flags(void)
{
	return (PCI_PRD_COMPAT_PCI_NODE_NAME);
}

static struct modlmisc pci_prd_modlmisc_oxide = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "Oxide PCI Resource Discovery"
};

static struct modlinkage pci_prd_modlinkage_oxide = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &pci_prd_modlmisc_oxide, NULL }
};

int
_init(void)
{
	return (mod_install(&pci_prd_modlinkage_oxide));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&pci_prd_modlinkage_oxide, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&pci_prd_modlinkage_oxide));
}
