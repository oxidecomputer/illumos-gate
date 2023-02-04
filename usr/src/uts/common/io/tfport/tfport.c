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
 * A tfport device is a simple packet (de)multiplexer, acting as both a mac
 * client and a mac device.
 *
 * A tfport device is layered over a single existing mac device, which sits on
 * the notional bottom side of the demux.  At the direction of dladm(8) the
 * tfport device will create additional links, which collectively sit on the top
 * of the demux.  Each upper link has a unique port number associated with it
 * when it is created.
 *
 *      +-------+  +-------+  +--------+
 *      |tfport0|  |tfport4|  |tfport55|
 *      +-------+  +-------+  +--------+
 *          |          |           |
 *          +----------+-----------+
 *                     |
 *               +-----+-----+
 *               |  tfport   |
 *               +-----+-----+
 *                     |
 *             +-------+--------+
 *             | tfpkt, vioif,  |
 *             |   igb0, etc.   |
 *             +-------+--------+
 *
 * As ethernet packets arrive from the bottom mac, they are forwarded out one of
 * the upper links.  If the ethernet packet contains a sidecar header, the demux
 * will use the port number embedded in the header to decide which of the links
 * the pcket should be forwarded to.  Before forwarding, the sidecar header is
 * removed so the upstream link will receive a normal IP, ARP, etc. packet.  An
 * incoming packet with no sidecar header will be forwarded to the link
 * associated with port 0 (if it exists).
 *
 * As ethernet packets arrive on the upper links, they are all forwarded out the
 * bottom mac.  Before forwarding, a sidecar header is inserted immediately
 * after the ethernet header, with the port number of the upper link embedded.
 */

#include <sys/policy.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/dls.h>
#include <sys/dlpi.h>
#include <sys/dld_ioc.h>
#include <sys/mac_provider.h>
#include <sys/mac_client.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_ether.h>
#include <inet/ip.h>
#include <inet/ip2mac.h>
#include <sys/vlan.h>
#include <sys/list.h>
#include <sys/mac_impl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/strsun.h>
#include <sys/strsubr.h>
#include <sys/sysmacros.h>
#include <sys/tfport.h>
#include <sys/tofino.h>
#include "tfport_impl.h"

#define	ETHSZ (sizeof (struct ether_header))
#define	SCSZ (sizeof (schdr_t))

/*
 * Ensure that the header size stays a multiple of 4 to avoid alignment problems
 * with IP headers.
 */
CTASSERT(sizeof (schdr_t) == 24);

static tfport_t *tfport;
static dev_info_t *tfport_dip;

static int tfport_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int tfport_attach(dev_info_t *, ddi_attach_cmd_t);
static int tfport_detach(dev_info_t *, ddi_detach_cmd_t);

/* MAC callback function declarations */
static int tfport_m_start(void *);
static void tfport_m_stop(void *);
static int tfport_m_promisc(void *, boolean_t);
static int tfport_m_multicst(void *, boolean_t, const uint8_t *);
static int tfport_m_unicst(void *, const uint8_t *);
static int tfport_m_stat(void *, uint_t, uint64_t *);
static void tfport_m_ioctl(void *, queue_t *, mblk_t *);
static mblk_t *tfport_m_tx(void *, mblk_t *);

static mac_callbacks_t tfport_m_callbacks = {
	.mc_callbacks =		MC_IOCTL,
	.mc_getstat =		tfport_m_stat,
	.mc_start =		tfport_m_start,
	.mc_stop =		tfport_m_stop,
	.mc_setpromisc =	tfport_m_promisc,
	.mc_multicst =		tfport_m_multicst,
	.mc_unicst =		tfport_m_unicst,
	.mc_tx =		tfport_m_tx,
	.mc_ioctl =		tfport_m_ioctl,
};

static int tfport_ioc_create(void *, intptr_t, int, cred_t *, int *);
static int tfport_ioc_delete(void *, intptr_t, int, cred_t *, int *);
static int tfport_ioc_info(void *, intptr_t, int, cred_t *, int *);
static void tfport_ioc_l2_needed(tfport_port_t *, struct iocblk *, queue_t *,
    mblk_t *);
static tfport_source_t *tfport_hold_source(tfport_t *devp,
    datalink_id_t src_id);
static int tfport_rele_source(tfport_t *devp, tfport_source_t *src);

static dld_ioc_info_t tfport_ioc_list[] = {
	{TFPORT_IOC_CREATE, DLDCOPYINOUT, sizeof (tfport_ioc_create_t),
	    tfport_ioc_create, secpolicy_dl_config},
	{TFPORT_IOC_DELETE, DLDCOPYIN, sizeof (tfport_ioc_delete_t),
	    tfport_ioc_delete, secpolicy_dl_config},
	{TFPORT_IOC_INFO, DLDCOPYINOUT, sizeof (tfport_ioc_info_t),
	    tfport_ioc_info, NULL},
};

/*
 * By default we drop packets without a sidecar header or a matching tfport
 * device.  For debugging, these flags will can be used to send them to tfport0
 * instead.
 */
#define	TFPORT_PORT0_NONSIDECAR	0x01
#define	TFPORT_PORT0_NONCLAIMED	0x02
int tfport_port0 = 0;

int tfport_debug = 0;

static void
tfport_dlog(tfport_t *t, const char *fmt, ...)
{
	va_list args;

	if (tfport_debug) {
		va_start(args, fmt);
		vdev_err(t->tfp_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

static void
tfport_err(tfport_t *t, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(t->tfp_dip, CE_WARN, fmt, args);
	va_end(args);
}

#define	CMP(a, b) ((a) < (b) ? -1 : (a) > (b) ? 1 : 0)
/*
 * Nodes in the port/source-indexed are sorted by port first, then by the link
 * id of the packet source.
 */
static int
tfport_port_cmp(const void *a, const void *b)
{
	const tfport_port_t *ta = (tfport_port_t *)a;
	const tfport_port_t *tb = (tfport_port_t *)b;
	int c;

	if ((c = CMP(ta->tp_port, tb->tp_port)) == 0)
		c = CMP(ta->tp_src_id, tb->tp_src_id);
	return (c);
}

static int
tfport_link_cmp(const void *a, const void *b)
{
	const tfport_port_t *ta = (tfport_port_t *)a;
	const tfport_port_t *tb = (tfport_port_t *)b;
	return (CMP(ta->tp_link_id, tb->tp_link_id));
}

static void
tfport_random_mac(uint8_t mac[ETHERADDRL])
{
	(void) random_get_pseudo_bytes(mac, ETHERADDRL);
	/* Ensure MAC address is not multicast and is local */
	mac[0] = (mac[0] & ~1) | 2;
}

/*
 * Return the device associated with this link.  Because we do not take a
 * reference on the port before returning it, the pointer is only valid until
 * the tfp_mutex is released.
 */
static tfport_port_t *
tfport_find_link(tfport_t *devp, datalink_id_t link)
{
	tfport_port_t find;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	find.tp_link_id = link;
	return (avl_find(&devp->tfp_ports_by_link, &find, NULL));
}

/*
 * Return the active device associated with this port, after taking a reference
 * on it.
 */
static tfport_port_t *
tfport_find_port(tfport_t *devp, tfport_source_t *srcp, int port)
{
	tfport_port_t find, *portp;

	mutex_enter(&devp->tfp_mutex);

	find.tp_port = port;
	find.tp_src_id = srcp->tps_id;
	portp = avl_find(&devp->tfp_ports_by_port, &find, NULL);

	if (portp == NULL || portp->tp_run_state != TFPORT_RUNSTATE_RUNNING) {
		devp->tfp_stats.tfs_unclaimed_pkts++;

		if (tfport_port0 & TFPORT_PORT0_NONCLAIMED) {
			find.tp_port = 0;
			portp = avl_find(&devp->tfp_ports_by_port, &find, NULL);
		}
	}

	if (portp != NULL) {
		mutex_enter(&portp->tp_mutex);
		if (portp->tp_run_state == TFPORT_RUNSTATE_RUNNING) {
			portp->tp_refcnt++;
			mutex_exit(&portp->tp_mutex);
		} else {
			devp->tfp_stats.tfs_zombie_pkts++;
			mutex_exit(&portp->tp_mutex);
			portp = NULL;
		}
	}

	mutex_exit(&devp->tfp_mutex);

	return (portp);
}

/*
 * Drop a reference on the port.  If the reference count goes to 0 and the port
 * is in the STOPPING state, transition to STOPPED.
 */
static void
tfport_rele_port(tfport_t *devp, tfport_port_t *portp)
{
	if (portp == NULL)
		return;

	mutex_enter(&portp->tp_mutex);
	ASSERT(portp->tp_refcnt > 0);
	portp->tp_refcnt--;
	if (portp->tp_refcnt == 0 &&
	    portp->tp_run_state == TFPORT_RUNSTATE_STOPPING) {
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	}
	mutex_exit(&portp->tp_mutex);
}

/*
 * This copies at most "bytes" bytes from the rptr in the src buffer to the wptr
 * in the dst buffer.  Because we don't own the src buffer, we use an external
 * offset into the src rather than modifying the rptr in the mblk_t itself.
 *
 * The wptr in the dst buffer is updated accordingly.  The number of bytes
 * copied is returned.  This routine assumes that the destination buffer was
 * allocated with enough space to receive all the bytes we throw at it.
 */
static size_t
copy_mb_data(mblk_t *src, size_t *offset, mblk_t *dst, size_t bytes)
{
	unsigned char *rptr = src->b_rptr + *offset;
	size_t sz = MIN(bytes, src->b_wptr - rptr);
	bcopy(rptr, dst->b_wptr, sz);
	dst->b_wptr += sz;
	*offset += sz;

	return (sz);
}

static void
tfport_tx_one(tfport_source_t *srcp, tfport_port_t *portp, mblk_t *mp_head)
{
	tfport_t *devp = srcp->tps_tfport;
	schdr_t sc;
	mblk_t *orig_buf, *tx_buf;
	size_t offset, sz, resid, full_sz;

	/*
	 * The tfport on which the packet arrives determines which tofino port
	 * the packet will egress.  We don't allow packets to loopback on port
	 * 0, so we drop them here.
	 */
	if (portp->tp_port == 0) {
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_loopback_pkts++;
		mutex_exit(&devp->tfp_mutex);
		goto done;
	}

	/*
	 * Allocate a buffer large enough for the full packet along with an
	 * additional sidecar header.
	 */
	full_sz = msgsize(mp_head) + SCSZ;
	if ((tx_buf = allocb(full_sz, BPRI_HI)) == NULL) {
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_tx_nomem_drops++;
		mutex_exit(&devp->tfp_mutex);
		goto done;
	}

	/* Copy the ethernet header into the transfer buffer */
	struct ether_header *eth = (struct ether_header *)(tx_buf->b_wptr);
	orig_buf = mp_head;
	offset = 0;
	resid = ETHSZ;
	while (resid > 0) {
		sz = copy_mb_data(orig_buf, &offset, tx_buf, resid);
		if ((resid -= sz) > 0) {
			orig_buf = orig_buf->b_cont;
			offset = 0;
		}
	}

	/* construct the sidecar header and update the ethernet header */
	bzero(&sc, sizeof (sc));
	sc.sc_code = SC_FORWARD_FROM_USERSPACE;
	sc.sc_ingress = 0;
	sc.sc_egress = htons(portp->tp_port);
	sc.sc_ethertype = eth->ether_type;
	bcopy((void *)&sc, tx_buf->b_wptr, sizeof (sc));
	tx_buf->b_wptr += SCSZ;
	eth->ether_type = htons(ETHERTYPE_SIDECAR);

	/*
	 * Copy the rest of the packet into the tx buffer, skipping
	 * over the headers we've already copied.
	 */
	resid = full_sz - SCSZ - ETHSZ;
	while (resid > 0) {
		sz = copy_mb_data(orig_buf, &offset, tx_buf, resid);
		resid -= sz;
		orig_buf = orig_buf->b_cont;
		offset = 0;
	}

	mutex_enter(&portp->tp_mutex);
	portp->tp_stats.tfs_tx_pkts++;
	portp->tp_stats.tfs_tx_bytes += full_sz;
	mutex_exit(&portp->tp_mutex);

	(void) mac_tx(srcp->tps_mch, tx_buf, 0, MAC_DROP_ON_NO_DESC, NULL);

done:
	/*
	 * The lower level is responsible for the freeing transmit mblk.  It is
	 * our responsibility to free the original mblk.
	 */
	freemsg(mp_head);
}

static mblk_t *
tfport_m_tx(void *arg, mblk_t *mp_chain)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = portp->tp_tfport;
	tfport_source_t *srcp;
	mblk_t *mp, *next;

	mutex_enter(&devp->tfp_mutex);
	ASSERT(portp == tfport_find_link(devp, portp->tp_link_id));

	srcp = tfport_hold_source(devp, portp->tp_src_id);
	ASSERT(srcp != NULL);
	mutex_exit(&devp->tfp_mutex);

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;
		tfport_tx_one(srcp, portp, mp);
	}

	mutex_enter(&devp->tfp_mutex);
	(void) tfport_rele_source(devp, srcp);
	mutex_exit(&devp->tfp_mutex);

	return (NULL);
}

/*
 * We support two different types of notification: link state changes and tx
 * updates.  In each case, we iterate over all of the tfport devices layered
 * over this source, propagating the notification upwards.
 */
static void
tfport_pkt_notify_cb(void *arg, mac_notify_type_t type)
{
	tfport_source_t *srcp = arg;
	tfport_t *devp = srcp->tps_tfport;
	tfport_port_t *portp;
	link_state_t ls;
	mac_handle_t *mhp;
	size_t total, updates;

	if (type != MAC_NOTE_LINK)
		return;

	mutex_enter(&devp->tfp_mutex);
	VERIFY3P(srcp, ==, tfport_hold_source(devp, srcp->tps_id));
	mutex_exit(&devp->tfp_mutex);

	ls = mac_client_stat_get(srcp->tps_mch, MAC_STAT_LINK_STATE);

	mutex_enter(&devp->tfp_mutex);
	total = avl_numnodes(&devp->tfp_ports_by_port);
	updates = 0;
	mhp = kmem_zalloc(sizeof (*mhp) * total, KM_SLEEP);

	for (portp = avl_first(&devp->tfp_ports_by_port);
	    portp != NULL;
	    portp = avl_walk(&devp->tfp_ports_by_port, portp, AVL_AFTER)) {
		mutex_enter(&portp->tp_mutex);
		if (portp->tp_link_state != ls) {
			portp->tp_link_state = ls;
			mhp[updates++] = portp->tp_mh;
		}
		mutex_exit(&portp->tp_mutex);
	}

	(void) tfport_rele_source(devp, srcp);
	mutex_exit(&devp->tfp_mutex);

	for (size_t i = 0; i < updates; i++)
		mac_link_update(mhp[i], ls);

	kmem_free(mhp, sizeof (*mhp) * total);
}

static int
mac_sidecar_header_info(tfport_t *devp, mac_handle_t mh, mblk_t *mp,
    tfport_header_info_t *thip)
{
	mac_header_info_t mhi;
	schdr_t sc;

	if (mac_header_info(mh, mp, &mhi) != 0) {
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_truncated_eth++;
		mutex_exit(&devp->tfp_mutex);
		return (-1);
	}

	thip->thi_eth_type = mhi.mhi_origsap;
	if (thip->thi_eth_type == ETHERTYPE_SIDECAR) {
		size_t hdr_size = ETHSZ + SCSZ;
		mblk_t *tmp = NULL;

		if (MBLKL(mp) < hdr_size)  {
			tmp = msgpullup(mp, -1);
			if (tmp == NULL || MBLKL(tmp) < hdr_size) {
				mutex_enter(&devp->tfp_mutex);
				devp->tfp_stats.tfs_truncated_eth++;
				mutex_exit(&devp->tfp_mutex);
				if (tmp != NULL)
					freemsg(tmp);
				return (-1);
			}
			mp = tmp;
		}
		bcopy(mp->b_rptr + ETHSZ, (void *) &sc, sizeof (sc));
		thip->thi_sc_eth_type = ntohs(sc.sc_ethertype);
		thip->thi_sc_code = sc.sc_code;
		thip->thi_sc_port = ntohs(sc.sc_ingress);
		freemsg(tmp);
	}

	return (0);
}

static void
tfport_rx_one(tfport_source_t *srcp, mac_resource_handle_t mrh, mblk_t *mp)
{
	tfport_t *devp = srcp->tps_tfport;
	tfport_port_t *portp = NULL;
	tfport_header_info_t hdr_info;
	uint32_t port = 0;

	if (mac_sidecar_header_info(devp, srcp->tps_mh, mp, &hdr_info) != 0) {
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_truncated_eth++;
		mutex_exit(&devp->tfp_mutex);
		goto done;
	}

	if (hdr_info.thi_eth_type == ETHERTYPE_SIDECAR) {
		if (hdr_info.thi_sc_code == SC_FORWARD_TO_USERSPACE)
			port = hdr_info.thi_sc_port;

	} else if ((tfport_port0 & TFPORT_PORT0_NONSIDECAR) == 0) {
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_non_sidecar++;
		mutex_exit(&devp->tfp_mutex);
		goto done;
	}

	if ((portp = tfport_find_port(devp, srcp, port)) == NULL)
		goto done;

	/*
	 * If the packet is going to a port device, we strip out the sidecar
	 * header.  This requires:
	 *    - copying the ethertype from the sidecar header to the ethernet
	 *	header
	 *    - moving the body of the packet to replace the sidecar header
	 *    - adjusting the size of the buffer to reflect the removed header
	 */
	if (portp->tp_port != 0) {
		struct ether_header *eth;
		size_t blk_size, hdr_size, body_size;

		/*
		 * If we don't have both headers in the first mblk, we need to
		 * do a pullup().
		 */
		blk_size = MBLKL(mp);
		hdr_size = ETHSZ + SCSZ;
		if (blk_size < hdr_size) {
			struct ether_header;
			mblk_t *tmp = NULL;

			if ((tmp = msgpullup(mp, -1)) == NULL) {
				mutex_enter(&devp->tfp_mutex);
				devp->tfp_stats.tfs_rx_nomem_drops++;
				mutex_exit(&devp->tfp_mutex);
				goto done;
			}
			freemsg(mp);
			mp = tmp;
			blk_size = MBLKL(mp);
		}

		eth = (struct ether_header *)mp->b_rptr;
		eth->ether_type = htons(hdr_info.thi_sc_eth_type);

		body_size = blk_size - hdr_size;
		if (body_size > 0)
			bcopy(mp->b_rptr + hdr_size, mp->b_rptr + ETHSZ,
			    body_size);
		mp->b_wptr = mp->b_rptr + (ETHSZ + body_size);
	}

	mutex_enter(&portp->tp_mutex);
	portp->tp_stats.tfs_rx_pkts++;
	portp->tp_stats.tfs_rx_bytes += msgsize(mp);
	mutex_exit(&portp->tp_mutex);

	mac_rx(portp->tp_mh, NULL, mp);
	tfport_rele_port(devp, portp);
	return;

done:
	if (portp != NULL)
		tfport_rele_port(devp, portp);

	freemsg(mp);
}

static void
tfport_rx(void *arg, mac_resource_handle_t mrh, mblk_t *mp_chain, boolean_t lb)
{
	tfport_source_t *srcp = arg;
	mblk_t *mp, *next;

	if (lb) {
		tfport_t *devp = srcp->tps_tfport;
		mutex_enter(&devp->tfp_mutex);
		devp->tfp_stats.tfs_mac_loopback++;
		mutex_exit(&devp->tfp_mutex);
		freemsgchain(mp_chain);
		return;
	}

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;

		tfport_rx_one(srcp, mrh, mp);
	}
}

static int
tfport_mac_init(tfport_t *devp, tfport_port_t *portp)
{
	mac_register_t *mac;
	int err;

	if ((mac = mac_alloc(MAC_VERSION)) == NULL)
		return (EINVAL);

	/* Register the new device with the mac(9e) framework */
	mac->m_driver = portp;
	mac->m_dip = devp->tfp_dip;
	/* let mac layer assign a unique instance */
	mac->m_instance = -1;
	mac->m_src_addr = portp->tp_mac_addr;
	mac->m_callbacks = &tfport_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_max_sdu = ETHERMTU;
	mac->m_margin = SCSZ;
	err = mac_register(mac, &portp->tp_mh);
	mac_free(mac);

	if (err == 0) {
		portp->tp_init_state |= TFPORT_INIT_MAC_REGISTER;
		mac_link_update(portp->tp_mh, LINK_STATE_UP);
	} else {
		tfport_dlog(devp, "!failed to register port %d: %d",
		    portp->tp_port, err);
	}

	return (err);
}

static tfport_source_t *
tfport_find_source(tfport_t *devp, datalink_id_t src_id)
{
	tfport_source_t *srcp;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	for (srcp = list_head(&devp->tfp_sources);
	    (srcp != NULL) && (srcp->tps_id != src_id);
	    srcp = list_next(&devp->tfp_sources, srcp))
		;

	return (srcp);
}

static tfport_source_t *
tfport_hold_source(tfport_t *devp, datalink_id_t src_id)
{
	tfport_source_t *srcp;

	if ((srcp = tfport_find_source(devp, src_id)) != NULL)
		srcp->tps_refcnt++;

	return (srcp);
}

static int
tfport_rele_source(tfport_t *devp, tfport_source_t *srcp)
{
	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	ASSERT(srcp->tps_refcnt > 0);

	return (--srcp->tps_refcnt);
}

static int
tfport_close_source(tfport_t *devp, datalink_id_t src_id)
{
	tfport_source_t *srcp;
	int err;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));

	if ((srcp = tfport_find_source(devp, src_id)) == NULL)
		return (ENOENT);

	if (tfport_rele_source(devp, srcp) != 0)
		return (0);

	list_remove(&devp->tfp_sources, srcp);
	if (srcp->tps_init_state & TFPORT_SOURCE_RX_SET)
		mac_rx_clear(srcp->tps_mch);

	if (srcp->tps_init_state & TFPORT_SOURCE_UNICAST_ADD &&
	    (err = mac_unicast_remove(srcp->tps_mch, srcp->tps_muh) != 0))
		tfport_err(devp, "!mac_unicast_remove() failed: %d", err);

	if (srcp->tps_init_state & TFPORT_SOURCE_NOTIFY_ADD &&
	    ((err = mac_notify_remove(srcp->tps_mnh, B_FALSE)) != 0))
		tfport_err(devp, "!mac_notify_remove() failed: %d", err);

	if (srcp->tps_init_state & TFPORT_SOURCE_CLIENT_OPEN)
		mac_client_close(srcp->tps_mch, 0);

	if (srcp->tps_init_state & TFPORT_SOURCE_OPEN)
		mac_close(srcp->tps_mh);

	mutex_destroy(&srcp->tps_mutex);
	kmem_free(srcp, sizeof (*srcp));
	return (0);
}

static int
tfport_open_source(tfport_t *devp, uint8_t mac_buf[ETHERADDRL],
    datalink_id_t src_id, tfport_source_t **srcpp)
{
	tfport_source_t *srcp;
	const mac_info_t *minfop;
	char name[32];
	int err;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));

	if ((srcp = tfport_hold_source(devp, src_id)) != NULL) {
		*srcpp = srcp;
		return (0);
	}

	tfport_dlog(devp, "!opening source link %d", src_id);

	srcp = kmem_zalloc(sizeof (*srcp), KM_SLEEP);
	list_insert_head(&devp->tfp_sources, srcp);
	mutex_init(&srcp->tps_mutex, NULL, MUTEX_DRIVER, NULL);
	srcp->tps_refcnt = 1;
	srcp->tps_tfport = devp;
	srcp->tps_id = src_id;

	if ((err = mac_open_by_linkid(src_id, &srcp->tps_mh)) != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_OPEN;

	(void) snprintf(name, sizeof (name), "tfport%d", src_id);
	err = mac_client_open(srcp->tps_mh, &srcp->tps_mch, name, 0);
	if (err != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_CLIENT_OPEN;

	mac_diag_t mac_diag = MAC_DIAG_NONE;
	err = mac_unicast_add(srcp->tps_mch, mac_buf, 0, &srcp->tps_muh, 0,
	    &mac_diag);
	if (err != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_UNICAST_ADD;

	minfop = mac_info(srcp->tps_mh);
	if (minfop->mi_media != DL_ETHER ||
	    minfop->mi_nativemedia != DL_ETHER) {
		err = ENOTSUP;
		goto out;
	}
	srcp->tps_mnh = mac_notify_add(srcp->tps_mh, tfport_pkt_notify_cb,
	    srcp);
	srcp->tps_init_state |= TFPORT_SOURCE_NOTIFY_ADD;

	mac_rx_set(srcp->tps_mch, tfport_rx, srcp);
	srcp->tps_init_state |= TFPORT_SOURCE_RX_SET;
out:
	if (err == 0) {
		*srcpp = srcp;
	} else {
		(void) tfport_close_source(devp, src_id);
	}

	return (err);
}

/*
 * If the provided port doesn't exist in either the link-indexed or port-indexed
 * trees, insert into both and return 0.  If the port is in either tree, return
 * -1.
 */
static int
tfport_port_index(tfport_t *devp, tfport_port_t *portp)
{
	avl_index_t port_where;
	avl_index_t link_where;

	tfport_dlog(devp, "!indexing (%d, %d)", portp->tp_port,
	    portp->tp_src_id);

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));

	/* Check both trees for collisions and for the insert location */
	if (avl_find(&devp->tfp_ports_by_port, portp, &port_where) != NULL) {
		tfport_dlog(devp, "!collision in port tree");
		return (-1);
	}
	if (avl_find(&devp->tfp_ports_by_link, portp, &link_where) != NULL) {
		tfport_dlog(devp, "!collision in link tree");
		return (-1);
	}
	avl_insert(&devp->tfp_ports_by_port, portp, port_where);
	avl_insert(&devp->tfp_ports_by_link, portp, link_where);
	portp->tp_init_state |= TFPORT_INIT_INDEXED;

	return (0);
}

/*
 * Remove the provided port from both avl trees.
 */
static void
tfport_port_deindex(tfport_t *devp, tfport_port_t *portp)
{
	tfport_dlog(devp, "!removing (%d, %d)", portp->tp_port,
	    portp->tp_src_id);

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	ASSERT(avl_find(&devp->tfp_ports_by_port, portp, NULL) != NULL);
	ASSERT(avl_find(&devp->tfp_ports_by_link, portp, NULL) != NULL);

	avl_remove(&devp->tfp_ports_by_link, portp);
	avl_remove(&devp->tfp_ports_by_port, portp);
}

static int
tfport_port_fini(tfport_t *devp, tfport_port_t *portp)
{
	datalink_id_t tmpid;
	int err;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	ASSERT(MUTEX_HELD(&portp->tp_mutex));

	if (portp->tp_init_state & TFPORT_INIT_DEVNET) {
		portp->tp_run_state = TFPORT_RUNSTATE_DLS;

		mutex_exit(&portp->tp_mutex);
		mutex_exit(&devp->tfp_mutex);
		err = dls_devnet_destroy(portp->tp_mh, &tmpid, B_TRUE);
		mutex_enter(&devp->tfp_mutex);
		mutex_enter(&portp->tp_mutex);

		portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
		if (err != 0)  {
			tfport_err(devp,
			    "!failed to clean up devnet link: %d: %d",
			    portp->tp_link_id, err);
			return (err);
		}
		portp->tp_init_state &= ~TFPORT_INIT_DEVNET;
	}

	if ((portp->tp_init_state & TFPORT_INIT_MAC_REGISTER) &&
	    (err = mac_unregister(portp->tp_mh)) != 0) {
		tfport_err(devp, "!failed to unregister mac for link %d: %d",
		    portp->tp_link_id, err);
		mutex_exit(&portp->tp_mutex);
		return (err);
	}

	if (portp->tp_init_state & TFPORT_INIT_SOURCE_OPENED)
		VERIFY3U(tfport_close_source(devp, portp->tp_src_id), ==, 0);

	if (portp->tp_init_state & TFPORT_INIT_INDEXED)
		tfport_port_deindex(devp, portp);

	mutex_exit(&portp->tp_mutex);
	mutex_destroy(&portp->tp_mutex);
	kmem_free(portp, sizeof (*portp));
	return (0);
}

static tfport_port_t *
tfport_port_new(tfport_t *devp, tfport_source_t *srcp,
    tfport_ioc_create_t *carg)
{
	tfport_port_t *portp;
	uchar_t mac_buf[ETHERADDRL];
	uchar_t *mac_addr;

	if (carg->tic_mac_len == 0) {
		tfport_random_mac(mac_buf);
		mac_addr = mac_buf;
		carg->tic_mac_len = ETHERADDRL;
		bcopy(mac_addr, carg->tic_mac_addr, ETHERADDRL);
	} else if (carg->tic_mac_len == ETHERADDRL) {
		mac_addr = carg->tic_mac_addr;
	} else {
		return (NULL);
	}

	portp = kmem_zalloc(sizeof (*portp), KM_SLEEP);
	mutex_init(&portp->tp_mutex, NULL, MUTEX_DRIVER, NULL);
	portp->tp_refcnt = 0;
	portp->tp_tfport = devp;
	portp->tp_run_state = TFPORT_RUNSTATE_INIT;
	portp->tp_port = carg->tic_port_id;
	portp->tp_link_id = carg->tic_link_id;
	bcopy(mac_addr, portp->tp_mac_addr, ETHERADDRL);
	portp->tp_mac_len = ETHERADDRL;
	portp->tp_src_id = carg->tic_pkt_id;
	portp->tp_init_state |= TFPORT_INIT_SOURCE_OPENED;

	return (portp);
}

static int
tfport_ioc_create(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	uint8_t mac_buf[ETHERADDRL];
	tfport_ioc_create_t *carg = karg;
	tfport_t *devp = tfport;
	datalink_id_t src_link;
	tfport_source_t *src;
	tfport_port_t *portp = NULL;
	int err;

	mutex_enter(&devp->tfp_mutex);
	src_link = carg->tic_pkt_id;
	mac_buf[0] = 2;  /* mark as locally administered */
	mac_buf[1] = (carg->tic_link_id >> 24) & 0xff;
	mac_buf[2] = (carg->tic_link_id >> 16) & 0xff;
	mac_buf[3] = (carg->tic_link_id >> 8) & 0xff;
	mac_buf[4] = carg->tic_link_id & 0xff;

	if ((err = tfport_open_source(devp, mac_buf, src_link, &src)) != 0) {
		mutex_exit(&devp->tfp_mutex);
		return (err);
	}

	if ((portp = tfport_port_new(devp, src, carg)) == NULL) {
		(void) tfport_close_source(devp, carg->tic_pkt_id);
		mutex_exit(&devp->tfp_mutex);
		return (EINVAL);
	}

	if (tfport_port_index(devp, portp) != 0)
		goto out;

	if ((err = tfport_mac_init(devp, portp)) != 0) {
		tfport_err(devp, "!tfport_init_mac() failed: %d", err);
		goto out;
	}
	portp->tp_run_state = TFPORT_RUNSTATE_DLS;
	mutex_exit(&devp->tfp_mutex);

	/*
	 * Because tp_run_state is TFPORT_RUNSTATE_DLS, nobody will be able to
	 * delete the port while we're in this upcall.
	 */
	if ((err = dls_devnet_create(portp->tp_mh, portp->tp_link_id,
	    crgetzoneid(cred))) != 0) {
		tfport_err(devp, "!dls_devnet_create() failed: %d", err);
		goto out;
	}

	portp->tp_link_state = mac_client_stat_get(src->tps_mch,
	    MAC_STAT_LINK_STATE);

	mutex_enter(&portp->tp_mutex);
	portp->tp_init_state |= TFPORT_INIT_DEVNET;
	portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	mutex_exit(&portp->tp_mutex);

	return (0);

out:
	mutex_enter(&portp->tp_mutex);
	(void) tfport_port_fini(devp, portp);
	mutex_exit(&devp->tfp_mutex);

	return (err);
}

static int
tfport_ioc_delete(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_delete_t *darg = karg;
	tfport_t *devp = tfport;
	tfport_port_t *portp;
	int rval;

	mutex_enter(&devp->tfp_mutex);
	portp = tfport_find_link(devp, darg->tid_link_id);
	if (portp == NULL) {
		rval = ENOENT;
	} else {
		mutex_enter(&portp->tp_mutex);
		if (portp->tp_run_state != TFPORT_RUNSTATE_STOPPED) {
			mutex_exit(&portp->tp_mutex);
			rval = EBUSY;
		} else {
			rval = tfport_port_fini(devp, portp);
		}
	}
	mutex_exit(&devp->tfp_mutex);

	return (rval);
}


static int
tfport_ioc_info(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_info_t *iarg = karg;
	tfport_t *devp = tfport;
	tfport_port_t *portp;
	datalink_id_t link = iarg->tii_link_id;
	int rval = 0;

	mutex_enter(&devp->tfp_mutex);
	portp = tfport_find_link(devp, link);
	if (portp == NULL) {
		rval = ENOENT;
	} else {
		iarg->tii_port_id = portp->tp_port;
		iarg->tii_link_id = portp->tp_link_id;
		iarg->tii_pkt_id = portp->tp_src_id;
		iarg->tii_mac_len = portp->tp_mac_len;
		if (iarg->tii_mac_len > 0 &&
		    iarg->tii_mac_len <= sizeof (iarg->tii_mac_addr)) {
			bcopy(portp->tp_mac_addr, iarg->tii_mac_addr,
			    iarg->tii_mac_len);
		}
	}
	mutex_exit(&devp->tfp_mutex);

	return (rval);
}

void
tfport_ioc_l2_done(ip2mac_t *ip2macp, void *arg)
{
}

/*
 * This provides a mechanism that allows a userspace daemon to request that we
 * initiate an arp/ndp request on behalf of the p4 program running on the ASIC.
 */
static void
tfport_ioc_l2_needed(tfport_port_t *portp, struct iocblk *iocp, queue_t *q,
    mblk_t *mp)
{
	tfport_ioc_l2_t *arg;
	struct sockaddr *addr;
	mblk_t *mp1;
	ip2mac_t ip2m;

	if (iocp->ioc_count < sizeof (tfport_ioc_l2_t))
		return (miocnak(q, mp, 0, EINVAL));

	mp1 = mp->b_cont;
	if (mp1 == NULL)
		return (miocnak(q, mp, 0, EINVAL));

	if (mp1->b_cont != NULL) {
		freemsg(mp1->b_cont);
		mp1->b_cont = NULL;
	}

	arg = (tfport_ioc_l2_t *)mp1->b_rptr;
	addr = (struct sockaddr *)&arg->til_addr;
	bzero(&ip2m, sizeof (ip2m));
	ip2m.ip2mac_ifindex = arg->til_ifindex;
	if (addr->sa_family == AF_INET) {
		sin_t *sin = (sin_t *)&ip2m.ip2mac_pa;
		sin->sin_family = AF_INET;
		sin->sin_addr = ((sin_t *)addr)->sin_addr;
	} else if (addr->sa_family == AF_INET6) {
		sin6_t *sin6 = (sin6_t *)&ip2m.ip2mac_pa;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = ((sin6_t *)addr)->sin6_addr;
	} else {
		return (miocnak(q, mp, 0, EINVAL));
	}

	(void) ip2mac(IP2MAC_RESOLVE, &ip2m, tfport_ioc_l2_done, NULL, 0);

	switch (ip2m.ip2mac_err) {
	case 0:
	case EINPROGRESS:
		return (miocack(q, mp, 0, 0));
	default:
		return (miocnak(q, mp, 0, EIO));
	}
}

static void
tfport_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	tfport_port_t *portp = arg;
	struct iocblk *iocp;
	int cmd;

	if (MBLKL(mp) < sizeof (struct iocblk)) {
		miocnak(q, mp, 0, EINVAL);
		return;
	}

	iocp = (struct iocblk *)mp->b_rptr;
	iocp->ioc_error = 0;
	cmd = iocp->ioc_cmd;
	switch (cmd) {
	case TFPORT_IOC_L2_NEEDED:
		tfport_ioc_l2_needed(portp, iocp, q, mp);
		break;
	default:
		miocnak(q, mp, 0, EINVAL);
	}
}

static int
tfport_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	tfport_port_t *portp = arg;
	int rval = 0;

	ASSERT(portp->tp_mh != NULL);

	mutex_enter(&portp->tp_mutex);
	switch (stat) {
	case MAC_STAT_LINK_STATE:
		*val = portp->tp_link_state;
		break;
	case MAC_STAT_LINK_UP:
		*val = (portp->tp_link_state == LINK_STATE_UP);
		break;
	case MAC_STAT_OPACKETS:
		*val = portp->tp_stats.tfs_tx_pkts;
		break;
	case MAC_STAT_OBYTES:
		*val = portp->tp_stats.tfs_tx_bytes;
		break;
	case MAC_STAT_RBYTES:
		*val = portp->tp_stats.tfs_rx_bytes;
		break;
	case MAC_STAT_IPACKETS:
		*val = portp->tp_stats.tfs_rx_pkts;
		break;
	default:
		rval = ENOTSUP;
		break;
	}
	mutex_exit(&portp->tp_mutex);

	return (rval);
}

static int
tfport_m_start(void *arg)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = portp->tp_tfport;
	int rval;

	mutex_enter(&devp->tfp_mutex);
	VERIFY3P(tfport_find_link(devp, portp->tp_link_id), ==, portp);

	mutex_enter(&portp->tp_mutex);
	if (portp->tp_run_state == TFPORT_RUNSTATE_DLS) {
		rval = EAGAIN;
	} else {
		portp->tp_run_state = TFPORT_RUNSTATE_RUNNING;
		rval = 0;
	}
	mutex_exit(&portp->tp_mutex);
	mutex_exit(&devp->tfp_mutex);

	return (rval);
}

static void
tfport_m_stop(void *arg)
{
	tfport_port_t *portp = arg;

	mutex_enter(&portp->tp_mutex);

	ASSERT(portp->tp_run_state != TFPORT_RUNSTATE_DLS);

	if (portp->tp_refcnt == 0)
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	else
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPING;

	mutex_exit(&portp->tp_mutex);
}

/*
 * We don't do any filtering, since we're expecting the switch to take care of
 * that.  We take note of the new setting, in case it's ever interesting for
 * debugging and return success.
 */
static int
tfport_m_promisc(void *arg, boolean_t on)
{
	tfport_port_t *portp = arg;

	portp->tp_promisc = on;
	return (0);
}

/*
 * We don't attempt to do any multicast filtering here.  If you squint and look
 * at it sideways, that means we have 0 filter slots, so we always return
 * ENOSPC.
 */
static int
tfport_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (ENOSPC);
}

/*
 * The tfport's mac address is intended to match that programmed into the
 * switch.  It's not something we support changing here.
 */
static int
tfport_m_unicst(void *arg, const uint8_t *macaddr)
{
	return (ENOTSUP);
}

static int
tfport_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = tfport_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = NULL;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

static int
tfport_dev_alloc(dev_info_t *dip)
{
	ASSERT(tfport == NULL);
	tfport = kmem_zalloc(sizeof (*tfport), KM_SLEEP);
	tfport->tfp_dip = dip;
	mutex_init(&tfport->tfp_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&tfport->tfp_sources, sizeof (tfport_source_t),
	    offsetof(tfport_source_t, tps_listnode));
	avl_create(&tfport->tfp_ports_by_port, tfport_port_cmp,
	    sizeof (tfport_port_t), offsetof(tfport_port_t, tp_port_node));
	avl_create(&tfport->tfp_ports_by_link, tfport_link_cmp,
	    sizeof (tfport_port_t), offsetof(tfport_port_t, tp_link_node));

	return (DDI_SUCCESS);
}

static void
tfport_dev_free(dev_info_t *dip)
{
	if (tfport != NULL) {
		mutex_destroy(&tfport->tfp_mutex);
		list_destroy(&tfport->tfp_sources);
		avl_destroy(&tfport->tfp_ports_by_link);
		avl_destroy(&tfport->tfp_ports_by_port);
		kmem_free(tfport, sizeof (*tfport));
		tfport = NULL;
	}
}

static int
tfport_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	int err;

	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if (instance != 0) {
		/* we only allow instance 0 to attach */
		dev_err(dip, CE_WARN, "attempted to attach instance %d",
		    instance);
		return (DDI_FAILURE);
	}

	ASSERT(tfport == NULL);
	ASSERT(tfport_dip == NULL);

	if ((err = dld_ioc_register(TFPORT_IOC, tfport_ioc_list,
	    DLDIOCCNT(tfport_ioc_list))) != 0) {
		dev_err(dip, CE_WARN, "dld_ioc_register failed: %d", err);
		return (DDI_FAILURE);
	}

	if ((err = tfport_dev_alloc(dip)) != 0) {
		dev_err(dip, CE_WARN, "failed to allocate tfport: %d", err);
		dld_ioc_unregister(TFPORT_IOC);
		return (DDI_FAILURE);
	}

	tfport_dip = dip;
	ddi_set_driver_private(dip, tfport);

	return (DDI_SUCCESS);
}

static int
tfport_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	tfport_t *devp;
	int rval = DDI_SUCCESS;

	switch (cmd) {
	case DDI_DETACH:
		devp = (tfport_t *)ddi_get_driver_private(dip);
		ASSERT(devp == tfport);
		mutex_enter(&devp->tfp_mutex);
		if (list_head(&devp->tfp_sources) != NULL) {
			rval = DDI_FAILURE;
		} else {
			ASSERT(avl_first(&tfport->tfp_ports_by_link) == NULL);
			ASSERT(avl_first(&tfport->tfp_ports_by_port) == NULL);
		}
		mutex_exit(&devp->tfp_mutex);

		if (rval == DDI_SUCCESS) {
			dld_ioc_unregister(TFPORT_IOC);
			tfport_dev_free(dip);
			tfport_dip = NULL;
		}

		return (rval);

	default:
		return (DDI_FAILURE);
	}
}

static struct cb_ops tfport_cb_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* cb_streamtab */
	D_MP,			/* cb_flag */
	CB_REV,			/* cb_rev */
	nodev,			/* cb_aread */
	nodev,			/* cb_awrite */
};

static struct dev_ops tfport_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	tfport_getinfo,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	tfport_attach,		/* devo_attach */
	tfport_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&tfport_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* devo_power */
	ddi_quiesce_not_needed,		/* devo_quiesce */
};
static struct modldrv tfport_modldrv = {
	.drv_modops =		&mod_driverops,
	.drv_linkinfo =		"Tofino Switch Port Multiplexer",
	.drv_dev_ops =		&tfport_dev_ops,
};

static struct modlinkage modlinkage = {
	.ml_rev =		MODREV_1,
	.ml_linkage =		{ &tfport_modldrv, NULL },
};

int
_init(void)
{
	int r;

	ASSERT(tfport == NULL);

	mac_init_ops(&tfport_dev_ops, "tfport");
	if ((r = mod_install(&modlinkage)) != 0) {
		cmn_err(CE_WARN, "tfport: mod_install failed: %d", r);
		mac_fini_ops(&tfport_dev_ops);
	}

	return (r);
}

int
_fini(void)
{
	int status;

	if ((status = mod_remove(&modlinkage)) == 0) {
		ASSERT(tfport == NULL);
		mac_fini_ops(&tfport_dev_ops);
	}

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
