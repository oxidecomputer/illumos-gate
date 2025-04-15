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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_SYS_TOFINO_H
#define	_SYS_TOFINO_H

#include <sys/ccompile.h>

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
	uint8_t		sc_pad;
	uint16_t	sc_ingress;
	uint16_t	sc_egress;
	uint16_t	sc_ethertype;
	uint8_t		sc_payload[16];
} __packed schdr_t;

/*
 * These codes are also defined in the p4 code that runs on the tofino ASIC.
 */
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
#define	BF_GET_VERSION		TOF_IOC(0x1002)

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

/*
 * Used to communicate the tofino driver version number to the userspace daemon.
 */
typedef struct {
	uint32_t tofino_major;
	uint32_t tofino_minor;
	uint32_t tofino_patch;
} tofino_version_t;

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
	TOFINO_G_TF2,
	TOFINO_G_INVALID
} tofino_gen_t;

typedef enum {
	TF_TBUS_UNINITIALIZED,
	TF_TBUS_REMOVED,
	TF_TBUS_RESETTING,
	TF_TBUS_RESET,
	TF_TBUS_READY,
} tofino_tbus_state_t;

typedef int (*tofino_intr_hdlr)(void *);

int tofino_tbus_ready(dev_info_t *);
int tofino_tbus_register(dev_info_t *);
int tofino_tbus_unregister(dev_info_t *);
int tofino_tbus_register_intr(dev_info_t *, tofino_intr_hdlr, void *);
void tofino_tbus_unregister_intr(dev_info_t *);
tofino_tbus_state_t tofino_tbus_state(dev_info_t *);
const char *tofino_state_name(tofino_tbus_state_t);
tofino_gen_t tofino_get_generation(dev_info_t *);

int tofino_tbus_read_reg(dev_info_t *, size_t offset, uint32_t *val);
int tofino_tbus_write_reg(dev_info_t *, size_t offset, uint32_t val);
int tofino_tbus_clear_reg(dev_info_t *, size_t offset);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TOFINO_H */
