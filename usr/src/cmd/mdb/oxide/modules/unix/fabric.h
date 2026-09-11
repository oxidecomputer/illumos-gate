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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_FABRIC_H
#define	_FABRIC_H

#ifdef	__cplusplus
extern "C" {
#endif

extern int fabric_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_dcmd_help(void);

extern int fabric_ioms_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_ioms_dcmd_help(void);

extern int fabric_pcie_core_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_pcie_core_dcmd_help(void);
extern int fabric_pcie_port_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_pcie_port_dcmd_help(void);
extern int fabric_nbif_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_nbif_dcmd_help(void);
extern int fabric_nbif_func_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_nbif_func_dcmd_help(void);
extern int fabric_ccd_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_ccd_dcmd_help(void);
extern int fabric_ccx_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_ccx_dcmd_help(void);
extern int fabric_core_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_core_dcmd_help(void);
extern int fabric_thread_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_thread_dcmd_help(void);

#define	FABRIC_DCMDS	\
	{ "fabric", "?[-cnv]", "summarise the fabric", fabric_dcmd, \
	    fabric_dcmd_help }, \
	{ "ioms", "?[-n num] [-h hub] [-N nbio] [-i iohc] [-b bus]\n" \
	    "\t\t[-f flags] [-x flags]", "show IOMS", fabric_ioms_dcmd, \
	    fabric_ioms_dcmd_help }, \
	{ "pcie_core", "?[-n num] [-i iohc] [-b bus] [-f flags] [-x flags]", \
	    "show PCIe cores", fabric_pcie_core_dcmd, \
	    fabric_pcie_core_dcmd_help }, \
	{ "pcie_port", "?[-n num] [-c core] [-b bus] [-s slot]\n" \
	    "\t\t[-f flags] [-x flags]", "show PCIe ports", \
	    fabric_pcie_port_dcmd, fabric_pcie_port_dcmd_help }, \
	{ "nbif", "?[-n num] [-b bus]", "show nBIFs", fabric_nbif_dcmd, \
	    fabric_nbif_dcmd_help }, \
	{ "nbif_func", "?[-n num] [-b bus] [-t type] [-f flags] [-x flags]", \
	    "show nBIF functions", fabric_nbif_func_dcmd, \
	    fabric_nbif_func_dcmd_help }, \
	{ "ccd", "?[-n num] [-p phys]", "show CCDs", fabric_ccd_dcmd, \
	    fabric_ccd_dcmd_help }, \
	{ "ccx", "?[-n num] [-p phys]", "show CCXs", fabric_ccx_dcmd, \
	    fabric_ccx_dcmd_help }, \
	{ "zen_core", "?[-n num] [-p phys]", "show CPU cores", \
	    fabric_core_dcmd, fabric_core_dcmd_help }, \
	{ "zen_thread", "?[-n num] [-a apicid]", "show CPU threads", \
	    fabric_thread_dcmd, fabric_thread_dcmd_help }

/*
 * The levels of the fabric tree that can be walked. A walker's level is
 * passed to the common walk functions as its init argument.
 */
typedef enum {
	FABRIC_L_ROOT,
	FABRIC_L_SOC,
	FABRIC_L_IODIE,
	FABRIC_L_NBIO,
	FABRIC_L_IOMS,
	FABRIC_L_PCIE_CORE,
	FABRIC_L_PCIE_PORT,
	FABRIC_L_NBIF,
	FABRIC_L_NBIF_FUNC,
	FABRIC_L_CCD,
	FABRIC_L_CCX,
	FABRIC_L_CORE,
	FABRIC_L_THREAD,
	FABRIC_L_NLEVELS
} fabric_level_t;

extern int fabric_walk_init(mdb_walk_state_t *);
extern int fabric_walk_step(mdb_walk_state_t *);

#define	FABRIC_WALKER(_name, _descr, _level)	\
	{ _name, _descr, fabric_walk_init, fabric_walk_step, NULL, \
	    (void *)(uintptr_t)(_level) }

#define	FABRIC_WALKERS	\
	FABRIC_WALKER("soc", "walk SOCs", FABRIC_L_SOC), \
	FABRIC_WALKER("iodie", "walk IODIEs", FABRIC_L_IODIE), \
	FABRIC_WALKER("nbio", "walk NBIOs", FABRIC_L_NBIO), \
	FABRIC_WALKER("ioms", "walk IOMS", FABRIC_L_IOMS), \
	FABRIC_WALKER("pcie_core", "walk PCIe cores", FABRIC_L_PCIE_CORE), \
	FABRIC_WALKER("pcie_port", "walk PCIe ports", FABRIC_L_PCIE_PORT), \
	FABRIC_WALKER("nbif", "walk nBIFs", FABRIC_L_NBIF), \
	FABRIC_WALKER("nbif_func", "walk nBIF functions", \
	    FABRIC_L_NBIF_FUNC), \
	FABRIC_WALKER("ccd", "walk CCDs", FABRIC_L_CCD), \
	FABRIC_WALKER("ccx", "walk CCXs", FABRIC_L_CCX), \
	FABRIC_WALKER("zen_core", "walk CPU cores", FABRIC_L_CORE), \
	FABRIC_WALKER("zen_thread", "walk CPU threads", FABRIC_L_THREAD)

#ifdef	__cplusplus
}
#endif

#endif	/* _FABRIC_H */
