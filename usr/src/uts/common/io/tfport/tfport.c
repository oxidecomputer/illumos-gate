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

DDI_DEFINE_STREAM_OPS(tfport_dev_ops, nulldev, nulldev, tfport_attach,
    tfport_detach, nodev, tfport_getinfo, D_MP, NULL,
    ddi_quiesce_not_needed);

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
static tfport_source_t *tfport_hold_source(tfport_t *devp, datalink_id_t src_id);
static int tfport_rele_source(tfport_t *devp, tfport_source_t *src);

static dld_ioc_info_t tfport_ioc_list[] = {
	{TFPORT_IOC_CREATE, DLDCOPYINOUT, sizeof (tfport_ioc_create_t),
	    tfport_ioc_create, secpolicy_dl_config},
	{TFPORT_IOC_DELETE, DLDCOPYIN, sizeof (tfport_ioc_delete_t),
	    tfport_ioc_delete, secpolicy_dl_config},
	{TFPORT_IOC_INFO, DLDCOPYINOUT, sizeof (tfport_ioc_info_t),
	    tfport_ioc_info, NULL},
};

int tfport_debug = 1;

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

#define CMP(a, b) ((a) < (b) ? -1 : (a) > (b) ? 1 : 0)
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
		c = CMP(ta->tp_pkt_src->tps_id, tb->tp_pkt_src->tps_id);
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
 * Return the active device associated with this port.  If no such device
 * exists, return the default device for this source.  In either case, take a
 * reference on the returned port.
 */
static tfport_port_t *
tfport_find_port(tfport_t *devp, tfport_source_t *srcp, int port)
{
	tfport_port_t find, *portp;

	mutex_enter(&devp->tfp_mutex);
	find.tp_port = port;
	find.tp_pkt_src = srcp;
	portp = avl_find(&devp->tfp_ports_by_port, &find, NULL);
	if (portp == NULL || portp->tp_run_state != TFPORT_RUNSTATE_RUNNING) {
		find.tp_port = 0;
		portp = avl_find(&devp->tfp_ports_by_port, &find, NULL);
	}

	if (portp != NULL) {
		if (portp->tp_run_state == TFPORT_RUNSTATE_RUNNING)
			portp->tp_refcnt++;
		else
			portp = NULL;
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

	mutex_enter(&devp->tfp_mutex);
	ASSERT(portp->tp_refcnt > 0);
	portp->tp_refcnt--;
	if (portp->tp_refcnt == 0 &&
	    portp->tp_run_state == TFPORT_RUNSTATE_STOPPING) {
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	}
	mutex_exit(&devp->tfp_mutex);
}

static int
tfport_tx_one(tfport_source_t *srcp, int port, mblk_t *mp_head)
{
	mblk_t *tx_buf;
	size_t full_sz = msgsize(mp_head);

	/*
	 * If this is from a port device, we need to insert a sidecar header
	 * after the ethernet header, so the ASIC knows which port the packet
	 * should egress.
	 */
	if (port == 0) {
		tx_buf = mp_head;
	} else {
		full_sz += SCSZ;

		if ((tx_buf = allocb(full_sz, BPRI_HI)) == NULL) {
			return (-1);
		}

		/*
		 * Copy the ethernet header into the transfer buffer:
		 */
		struct ether_header *eth =
		    (struct ether_header *)(tx_buf->b_wptr);
		bcopy(mp_head->b_rptr, tx_buf->b_wptr, ETHSZ);
		tx_buf->b_wptr += ETHSZ;

		/*
		 * If needed, construct the sidecar header and update the
		 * ethernet header:
		 */
		if (port != 0) {
			schdr_t *sc = (schdr_t *)(tx_buf->b_wptr);

			sc->sc_code = SC_FORWARD_FROM_USERSPACE;
			sc->sc_ingress = 0;
			sc->sc_egress = htons(port);
			sc->sc_ethertype = eth->ether_type;
			eth->ether_type = htons(ETHERTYPE_SIDECAR);
			tx_buf->b_wptr += SCSZ;
		}

		/*
		 * Copy the rest of the packet into the tx buffer, skipping
		 * over the ethernet header we've already copied.
		 */
		size_t skip = ETHSZ;
		for (mblk_t *m = mp_head; m != NULL; m = m->b_cont) {
			size_t sz = MBLKL(m) - skip;

			bcopy(m->b_rptr + skip, tx_buf->b_wptr, sz);
			tx_buf->b_wptr += sz;
			skip = 0;
		}
	}

	(void) mac_tx(srcp->tps_mch, tx_buf, 0, MAC_DROP_ON_NO_DESC, NULL);

	/*
	 * On success, the lower level is responsible for the transmit mblk.
	 * If that was our temporary mblk, then it is our responsibility to
	 * free the original mblk.
	 */
	if (tx_buf != mp_head)
		freeb(mp_head);
	return (0);
}

static mblk_t *
tfport_m_tx(void *arg, mblk_t *mp_chain)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = portp->tp_tfport;
	int port = portp->tp_port;
	tfport_source_t *srcp;
	mblk_t *mp, *next;

	mutex_enter(&devp->tfp_mutex);
	ASSERT(portp == tfport_find_link(devp, portp->tp_link_id));
	srcp = tfport_hold_source(devp, portp->tp_link_id);
	mutex_exit(&devp->tfp_mutex);

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		mp->b_next = NULL;
		if (srcp == NULL)
			freemsg(mp);
		else if (tfport_tx_one(srcp, port, mp) != 0)
			break;
	}

	if (srcp != NULL) {
		mutex_enter(&devp->tfp_mutex);
		tfport_rele_source(devp, srcp);
		mutex_exit(&devp->tfp_mutex);
	}

	return (mp);
}


static void
tfport_pkt_notify_cb(void *arg, mac_notify_type_t type)
{
}

static void
tfport_rx(void *arg, mac_resource_handle_t mrh, mblk_t *mp,
    boolean_t is_loopback)
{
	tfport_source_t *srcp = arg;
	tfport_t *devp = srcp->tps_tfport;
	tfport_port_t *portp;

	struct ether_header *eth = NULL;
	schdr_t *sc = NULL;
	uint32_t port = 0;
	size_t mblk_sz = msgsize(mp);

	if (is_loopback || mblk_sz < ETHSZ)
		goto done;

	/*
	 * Look for a sidecar header to determine whether the packet should be
	 * sent to an indexed port or the default port.
	 */
	eth = (struct ether_header *)mp->b_rptr;
	if (ntohs(eth->ether_type) == ETHERTYPE_SIDECAR) {
		if (mblk_sz < ETHSZ + SCSZ) {
			goto done;
		}
		sc = (schdr_t *)(mp->b_rptr + ETHSZ);
		if (sc->sc_code == SC_FORWARD_TO_USERSPACE)
			port = ntohs(sc->sc_ingress);
	}

	if ((portp = tfport_find_port(devp, srcp, port)) == NULL)
		goto done;

	/*
	 * If the packet is going to a port device, we strip out the sidecar
	 * header.  This requires:
	 *   - copying the ethertype from the sidecar header to the ethernet
	 *     header
	 *   - moving the body of the packet to replace the sidecar header
	 *   - adjusting the size of the buffer to reflect the removed header
	 */
	if (portp->tp_port != 0) {
		unsigned char *base = mp->b_rptr;
		size_t eth_hdr_end = ETHSZ;
		size_t sc_hdr_end = ETHSZ + SCSZ;
		size_t body_sz = mblk_sz - sc_hdr_end;

		eth->ether_type = sc->sc_ethertype;
		bcopy(base + sc_hdr_end, base + eth_hdr_end, body_sz);
		mp->b_wptr = base + (ETHSZ + body_sz);
	}

	mac_rx(portp->tp_mh, NULL, mp);
	tfport_rele_port(devp, portp);
	return;

done:
	freemsgchain(mp);
}

static int
tfport_mac_init(tfport_t *devp, tfport_port_t *portp)
{
	mac_register_t *mac;
	int err;

	mac = mac_alloc(MAC_VERSION);
	if (mac == NULL) {
		return (ENOMEM);
	}

	/* Register the new device with the mac(9e) framework */
	mac->m_driver = portp;
	mac->m_dip = devp->tfp_dip;
	mac->m_instance = portp->tp_port;
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
		mac_tx_update(portp->tp_mh);
	} else {
		tfport_dlog(devp, "failed to register port %d: %d",
		    portp->tp_port, err);
	}

	return (err);
}

static tfport_source_t *
tfport_hold_source(tfport_t *devp, datalink_id_t src_id)
{
	tfport_source_t *srcp;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	for (srcp = list_head(&devp->tfp_sources);
	    srcp != NULL;
	    srcp = list_next(&devp->tfp_sources, srcp)) {
		if (srcp->tps_id == src_id) {
			srcp->tps_refcnt++;
			break;
		}
	}
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
tfport_close_source(tfport_t *devp, tfport_source_t *srcp)
{
	int err;

	if (tfport_rele_source(devp, srcp) != 0)
		return (-1);

	list_remove(&devp->tfp_sources, srcp);
	if (srcp->tps_init_state & TFPORT_SOURCE_RX_SET)
		mac_rx_clear(srcp->tps_mch);

	if (srcp->tps_init_state & TFPORT_SOURCE_UNICAST_ADD &&
	    ((err = mac_unicast_remove(srcp->tps_mch, srcp->tps_muh)) != 0))
		tfport_err(devp, "mac_unicast_remove() failed: %d", err);

	if (srcp->tps_init_state & TFPORT_SOURCE_NOTIFY_ADD &&
	    ((err = mac_notify_remove(srcp->tps_mnh, B_FALSE)) != 0))
		tfport_err(devp, "mac_notify_remove() failed: %d", err);

	if (srcp->tps_init_state & TFPORT_SOURCE_CLIENT_OPEN)
		mac_client_close(srcp->tps_mch, MAC_CLOSE_FLAGS_EXCLUSIVE);

	if (srcp->tps_init_state & TFPORT_SOURCE_OPEN)
		mac_close(srcp->tps_mh);

	mutex_destroy(&srcp->tps_mutex);
	kmem_free(srcp, sizeof (*srcp));
	return (0);
}

static int
tfport_open_source(tfport_t *devp, datalink_id_t link, tfport_source_t **srcpp)
{
	tfport_source_t *srcp;
	const mac_info_t *minfop;
	int err;

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));

	if ((srcp = tfport_hold_source(devp, link)) != NULL) {
		*srcpp = srcp;
		return (0);
	}

	tfport_dlog(devp, "opening source link %d", link);

	srcp = kmem_zalloc(sizeof (*srcp), KM_SLEEP);
	list_insert_head(&devp->tfp_sources, srcp);
	mutex_init(&srcp->tps_mutex, NULL, MUTEX_DRIVER, NULL);
	srcp->tps_refcnt = 1;
	srcp->tps_tfport = devp;
	srcp->tps_id = link;

	if ((err = mac_open_by_linkid(link, &srcp->tps_mh)) != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_OPEN;

	err = mac_client_open(srcp->tps_mh, &srcp->tps_mch, "tfport",
	    MAC_OPEN_FLAGS_EXCLUSIVE);
	if (err != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_CLIENT_OPEN;

	minfop = mac_info(srcp->tps_mh);
	if (minfop->mi_media != DL_ETHER ||
	    minfop->mi_nativemedia != DL_ETHER) {
		err = ENOTSUP;
		goto out;
	}
	srcp->tps_mnh = mac_notify_add(srcp->tps_mh, tfport_pkt_notify_cb,
	    srcp);
	srcp->tps_init_state |= TFPORT_SOURCE_NOTIFY_ADD;

#if 0
	/* XXX: is there any reason we need this? */
	uint8_t mac_buf[ETHERADDRL];
	tfport_random_mac(mac_buf);

	err = mac_unicast_add(src->tps_mch, mac_buf, 0, &src->tps_muh, 0,
	    &mac_diag);
	if (err != 0)
		goto out;
	src->tps_init_state |= TFPORT_SOURCE_UNICAST_ADD;
#endif

	mac_rx_set(srcp->tps_mch, tfport_rx, srcp);
	srcp->tps_init_state |= TFPORT_SOURCE_RX_SET;
out:
	if (err == 0) {
		*srcpp = srcp;
	} else {
		(void) tfport_close_source(devp, srcp);
	}

	return (err);
}

static void
tfport_port_fini(tfport_t *devp, tfport_port_t *portp)
{
	char name[64];
	datalink_id_t tmpid;
	int err;

	ASSERT(MUTEX_NOT_HELD(&devp->tfp_mutex));

	(void) snprintf(name, sizeof (name), "tfport%d", portp->tp_port);
	if (portp->tp_init_state & TFPORT_INIT_DEVNET &&
	    ((err = dls_devnet_destroy(portp->tp_mh, &tmpid, B_TRUE)) != 0)) {
		tfport_err(devp, "failed to clean up devnet.  "
		    "name: %s  link: %d  err: %d", name, portp->tp_link_id, err);
	}

	if (portp->tp_init_state & TFPORT_INIT_MAC_REGISTER &&
	    ((err = mac_unregister(portp->tp_mh)) != 0)) {
		tfport_err(devp, "failed to unregister mac.  name: %s  err: %d",
		    name, err);
	}

	kmem_free(portp, sizeof (*portp));
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

	ASSERT(MUTEX_HELD(&devp->tfp_mutex));

	/* Check both trees for collisions and for the insert location */
	if (avl_find(&devp->tfp_ports_by_port, portp, &port_where) != NULL) {
		tfport_dlog(devp, "collision in port tree");
		return (-1);
	}
	if (avl_find(&devp->tfp_ports_by_link, portp, &link_where) != NULL) {
		tfport_dlog(devp, "collision in link tree");
		return (-1);
	}
	avl_insert(&devp->tfp_ports_by_port, portp, port_where);
	avl_insert(&devp->tfp_ports_by_link, &portp, link_where);
	return (0);
}

/*
 * Remove the provided port from both avl trees.
 */
static void
tfport_port_deindex(tfport_t *devp, tfport_port_t *portp)
{
	ASSERT(MUTEX_HELD(&devp->tfp_mutex));
	ASSERT(avl_find(&devp->tfp_ports_by_port, portp, NULL) != NULL);
	ASSERT(avl_find(&devp->tfp_ports_by_link, portp, NULL) != NULL);

	avl_remove(&devp->tfp_ports_by_link, portp);
	avl_remove(&devp->tfp_ports_by_port, portp);
}

static tfport_port_t *
tfport_port_new(tfport_t *devp, tfport_ioc_create_t *carg)
{
	tfport_port_t *portp;
	uchar_t mac_buf[ETHERADDRL];
	uchar_t *mac_addr;

	if (carg->tic_mac_len == 0) {
		tfport_random_mac(mac_buf);
		mac_addr = mac_buf;
	} else if (carg->tic_mac_len == ETHERADDRL) {
		mac_addr = carg->tic_mac_addr;
	} else {
		return (NULL);
	}

	portp = kmem_zalloc(sizeof (*portp), KM_SLEEP);
	portp->tp_refcnt = 0;
	portp->tp_tfport = devp;
	portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	portp->tp_port = carg->tic_port_id;
	portp->tp_link_id = carg->tic_link_id;
	bcopy(mac_addr, portp->tp_mac_addr, ETHERADDRL);
	portp->tp_mac_len = ETHERADDRL;
	portp->tp_ls = LINK_STATE_UNKNOWN;

	return (portp);
}

static int
tfport_ioc_create(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_create_t *carg = karg;
	tfport_t *devp = tfport;
	datalink_id_t src_link;
	tfport_source_t *src;
	tfport_port_t *portp;
	int err;

	tfport_dlog(devp, "%s(port: %d  link: %d  src: %d)",
	    __func__, carg->tic_port_id, carg->tic_link_id, carg->tic_pkt_id);

	src_link = carg->tic_pkt_id;
	if ((err = tfport_open_source(devp, src_link, &src)) != 0) {
		mutex_exit(&devp->tfp_mutex);
		return (err);
	}

	if ((portp = tfport_port_new(devp, carg)) == NULL) {
		err = EINVAL;
		goto out;
	}

	mutex_enter(&devp->tfp_mutex);
	if (tfport_port_index(devp, portp) != 0)
		goto out;
	portp->tp_init_state |= TFPORT_INIT_INDEXED;

	if ((err = tfport_mac_init(devp, portp)) != 0) {
		tfport_err(devp, "tfport_init_mac() failed: %d", err);
		goto out;
	}

	if ((err = dls_devnet_create(portp->tp_mh, portp->tp_link_id,
	    getzoneid())) != 0) {
		tfport_err(devp, "dls_devnet_create() failed: %d", err);
		goto out;
	}
	portp->tp_init_state |= TFPORT_INIT_DEVNET;
	mutex_exit(&devp->tfp_mutex);

	return (0);

out:
	(void) tfport_close_source(devp, src);
	if (portp != NULL) {
		if (portp->tp_init_state & TFPORT_INIT_INDEXED) {
			mutex_enter(&devp->tfp_mutex);
			tfport_port_deindex(devp, portp);
			mutex_exit(&devp->tfp_mutex);
		}
		tfport_port_fini(devp, portp);
	}

	return (err);
}

static int
tfport_ioc_delete(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_delete_t *darg = karg;
	tfport_t *devp = tfport;
	tfport_port_t *portp;
	datalink_id_t link = darg->tid_link_id;
	int rval = 0;

	tfport_dlog(devp, "%s(link: %d)", __func__, darg->tid_link_id);

	mutex_enter(&devp->tfp_mutex);
	portp = tfport_find_link(devp, link);
	if (portp == NULL) {
		rval =  (ENOENT);
	} else if (portp->tp_run_state != TFPORT_RUNSTATE_STOPPED) {
		rval = EBUSY;
	} else if (tfport_close_source(devp, portp->tp_pkt_src) != 0) {
		rval = EBUSY;
	} else {
		tfport_port_deindex(devp, portp);
	}
	mutex_exit(&devp->tfp_mutex);

	if (rval == 0)
		tfport_port_fini(devp, portp);

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
		iarg->tii_pkt_id = portp->tp_pkt_src->tps_id;
		iarg->tii_mac_len = MIN(portp->tp_mac_len, ETHERADDRL);
		bcopy(portp->tp_mac_addr, iarg->tii_mac_addr,
		    iarg->tii_mac_len);
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
	tfport_t *devp = tfport;
	static uintptr_t cnt = 0;
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

		char buf1[INET6_ADDRSTRLEN];
		tfport_dlog(devp, "ipv4 addr: %s",
		    inet_ntop(AF_INET, &sin->sin_addr, buf1, sizeof (buf1)));
	} else if (addr->sa_family == AF_INET6) {
		sin6_t *sin6 = (sin6_t *)&ip2m.ip2mac_pa;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = ((sin6_t *)addr)->sin6_addr;

		char buf1[INET6_ADDRSTRLEN];
		tfport_dlog(devp, "ipv6 addr: %s on %d",
		    inet_ntop(AF_INET6, &sin6->sin6_addr, buf1, sizeof (buf1)),
		    ip2m.ip2mac_ifindex);
	} else {
		return (miocnak(q, mp, 0, EINVAL));
	}

	cnt++;
	(void) ip2mac(IP2MAC_RESOLVE, &ip2m, tfport_ioc_l2_done,
	    (void *)cnt, 0);
	switch (ip2m.ip2mac_err) {
	case EINPROGRESS:
		tfport_dlog(devp, "searching for %ld", cnt);
		return (miocack(q, mp, 0, 0));
	case 0:
		tfport_dlog(devp, "already loaded");
		return (miocack(q, mp, 0, 0));
	default:
		tfport_dlog(devp, "ip2mac failed: %d", ip2m.ip2mac_err);
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

	switch (stat) {
	case MAC_STAT_LINK_STATE:
		*val = LINK_DUPLEX_FULL;
		break;
	case MAC_STAT_LINK_UP:
		if (portp->tp_run_state == TFPORT_RUNSTATE_RUNNING)
			*val = LINK_STATE_UP;
		else
			*val = LINK_STATE_DOWN;
		break;
	case MAC_STAT_IFSPEED:
	case MAC_STAT_PROMISC:
	case MAC_STAT_MULTIRCV:
	case MAC_STAT_MULTIXMT:
	case MAC_STAT_BRDCSTRCV:
	case MAC_STAT_BRDCSTXMT:
		rval = ENOTSUP;
		break;
	case MAC_STAT_OPACKETS:
		*val = portp->tp_stats.tfs_xmit_count;
		break;
	case MAC_STAT_OBYTES:
		*val = portp->tp_stats.tfs_obytes;
		break;
	case MAC_STAT_IERRORS:
		*val = portp->tp_stats.tfs_recv_errors;
		break;
	case MAC_STAT_OERRORS:
		*val = portp->tp_stats.tfs_xmit_errors;
		break;
	case MAC_STAT_RBYTES:
		*val = portp->tp_stats.tfs_rbytes;
		break;
	case MAC_STAT_IPACKETS:
		*val = portp->tp_stats.tfs_recv_count;
		break;
	default:
		rval = ENOTSUP;
		break;
	}

	return (rval);
}

static int
tfport_m_start(void *arg)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = portp->tp_tfport;
	tfport_port_t *indexed;
	int rval;

	/*
	 * There is a window during the port teardown where tfp_mutex is
	 * released, the port has been removed from the indexes, but has not yet
	 * unregistered with the mac layer.  We detect this window below to
	 * avoid re-enabling a port that's going away.
	 */
	mutex_enter(&devp->tfp_mutex);
	indexed = tfport_find_link(devp, portp->tp_link_id);
	if (indexed == NULL) {
		rval = ENXIO;
	} else {
		ASSERT(indexed == portp);
		rval = 0;
		portp->tp_run_state = TFPORT_RUNSTATE_RUNNING;
	}
	mutex_exit(&devp->tfp_mutex);

	return (rval);
}

static void
tfport_m_stop(void *arg)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = portp->tp_tfport;

	mutex_enter(&devp->tfp_mutex);
	tfport_dlog(devp, "%s(port: %d  refcnt: %d)", __func__, 
	    portp->tp_port, portp->tp_refcnt);

	if (portp->tp_refcnt == 0)
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	else
		portp->tp_run_state = TFPORT_RUNSTATE_STOPPING;

	mutex_exit(&devp->tfp_mutex);
}

static int
tfport_m_promisc(void *arg, boolean_t on)
{
	tfport_port_t *portp = arg;

	portp->tp_promisc = on;
	return (0);
}

static int
tfport_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (ENOTSUP);
}

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
	list_create(&tfport->tfp_sources, sizeof(tfport_source_t),
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
	int err;

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_get_instance(dip) != 0) {
			/* we only allow instance 0 to attach */
			dev_err(dip, CE_WARN, "attempted to attach instance %d",
			    ddi_get_instance(dip));
			return (DDI_FAILURE);
		}

		ASSERT(tfport == NULL);
		ASSERT(tfport_dip == NULL);

		if ((err = tfport_dev_alloc(dip)) != 0) {
			dev_err(dip, CE_WARN, "failed to allocate tfport: %d", err);
			return (err);
		}

		tfport_dip = dip;
		ddi_set_driver_private(dip, tfport);

		return (DDI_SUCCESS);

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
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
			tfport_dev_free(dip);
			tfport_dip = NULL;
		}

		return (rval);

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

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
		cmn_err(CE_WARN, "tfport: modinstall failed: %d", r);
		goto err1;
	}

	if ((r = dld_ioc_register(TFPORT_IOC, tfport_ioc_list,
	    DLDIOCCNT(tfport_ioc_list))) != 0) {
		cmn_err(CE_WARN, "tfport: failed to register ioctls: %d", r);
		goto err2;
	}

	return (r);

err2:
	(void) mod_remove(&modlinkage);
err1:
	mac_fini_ops(&tfport_dev_ops);
	return (r);
}

int
_fini(void)
{
	int status;

	if (tfport != NULL)
		return (EBUSY);

	dld_ioc_unregister(TFPORT_IOC);
	if ((status = mod_remove(&modlinkage)) == 0) {
		mac_fini_ops(&tfport_dev_ops);
	}

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
