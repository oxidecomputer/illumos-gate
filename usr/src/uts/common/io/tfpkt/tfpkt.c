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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * The tofino ASIC includes a nic-like interface to the dataplane, using a set
 * of registers in PCI space.  These registers describe a collection of ring
 * buffers.  The dataplane pushes free memory buffers onto FM rings and packets
 * to be transmitted onto TX rings.  The ASIC pulls buffers from the FM rings
 * for incoming packets, and pushes the populated buffers onto RX rings.  When a
 * packet has been sucessfuly transmitted, the ASIC will push a completion event
 * onto a CMP ring.
 *
 * +---------+ +----------+  +----------+  +---------------+
 * |  Free   | | Incoming |  | Outgoing |  |  Completion   |
 * | buffers | | packets  |  | packets  |  | notifications |
 * +---------+ +----------+  +----------+  +---------------+
 *      |           ^             |                ^
 *      V           |             V                |
 * +---------+ +---------+   +---------+      +----------+
 * | FM ring | | RX ring |   | TX ring |      | CMP ring |
 * +---------+ +---------+   +---------+      +----------+
 *     |            ^             |                ^
 *     |            |             |                |
 * +---|------------|-------------|----------------|-----+
 * |   |            |             |                |     |
 * |   +-> Packet --+             +-->  Packet ----+     |
 * |       Receipt                     Transmit          |
 * |                     Tofino                          |
 * +-----------------------------------------------------+
 *
 * The Tofino register documentation refers to this collection of registers as
 * the "tbus", although it doesn't explain why.  Access to the tbus by the p4
 * program running on the ASIC is via port 0.
 *
 * This tfpkt driver provides access to this network-like device via a mac(9e)
 * interface.
 *
 * Also managing the tbus register set is the dataplane daemon, running in
 * userspace.  When the daemon (re)starts it resets the Tofino ASIC, erasing any
 * configuration performed by this driver.  We rely on the daemon issuing a
 * BF_TFPKT_INIT ioctl() before and after the reset for correct performance.
 * When we are notified that a reset is happening, we stop using the registers,
 * free the buffer memory we were using, and fail all attempted mac_tx() calls.
 * When the reset completes, we allocate a new collection of buffers and
 * reprogram the ring configuration registers.
 */

#include <sys/policy.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/dls.h>
#include <sys/strsubr.h>
#include <sys/mac_ether.h>
#include <sys/mac_provider.h>
#include <sys/vlan.h>
#include <sys/list.h>
#include <sys/mac_impl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/strsun.h>
#include <sys/tofino.h>
#include "tfpkt_impl.h"

#define	ETHSZ (sizeof (struct ether_header))
#define	SCSZ (sizeof (struct schdr))

static int tfpkt_attach(dev_info_t *, ddi_attach_cmd_t);
static int tfpkt_detach(dev_info_t *, ddi_detach_cmd_t);

/* MAC callback function declarations */
static int tfpkt_m_start(void *);
static void tfpkt_m_stop(void *);
static int tfpkt_m_promisc(void *, boolean_t);
static int tfpkt_m_multicst(void *, boolean_t, const uint8_t *);
static int tfpkt_m_unicst(void *, const uint8_t *);
static int tfpkt_m_stat(void *, uint_t, uint64_t *);
static mblk_t *tfpkt_m_tx(void *, mblk_t *);

DDI_DEFINE_STREAM_OPS(tfpkt_dev_ops, nulldev, nulldev, tfpkt_attach,
    tfpkt_detach, nodev, NULL, D_MP, NULL, ddi_quiesce_not_needed);

static mac_callbacks_t tfpkt_m_callbacks = {
	.mc_callbacks =			0,
	.mc_getstat =			tfpkt_m_stat,
	.mc_start =			tfpkt_m_start,
	.mc_stop =			tfpkt_m_stop,
	.mc_setpromisc =		tfpkt_m_promisc,
	.mc_multicst =			tfpkt_m_multicst,
	.mc_unicst =			tfpkt_m_unicst,
	.mc_tx =			tfpkt_m_tx,
};

static void
tfpkt_err(tfpkt_t *tfp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(tfp->tfp_dip, CE_WARN, fmt, args);
	va_end(args);
}

static int
tfpkt_mac_hold(tfpkt_t *tfp)
{
	ASSERT(tfp != NULL);

	mutex_enter(&tfp->tfp_mutex);
	if (tfp->tfp_runstate == TFPKT_RUNSTATE_RUNNING) {
		tfp->tfp_mac_refcnt++;
		mutex_exit(&tfp->tfp_mutex);
		return (0);
	} else {
		mutex_exit(&tfp->tfp_mutex);
		return (-1);
	}
}

static void
tfpkt_mac_release(tfpkt_t *tfp)
{
	ASSERT(tfp != NULL);

	mutex_enter(&tfp->tfp_mutex);
	ASSERT(tfp->tfp_mac_refcnt > 0);
	tfp->tfp_mac_refcnt--;
	if (tfp->tfp_mac_refcnt == 0) {
		if (tfp->tfp_runstate == TFPKT_RUNSTATE_STOPPING)
			tfp->tfp_runstate = TFPKT_RUNSTATE_STOPPED;
	}
	mutex_exit(&tfp->tfp_mutex);
}

/*
 * Attempt to send a single packet.  The call returns 0 if the the packet was
 * sent successfully or if there was a structural problem with this packet.  In
 * either case, the caller is free to attempt another tx attempt.  The routine
 * returns -1 if there was an operational error, suggesting that subsequent
 * transmission attempts will fail as well.
 */
static int
tfpkt_tx_one(tfpkt_t *tfp, mblk_t *mp_head)
{
	size_t full_sz = msgsize(mp_head);
	struct ether_header *eth;
	tfpkt_tbus_t *tbp = NULL;
	tfpkt_buf_t *tx_buf;
	caddr_t tx_wp;
	size_t *errstat = NULL;
	int rval;

	rval = 0;
	if (MBLKL(mp_head) < sizeof (struct ether_header)) {
		errstat = &tfp->tfp_stats.tps_tx_truncated_eth;
		goto done;
	}

	/* Drop packets without a sidecar header */
	eth = (struct ether_header *)mp_head->b_rptr;
	if (ntohs(eth->ether_type) != ETHERTYPE_SIDECAR) {
		errstat = &tfp->tfp_stats.tps_tx_missing_schdr;
		goto done;
	}

	rval = -1;
	if ((tbp = tfpkt_tbus_hold(tfp)) == NULL) {
		errstat = &tfp->tfp_stats.tps_tx_tbus_fails;
		goto done;
	}

	/*
	 * Allocate a tofino-DMAable buffer large enough to hold the full
	 * packet.
	 */
	if ((tx_buf = tfpkt_tbus_tx_alloc(tbp, full_sz)) == NULL) {
		errstat = &tfp->tfp_stats.tps_tx_alloc_fails;
		goto done;
	}

	/* Copy the packet into the tx buffer */
	tx_wp = tfpkt_buf_va(tx_buf);
	for (mblk_t *m = mp_head; m != NULL; m = m->b_cont) {
		size_t sz = MBLKL(m);
		if (sz > 0) {
			bcopy(m->b_rptr, tx_wp, sz);
			tx_wp += sz;
		}
	}

	if (tfpkt_tbus_tx(tbp, tx_buf, full_sz) != 0) {
		tfpkt_tbus_tx_free(tbp, tx_buf);
		errstat = &tfp->tfp_stats.tps_tx_tbus_fails;
		goto done;
	}
	rval = 0;
	freemsg(mp_head);

done:
	if (tbp != NULL)
		tfpkt_tbus_release(tfp);

	mutex_enter(&tfp->tfp_mutex);
	if (errstat == NULL) {
		tfp->tfp_stats.tps_tx_pkts++;
		tfp->tfp_stats.tps_tx_bytes += full_sz;
	} else {
		tfp->tfp_stats.tps_tx_errs++;
		(*errstat)++;
	}
	mutex_exit(&tfp->tfp_mutex);

	return (rval);
}

static mblk_t *
tfpkt_m_tx(void *arg, mblk_t *mp_chain)
{
	tfpkt_t *tfp = arg;
	mblk_t *mp, *next;

	/*
	 * If the link isn't running, free the buffers before returning.
	 */
	if (tfpkt_mac_hold(tfp) < 0) {
		mutex_enter(&tfp->tfp_mutex);
		tfp->tfp_stats.tps_tx_zombie++;
		mutex_exit(&tfp->tfp_mutex);

		freemsgchain(mp_chain);
		return (NULL);
	}

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;
		if (tfpkt_tx_one(tfp, mp) != 0) {
			/* relink to avoid losing packets later in the chain */
			mp->b_next = next;
			break;
		}
	}
	tfpkt_mac_release(tfp);

	return (mp);
}

static int
tfpkt_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	tfpkt_t *tfp = arg;
	int rval = 0;

	ASSERT(tfp->tfp_mh != NULL);

	switch (stat) {
	case MAC_STAT_LINK_STATE:
		*val = tfp->tfp_link_state;
		break;
	case MAC_STAT_OPACKETS:
		*val = tfp->tfp_stats.tps_tx_pkts;
		break;
	case MAC_STAT_OBYTES:
		*val = tfp->tfp_stats.tps_tx_bytes;
		break;
	case MAC_STAT_OERRORS:
		*val = tfp->tfp_stats.tps_tx_errs;
		break;
	case MAC_STAT_IPACKETS:
		*val = tfp->tfp_stats.tps_rx_pkts;
		break;
	case MAC_STAT_RBYTES:
		*val = tfp->tfp_stats.tps_rx_bytes;
		break;
	case MAC_STAT_IERRORS:
		*val = tfp->tfp_stats.tps_rx_errs;
		break;
	case ETHER_STAT_TOOSHORT_ERRORS:
		*val = tfp->tfp_stats.tps_rx_truncated_eth;
		break;
	default:
		rval = ENOTSUP;
		break;
	}

	return (rval);
}

static int
tfpkt_m_start(void *arg)
{
	tfpkt_t *tfp = arg;

	mutex_enter(&tfp->tfp_mutex);
	tfp->tfp_runstate = TFPKT_RUNSTATE_RUNNING;
	mutex_exit(&tfp->tfp_mutex);

	return (0);
}

static void
tfpkt_m_stop(void *arg)
{
	tfpkt_t *tfp = arg;

	mutex_enter(&tfp->tfp_mutex);

	if (tfp->tfp_mac_refcnt == 0)
		tfp->tfp_runstate = TFPKT_RUNSTATE_STOPPED;
	else
		tfp->tfp_runstate = TFPKT_RUNSTATE_STOPPING;

	mutex_exit(&tfp->tfp_mutex);
}

/* This is a no-op.  We return SUCCESS to allow snoop to work */
static int
tfpkt_m_promisc(void *arg, boolean_t on)
{
	return (0);
}

static int
tfpkt_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (ENOTSUP);
}

static int
tfpkt_m_unicst(void *arg, const uint8_t *macaddr)
{
	return (ENOTSUP);
}

void
tfpkt_rx(tfpkt_t *tfp, void *vaddr, size_t sz)
{
	caddr_t addr = (caddr_t)vaddr;
	mblk_t *mp = NULL;
	uint64_t *errstat = NULL;

	if (sz < ETHSZ) {
		errstat = &tfp->tfp_stats.tps_rx_truncated_eth;
		goto done;
	}

	if ((mp = allocb(sz, 0)) == NULL) {
		errstat = &tfp->tfp_stats.tps_rx_alloc_fails;
		goto done;
	}
	bcopy(addr, mp->b_rptr, sz);
	mp->b_wptr = mp->b_rptr + sz;

	if (tfpkt_mac_hold(tfp) != 0) {
		errstat = &tfp->tfp_stats.tps_rx_zombie;
		goto done;
	}
	mac_rx(tfp->tfp_mh, NULL, mp);
	tfpkt_mac_release(tfp);
	mp = NULL;

done:
	if (mp != NULL)
		freemsg(mp);

	mutex_enter(&tfp->tfp_mutex);
	if (errstat == NULL) {
		tfp->tfp_stats.tps_rx_pkts++;
		tfp->tfp_stats.tps_rx_bytes += sz;
	} else {
		tfp->tfp_stats.tps_rx_errs++;
		(*errstat)++;
	}
	mutex_exit(&tfp->tfp_mutex);
}

static int
tfpkt_init_mac(tfpkt_t *tfp)
{
	mac_register_t *mac;
	uint8_t mac_addr[ETHERADDRL] = {2, 0, 0, 0, 0, 0};
	int err;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (EINVAL);

	/* Register the new device with the mac(9e) framework */
	mac->m_driver = tfp;
	mac->m_dip = tfp->tfp_dip;
	mac->m_instance = 0;

	/*
	 * mac_register() requires that you give it something for a mac address,
	 * even for a passthrough device like this which isn't addressable and
	 * doesn't have (or need) a mac address.
	 */
	mac->m_src_addr = mac_addr;

	mac->m_callbacks = &tfpkt_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_max_sdu = ETHERMTU;
	mac->m_margin = VLAN_TAGSZ;
	err = mac_register(mac, &tfp->tfp_mh);
	mac_free(mac);

	if (err != 0)
		tfpkt_err(tfp, "!failed to register packet driver: %d", err);

	return (err);
}

static void
tfpkt_cleanup(tfpkt_t *tfp)
{
	ddi_set_driver_private(tfp->tfp_dip, NULL);
	if (tfp->tfp_init_state & TFPKT_INIT_TASKQ) {
		ASSERT(tfp->tfp_tbus_tq != NULL);
		taskq_wait(tfp->tfp_tbus_tq);
		taskq_destroy(tfp->tfp_tbus_tq);
	}

	if (tfp->tfp_init_state & TFPKT_INIT_MAC) {
		(void) mac_unregister(tfp->tfp_mh);
	}

	cv_destroy(&tfp->tfp_tbus_cv);
	mutex_destroy(&tfp->tfp_tbus_mutex);
	mutex_destroy(&tfp->tfp_mutex);
	kmem_free(tfp, sizeof (*tfp));
}

static int
tfpkt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	tfpkt_t *tfp;
	int err;

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	tfp = kmem_zalloc(sizeof (*tfp), KM_SLEEP);
	mutex_init(&tfp->tfp_mutex, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&tfp->tfp_tbus_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&tfp->tfp_tbus_cv,  NULL, CV_DEFAULT, NULL);
	tfp->tfp_runstate = TFPKT_RUNSTATE_STOPPED;

	tfp->tfp_dip = dip;
	ddi_set_driver_private(dip, tfp);

	if ((err = tfpkt_init_mac(tfp)) != 0) {
		tfpkt_err(tfp, "!failed to init mac: %d", err);
		goto fail;
	}
	tfp->tfp_init_state |= TFPKT_INIT_MAC;

	/*
	 * Create a taskq with a single thread for monitoring the tbus state.
	 */
	tfp->tfp_tbus_tq = taskq_create("tfpkt_tq", 1, minclsyspri, 1, 1,
	    TASKQ_PREPOPULATE);
	if (tfp->tfp_tbus_tq == NULL) {
		tfpkt_err(tfp, "!failed to create taskq");
		goto fail;
	}
	tfp->tfp_init_state |= TFPKT_INIT_TASKQ;
	taskq_dispatch_ent(tfp->tfp_tbus_tq, tfpkt_tbus_monitor, dip, 0,
	    &tfp->tfp_tbus_monitor);

	return (DDI_SUCCESS);

fail:
	tfpkt_cleanup(tfp);
	return (DDI_FAILURE);
}

static int
tfpkt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	tfpkt_t *tfp;

	switch (cmd) {
	case DDI_DETACH:
		tfp = (tfpkt_t *)ddi_get_driver_private(dip);

		ASSERT(tfp->tfp_mac_refcnt == 0);

		if (tfp->tfp_runstate == TFPKT_RUNSTATE_STOPPED &&
		    tfpkt_tbus_monitor_halt(tfp) == 0) {
			tfpkt_cleanup(tfp);
			return (DDI_SUCCESS);
		}

		mutex_enter(&tfp->tfp_mutex);
		tfp->tfp_stats.tps_detach_fails++;
		mutex_exit(&tfp->tfp_mutex);
		/* FALLTHRU */
	default:
		return (DDI_FAILURE);
	}
}

static struct modldrv tfpkt_modldrv = {
	.drv_modops =			&mod_driverops,
	.drv_linkinfo =			"Tofino Switch Packet Driver",
	.drv_dev_ops =			&tfpkt_dev_ops,
};

static struct modlinkage modlinkage = {
	.ml_rev =			MODREV_1,
	.ml_linkage =			{ &tfpkt_modldrv, NULL },
};

int
_init(void)
{
	mac_init_ops(&tfpkt_dev_ops, "tfpkt");

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int rval;

	rval = mod_remove(&modlinkage);
	if (rval == 0)
		mac_fini_ops(&tfpkt_dev_ops);
	return (rval);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
