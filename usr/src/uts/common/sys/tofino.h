/*
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 */

/*
 * Copyright 2022 Oxide Computer Company
 */

#ifndef	_SYS_TOFINO_H
#define	_SYS_TOFINO_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sidecar network header
 *
 * This header is inserted between the ethernet and ip headers by the p4 program
 * running on the Tofino ASIC.
 */
typedef struct schdr {
	uint8_t		sc_code;
	uint16_t	sc_ingress;
	uint16_t	sc_egress;
	uint16_t	sc_ethertype;
	uint8_t		sc_payload[16];
} __packed schdr_t;

#define	SC_FORWARD_FROM_USERSPACE	0x00
#define	SC_FORWARD_TO_USERSPACE		0x01
#define	SC_ICMP_NEEDED			0x02
#define	SC_ARP_NEEDED			0x03
#define	SC_NEIGHBOR_NEEDED		0x04
#define	SC_INVALID			0xff

#define	TOC_IOC_PREFIX	0x1d1c
#define	TOF_IOC(x) ((TOC_IOC_PREFIX << 16) | x)

/* Update truss */
#define	BF_IOCMAPDMAADDR	TOF_IOC(0x0001)
#define	BF_IOCUNMAPDMAADDR	TOF_IOC(0x0002)
#define	BF_TBUS_MSIX_INDEX	TOF_IOC(0x0003)
#define	BF_GET_INTR_MODE	TOF_IOC(0x0004)
#define	BF_PKT_INIT		TOF_IOC(0x1000)
#define	BF_GET_PCI_DEVID	TOF_IOC(0x1001)

#define	BF_INTR_MODE_NONE	0
#define	BF_INTR_MODE_LEGACY	1
#define	BF_INTR_MODE_MSI	2
#define	BF_INTR_MODE_MSIX	3

/*
 * This structure is used to communicate parameters for the DMA mapping
 * ioctl from the userspace daemon.
 */
typedef struct {
	caddr_t va;
	uintptr_t dma_addr;
	size_t size;
} bf_dma_bus_map_t;

#ifdef _KERNEL
/*
 * Metadata used for tracking each DMA memory allocation.
 */
typedef struct tf_tbus_dma {
	ddi_dma_handle_t	tpd_handle;
	ddi_acc_handle_t	tpd_acchdl;
	ddi_dma_cookie_t	tpd_cookie;
	caddr_t			tpd_addr;
	size_t			tpd_len;
} tf_tbus_dma_t;

typedef struct tofino_tbus_client *tf_tbus_hdl_t;

typedef enum {
	TOFINO_G_TF1 = 1,
	TOFINO_G_TF2
} tofino_gen_t;

typedef enum {
	TF_TBUS_UNINITIALIZED,
	TF_TBUS_RESETTING,
	TF_TBUS_RESET,
	TF_TBUS_READY,
} tofino_tbus_state_t;

int tofino_tbus_register(tf_tbus_hdl_t *);
int tofino_tbus_unregister(tf_tbus_hdl_t);
int tofino_tbus_register_softint(tf_tbus_hdl_t, ddi_softint_handle_t);
int tofino_tbus_unregister_softint(tf_tbus_hdl_t, ddi_softint_handle_t);
tofino_tbus_state_t tofino_tbus_state(tf_tbus_hdl_t);
int tofino_get_generation(tf_tbus_hdl_t);

int tofino_tbus_read_reg(tf_tbus_hdl_t, size_t offset, uint32_t *val);
int tofino_tbus_write_reg(tf_tbus_hdl_t, size_t offset, uint32_t val);

int tofino_tbus_dma_alloc(tf_tbus_hdl_t, tf_tbus_dma_t *, size_t, int);
void tofino_tbus_dma_free(tf_tbus_dma_t *);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TOFINO_H */
