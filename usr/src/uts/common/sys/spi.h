/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2022 by Oxide Computer, Inc.
 * All rights reserved.
 */

#ifndef _SYS_SPI_H
#define _SYS_SPI_H

#include <sys/types32.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* spidev ioctl */
#define	SPIDEV_IOC	('k' << 8)
#define	SPIDEV_TRANSACTION		(SPIDEV_IOC | 0)	/* Perform a sequence of SPI transfers */

typedef struct spidev_transfer {
    /* data to be written or NULL */
    const uint8_t     *tx_buf;
    /* data to be read or NULL */
    uint8_t     *rx_buf;

    /* size of TX and RX buffers (in bytes) */
    uint32_t    len;

    /*
     * delay introduced after this transfer but before the next
     * transfer or completion of transaction.
     */
    uint16_t    delay_usec;

    /* When non-zero, deassert chip select at end of this transfer. */
    uint8_t     deassert_cs;
} spidev_transfer_t;

typedef struct spidev_transaction {
    spidev_transfer_t   *spidev_xfers;
    uint8_t             spidev_nxfers;
} spidev_transaction_t;

typedef struct spidev_transfer32 {
    caddr32_t   tx_buf;
    caddr32_t   rx_buf;

    uint32_t    len;
    uint16_t    delay_usec;
    uint8_t     deassert_cs;
} spidev_transfer32_t;

typedef struct spidev_transaction32 {
    caddr32_t   spidev_xfers;
    uint8_t     spidev_nxfers;
} spidev_transaction32_t;

#ifdef	__cplusplus
}
#endif

#endif  /* _SYS_SPI_H */
