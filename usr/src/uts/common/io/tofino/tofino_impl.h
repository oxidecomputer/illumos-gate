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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _SYS_TOFINO_IMPL_H
#define	_SYS_TOFINO_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

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

typedef struct tofino_tbus_client {
	uint32_t		tbc_dma_allocs;
	uint32_t		tbc_dma_frees;
	ddi_softint_handle_t	tbc_tbus_softint;
} tofino_tbus_client_t;

typedef struct tofino {
	kmutex_t		tf_mutex;
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

// We always use 2MB pages for Tofino DMA ranges
#define	TF_DMA_PGSIZE	(1 << 21)
#define	TF_DMA_PGMASK	((1 << 21) - 1)

/*
 * This structure is used to track each page that the switch daemon marks for
 * DMA.  We store them in a simple linked list.  Because there are a relatively
 * small number of them, and the list is only consulted during daemon startup
 * and shutdown, there is no need for anything more performant and complex.
 */
typedef struct tofino_dma_page {
	caddr_t			td_va;
	uint32_t		td_refcnt;
	uintptr_t		td_dma_addr;
	ddi_dma_handle_t	td_dma_hdl;
	ddi_umem_cookie_t	td_umem_cookie;
	ddi_dma_cookie_t	td_dma_cookie;
	struct tofino_dma_page	*td_next;
} tofino_dma_page_t;

/*
 * Information maintained for each open() of a tofino device.
 */
typedef struct tofino_open {
	kmutex_t		to_mutex;
	tofino_t		*to_device;
	uint32_t		to_intr_read[TOFINO_MAX_MSI_INTRS];
	tofino_dma_page_t	*to_pages;
} tofino_open_t;

void tofino_dlog(tofino_t *tf, const char *fmt, ...);
void tofino_err(tofino_t *tf, const char *fmt, ...);
uint32_t tf_read_reg(dev_info_t *dip, size_t offset);
void tf_write_reg(dev_info_t *dip, size_t offset, uint32_t val);
int tofino_tbus_state_update(tofino_t *tf, tofino_tbus_state_t new_state);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TOFINO_IMPL_H */
