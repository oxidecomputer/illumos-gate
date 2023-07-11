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

#ifndef _SYS_TOFINO_IMPL_H
#define	_SYS_TOFINO_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following triplet is used to version the interface between the tofino
 * driver and the userspace dataplane daemon.  This is primarily expected to be
 * bumped when an ioctl changes, but it needn't be limited to that.  For
 * example, changing the device name or the poll semantics would trigger a
 * version change as well.
 */
#define	TOFINO_DRIVER_MAJOR	1
#define	TOFINO_DRIVER_MINOR	1
#define	TOFINO_DRIVER_PATCH	0

#define	TOFINO_VENDID		0x1d1c

#define	TOFINO_DEVID_TF1_A0	0x0001
#define	TOFINO_DEVID_TF1_B0	0x0010
#define	TOFINO_DEVID_TF2_A0	0x0100
#define	TOFINO_DEVID_TF2_A00	0x0000
#define	TOFINO_DEVID_TF2_B0	0x0110

/*
 * The device is organized as three 64-bit BARs.
 */
#define	TOFINO_NBARS		3

/*
 * This is the maximum MSI interrupts that are expected by user land software if
 * more than one MSI is available.
 */
#define	TOFINO_MAX_MSI_INTRS	2

typedef enum {
	TOFINO_A_INTR_ALLOC	= 1 << 0,
	TOFINO_A_INTR_HANDLERS	= 1 << 1,
	TOFINO_A_INTR_ENABLE	= 1 << 2,
	TOFINO_A_MINOR		= 1 << 3,
} tofino_attach_t;

typedef struct tofino {
	list_node_t		tf_link;
	kmutex_t		tf_mutex;
	kcondvar_t		tf_cv;
	int			tf_instance;
	dev_info_t		*tf_dip;
	ddi_acc_handle_t	tf_cfgspace;
	tofino_gen_t		tf_gen;
	uint32_t		tf_devid;
	tofino_attach_t		tf_attach;
	ddi_acc_handle_t	tf_regs_hdls[TOFINO_NBARS];
	caddr_t			tf_regs_bases[TOFINO_NBARS];
	off_t			tf_regs_lens[TOFINO_NBARS];

	int			tf_nintrs;
	int			tf_intr_cap;
	uint_t			tf_intr_pri;
	ddi_intr_handle_t	tf_intrs[TOFINO_MAX_MSI_INTRS];

	uint32_t		tf_intr_cnt[TOFINO_MAX_MSI_INTRS];
	struct pollhead		tf_pollhead;

	tofino_tbus_state_t	tf_tbus_state;
	tf_tbus_hdl_t		tf_tbus_client;
} tofino_t;

/*
 * An opaque pointer to this struct is returned when a tbus client registers
 * with the tofino driver.
 */
typedef struct tofino_tbus_client {
	tofino_t		*tbc_tofino;
	tofino_intr_hdlr	tbc_intr;
	void			*tbc_intr_arg;
	boolean_t		tbc_intr_busy;
} tofino_tbus_client_t;

extern ddi_dma_attr_t tofino_dma_attr;

/*
 * This structure is used to track each page that the switch daemon marks for
 * DMA.  We store them in a simple linked list.  Because there are a relatively
 * small number of them, and the list is only consulted during daemon startup
 * and shutdown, there is no need for anything more performant and complex.
 */
typedef struct tofino_dma_page {
	list_node_t		td_list_node;
	caddr_t			td_va;
	uint32_t		td_refcnt;
	uintptr_t		td_dma_addr;
	ddi_dma_handle_t	td_dma_hdl;
	ddi_umem_cookie_t	td_umem_cookie;
} tofino_dma_page_t;

/*
 * Information maintained for each open() of a tofino device.
 */
typedef struct tofino_instance_data_t {
	kmutex_t		tid_mutex;
	tofino_t		*tid_tofino;
	uint32_t		tid_intr_read[TOFINO_MAX_MSI_INTRS];
	list_t			tid_pages;
} tofino_instance_data_t;

void tofino_dlog(tofino_t *tf, const char *fmt, ...);
void tofino_err(tofino_t *tf, const char *fmt, ...);
int tofino_read_reg(dev_info_t *dip, size_t offset, uint32_t *val);
int tofino_write_reg(dev_info_t *dip, size_t offset, uint32_t val);
void tofino_tbus_state_update(tofino_t *tf, tofino_tbus_state_t new_state);
tofino_t *tofino_get_phys(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TOFINO_IMPL_H */
