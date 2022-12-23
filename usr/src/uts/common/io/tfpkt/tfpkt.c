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
#include "tfpkt.h"
#include "tfpkt_impl.h"

#define	ETHSZ (sizeof (struct ether_header))
#define	SCSZ (sizeof (struct schdr))

static tfpkt_t *tfpkt;
static dev_info_t *tfpkt_dip;

static int tfpkt_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int tfpkt_attach(dev_info_t *, ddi_attach_cmd_t);
static int tfpkt_detach(dev_info_t *, ddi_detach_cmd_t);
static int tfpkt_probe(dev_info_t *);

/* MAC callback function declarations */
static int tfpkt_m_start(void *);
static void tfpkt_m_stop(void *);
static int tfpkt_m_promisc(void *, boolean_t);
static int tfpkt_m_multicst(void *, boolean_t, const uint8_t *);
static int tfpkt_m_unicst(void *, const uint8_t *);
static int tfpkt_m_stat(void *, uint_t, uint64_t *);
static void tfpkt_m_ioctl(void *, queue_t *, mblk_t *);
static mblk_t *tfpkt_m_tx(void *, mblk_t *);

DDI_DEFINE_STREAM_OPS(tfpkt_dev_ops, nulldev, tfpkt_probe, tfpkt_attach,
    tfpkt_detach, nodev, tfpkt_getinfo, D_MP, NULL,
    ddi_quiesce_not_needed);

static mac_callbacks_t tfpkt_m_callbacks = {
	.mc_callbacks =			MC_IOCTL,
	.mc_getstat =			tfpkt_m_stat,
	.mc_start =			tfpkt_m_start,
	.mc_stop =			tfpkt_m_stop,
	.mc_setpromisc =		tfpkt_m_promisc,
	.mc_multicst =			tfpkt_m_multicst,
	.mc_unicst =			tfpkt_m_unicst,
	.mc_tx =			tfpkt_m_tx,
	.mc_ioctl =			tfpkt_m_ioctl,
};

static int
tfpkt_mac_hold(tfpkt_t *tfp)
{
	int rval;

	ASSERT(tfp != NULL);
	mutex_enter(&tfp->tfp_mutex);
	if (tfp->tfp_runstate == TFPKT_RUNSTATE_RUNNING) {
		tfp->tfp_mac_refcnt++;
		rval = 0;
	} else {
		rval = -1;
	}
	mutex_exit(&tfp->tfp_mutex);

	return (rval);
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

static int
tfpkt_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = tfpkt_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = NULL;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

static int
tfpkt_probe(dev_info_t *dip)
{
	return (DDI_PROBE_SUCCESS);
}

static int
tfpkt_init_mac(tfpkt_t *tfp)
{
	mac_register_t *mac;
	uint8_t mac_addr[ETHERADDRL] = {2, 4, 6, 8, 10, 12};
	int rval;

	mac = mac_alloc(MAC_VERSION);
	if (mac == NULL) {
		return (ENOMEM);
	}

	/* Register the new device with the mac(9e) framework */
	mac->m_driver = tfp;
	mac->m_dip = tfp->tfp_dip;
	mac->m_instance = tfp->tfp_instance;
	mac->m_src_addr = mac_addr;
	mac->m_callbacks = &tfpkt_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_max_sdu = ETHERMTU;
	mac->m_margin = VLAN_TAGSZ;
	rval = mac_register(mac, &tfp->tfp_mh);
	mac_free(mac);

	if (rval == 0) {
		mac_link_update(tfp->tfp_mh, LINK_STATE_UP);
		mac_tx_update(tfp->tfp_mh);
	} else {
		dev_err(tfp->tfp_dip, CE_WARN,
		    "failed to register packet driver");
	}

	return (rval);
}

static boolean_t
tfpkt_minor_create(dev_info_t *dip, int instance)
{
	minor_t m = (minor_t)instance;

	if (ddi_create_minor_node(dip, "tfpkt", S_IFCHR, m, DDI_PSEUDO,
	    0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create minor node %d",
		    instance);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static void
tfpkt_cleanup(dev_info_t *dip)
{
	tfpkt_t *tfp;

	tfp = (tfpkt_t *)ddi_get_driver_private(dip);
	if (tfp != NULL) {
		tfpkt_dip = NULL;
		tfp->tfp_dip = NULL;
		ddi_set_driver_private(dip, NULL);
		if (tfpkt->tfp_tbus_tq != NULL)
			taskq_destroy(tfpkt->tfp_tbus_tq);
	}

	ddi_remove_minor_node(dip, "tfpkt");
}

static int
tfpkt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	ASSERT(tfpkt != NULL);
	ASSERT3P(tfpkt_dip, ==, NULL);

	if (!tfpkt_minor_create(dip, instance))
		goto fail;

	tfpkt_dip = dip;
	tfpkt->tfp_dip = dip;
	ddi_set_driver_private(dip, tfpkt);

	if (tfpkt_init_mac(tfpkt) != 0) {
		dev_err(tfpkt->tfp_dip, CE_WARN, "failed to init mac");
		goto fail;
	}

	if ((tfpkt->tfp_tbus_tq = taskq_create("tfpkt_tq", 1, minclsyspri, 1, 1,
	    TASKQ_PREPOPULATE)) == NULL) {
		dev_err(tfpkt->tfp_dip, CE_WARN, "failed to create taskq");
		goto fail;
	}

	taskq_dispatch_ent(tfpkt->tfp_tbus_tq, tfpkt_tbus_monitor,
	    dip, 0, &tfpkt->tfp_tbus_monitor);

	return (DDI_SUCCESS);

fail:
	tfpkt_cleanup(dip);
	return (DDI_FAILURE);
}

static int
tfpkt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int r;
	tfpkt_t *tfp;

	switch (cmd) {
	case DDI_DETACH:
		tfp = (tfpkt_t *)ddi_get_driver_private(dip);
		if (tfp->tfp_runstate != TFPKT_RUNSTATE_STOPPED)
			return (DDI_FAILURE);

		ASSERT(tfp == tfpkt);
		ASSERT(tfp->tfp_mac_refcnt == 0);

		if (tfpkt_tbus_monitor_halt(tfp) != 0) {
			dev_err(dip, CE_NOTE, "tbus_monitor halt failed");
			return (DDI_FAILURE);
		}

		if ((r = mac_unregister(tfp->tfp_mh)) != 0) {
			dev_err(dip, CE_NOTE, "mac unregister failed %d", r);
			return (DDI_FAILURE);
		}

		tfpkt_cleanup(dip);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

static int
tfpkt_tx_one(tfpkt_t *tfp, mblk_t *mp_head)
{
	size_t full_sz = msgsize(mp_head);
	struct ether_header *eth = NULL;
	caddr_t tx_buf, tx_wp;
	tf_tbus_t *tbp;

	/* Drop packets without a sidecar header */
	eth = (struct ether_header *)mp_head->b_rptr;
	if (ntohs(eth->ether_type) != ETHERTYPE_SIDECAR) {
		freeb(mp_head);
		return (0);
	}

	if ((tbp = tfpkt_tbus_hold(tfp)) == NULL)
		return (-1);

	if ((tx_buf = tofino_tbus_tx_alloc(tbp, full_sz)) == NULL) {
		tfpkt_tbus_release(tfp);
		return (-1);
	}

	tx_wp = tx_buf;

	/* Copy the ethernet header into the transfer buffer */
	bcopy(mp_head->b_rptr, tx_wp, ETHSZ);
	tx_wp += ETHSZ;

	/*
	 * Copy the rest of the packet into the tx buffer, skipping over the
	 * ethernet header we've already copied.
	 */
	size_t skip = ETHSZ;
	for (mblk_t *m = mp_head; m != NULL; m = m->b_cont) {
		size_t sz = MBLKL(m) - skip;

		bcopy(m->b_rptr + skip, tx_wp, sz);
		tx_wp += sz;
		skip = 0;
	}

	if (tofino_tbus_tx(tbp, tx_buf, full_sz) != 0) {
		tofino_tbus_tx_free(tbp, tx_buf);
		tfpkt_tbus_release(tfp);
		return (-1);
	}

	tfpkt_tbus_release(tfp);
	freeb(mp_head);
	return (0);
}

static mblk_t *
tfpkt_m_tx(void *arg, mblk_t *mp_chain)
{
	tfpkt_t *tfp = arg;
	mblk_t *mp, *next;

	/*
	 * If the link isn't running, free the buffers before returning.
	 * XXX: should we even get here if the link isn't running, or will the
	 * mac layer catch that?
	 */
	if (tfpkt_mac_hold(tfp) < 0) {
		for (mp = mp_chain; mp != NULL; mp = next) {
			next = mp->b_next;
			freeb(mp);
		}
		return (NULL);
	}

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		if (tfpkt_tx_one(tfp, mp) != 0)
			break;
	}
	tfpkt_mac_release(tfp);

	/*
	 * XXX: if we have unsent buffers left, call mac_tx_update() when more
	 * tx_bufs become available
	 */
	return (mp);
}

static void
tfpkt_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	miocnak(q, mp, 0, ENOTSUP);
}

static int
tfpkt_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	tfpkt_t *tfp = arg;
	int rval = 0;

	ASSERT(tfp->tfp_mh != NULL);

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = 100 * 1000000ull; /* 100 Mbps */
		break;
	case MAC_STAT_LINK_STATE:
		*val = LINK_DUPLEX_FULL;
		break;
	case MAC_STAT_LINK_UP:
		mutex_enter(&tfp->tfp_tbus_mutex);
		if (tfp->tfp_tbus_state == TFPKT_TBUS_ACTIVE)
			*val = LINK_STATE_UP;
		else
			*val = LINK_STATE_DOWN;
		mutex_exit(&tfp->tfp_tbus_mutex);
		break;
	case MAC_STAT_PROMISC:
	case MAC_STAT_MULTIRCV:
	case MAC_STAT_MULTIXMT:
	case MAC_STAT_BRDCSTRCV:
	case MAC_STAT_BRDCSTXMT:
		rval = ENOTSUP;
		break;
	case MAC_STAT_OPACKETS:
		*val = tfp->tfp_stats.tfs_xmit_count;
		break;
	case MAC_STAT_OBYTES:
		*val = tfp->tfp_stats.tfs_obytes;
		break;
	case MAC_STAT_IERRORS:
		*val = tfp->tfp_stats.tfs_recv_errors;
		break;
	case MAC_STAT_OERRORS:
		*val = tfp->tfp_stats.tfs_xmit_errors;
		break;
	case MAC_STAT_RBYTES:
		*val = tfp->tfp_stats.tfs_rbytes;
		break;
	case MAC_STAT_IPACKETS:
		*val = tfp->tfp_stats.tfs_recv_count;
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

static int
tfpkt_m_promisc(void *arg, boolean_t on)
{
	tfpkt_t *tfp = arg;

	tfp->tfp_promisc = on;
	return (0);
}

static int
tfpkt_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (0);
}

static int
tfpkt_m_unicst(void *arg, const uint8_t *macaddr)
{
	return (0);
}

void
tfpkt_rx(tfpkt_t *tfp, void *vaddr, size_t mblk_sz)
{
	caddr_t addr = (caddr_t)vaddr;
	mblk_t *mp;
	tf_tbus_t *tbp;

	if (mblk_sz < ETHSZ) {
		goto done;
	}

	if ((mp = allocb(mblk_sz, 0)) == NULL)
		goto done;

	bcopy(addr, mp->b_rptr, mblk_sz);
	mp->b_wptr = mp->b_rptr + mblk_sz;

	if (tfpkt_mac_hold(tfp) == 0) {
		mac_rx(tfp->tfp_mh, NULL, mp);
		tfpkt_mac_release(tfp);
	} else {
		freeb(mp);
	}

done:
	if ((tbp = tfpkt_tbus_hold(tfp)) != NULL) {
		tofino_tbus_rx_done(tbp, addr, mblk_sz);
		tfpkt_tbus_release(tfp);
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

static tfpkt_t *
tfpkt_dev_alloc()
{
	tfpkt_t *tfp;

	tfp = kmem_zalloc(sizeof (*tfp), KM_NOSLEEP);
	if (tfp != NULL) {
		mutex_init(&tfp->tfp_mutex, NULL, MUTEX_DRIVER, NULL);
		mutex_init(&tfp->tfp_tbus_mutex, NULL, MUTEX_DRIVER, NULL);
		cv_init(&tfp->tfp_tbus_cv,  NULL, CV_DEFAULT, NULL);
	}
	return (tfp);
}

static void
tfpkt_dev_free(tfpkt_t *tfp)
{
	cv_destroy(&tfp->tfp_tbus_cv);
	mutex_destroy(&tfp->tfp_tbus_mutex);
	mutex_destroy(&tfp->tfp_mutex);
	kmem_free(tfp, sizeof (*tfp));
}

int
_init(void)
{
	tfpkt_t *tfp;
	int status;

	if ((tfp = tfpkt_dev_alloc()) == NULL) {
		cmn_err(CE_WARN, "failed to alloc tfpkt struct");
		return (ENOMEM);
	}

	mac_init_ops(&tfpkt_dev_ops, "tfpkt");
	if ((status = mod_install(&modlinkage)) == 0) {
		tfpkt = tfp;
		cmn_err(CE_WARN, "loaded tfpkt, built at %s", __TIMESTAMP__);
	} else {
		cmn_err(CE_WARN, "failed to install tfpkt");
		mac_fini_ops(&tfpkt_dev_ops);
		tfpkt_dev_free(tfp);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	if ((status = mod_remove(&modlinkage)) == 0) {
		mac_fini_ops(&tfpkt_dev_ops);
	}

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
