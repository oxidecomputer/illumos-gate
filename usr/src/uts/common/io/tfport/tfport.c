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
#define	SCSZ (sizeof (struct schdr))

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
    ddi_quiesce_not_supported);

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

static dld_ioc_info_t tfport_ioc_list[] = {
	{TFPORT_IOC_CREATE, DLDCOPYINOUT, sizeof (tfport_ioc_create_t),
	    tfport_ioc_create, secpolicy_dl_config},
	{TFPORT_IOC_DELETE, DLDCOPYIN, sizeof (tfport_ioc_delete_t),
	    tfport_ioc_delete, secpolicy_dl_config},
	{TFPORT_IOC_INFO, DLDCOPYINOUT, sizeof (tfport_ioc_info_t),
	    tfport_ioc_info, NULL},
};

static void
tfport_random_mac(uint8_t mac[ETHERADDRL])
{
	(void) random_get_pseudo_bytes(mac, ETHERADDRL);
	/* Ensure MAC address is not multicast and is local */
	mac[0] = (mac[0] & ~1) | 2;
}

/*
 * Return the device associated with this port.  If no such device exists,
 * return the system device.
 */
static tfport_port_t *
tfport_find_port(tfport_t *devp, int port)
{
	list_t *ports = &devp->tfp_source->tps_ports;
	tfport_port_t *portp, *rval;

	/* XXX: take a lock on devp and maintain a reference count on portp */
	rval = NULL;
	portp = list_head(ports);
	while (portp != NULL) {
		if (portp->tp_port == port || portp->tp_port == 0) {
			rval = portp;
			if (portp->tp_port == port)
				break;
		}
		portp = list_next(ports, portp);
	}

	return (rval);
}

static int
tfport_tx_one(tfport_port_t *portp, mblk_t *mp_head)
{
	tfport_t *devp = portp->tp_tfport;
	tfport_source_t *srcp = devp->tfp_source;
	mblk_t *tx_buf;
	size_t full_sz = msgsize(mp_head);

	/*
	 * If this is from a port device, we need to insert a sidecar header
	 * after the ethernet header, so the ASIC knows which port the packet
	 * should egress.
	 */
	if (portp->tp_port == 0) {
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
		if (portp->tp_port != 0) {
			struct schdr *sc = (struct schdr *)(tx_buf->b_wptr);

			sc->sc_code = SC_FORWARD_FROM_USERSPACE;
			sc->sc_ingress = 0;
			sc->sc_egress = htons(portp->tp_port);
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
	mblk_t *mp, *next;

	for (mp = mp_chain; mp != NULL; mp = next) {
		next = mp->b_next;
		if (tfport_tx_one(portp, mp) != 0) {
			/*
			 * XXX: call mac_tx_update() when more tx_bufs
			 * become available
			 */
			return (mp);
		}
	}

	return (NULL);
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
	struct schdr *sc = NULL;
	uint32_t port = 0;
	size_t mblk_sz = msgsize(mp);

	if (is_loopback) {
		goto done;
	}

	if (mblk_sz < ETHSZ) {
		goto done;
	}

	/*
	 * Look for a sidecar header to determine whether the packet should be
	 * sent to an indexed port the default port.
	 */
	eth = (struct ether_header *)mp->b_rptr;
	if (ntohs(eth->ether_type) == ETHERTYPE_SIDECAR) {
		if (mblk_sz < ETHSZ + SCSZ) {
			goto done;
		}
		sc = (struct schdr *)(mp->b_rptr + ETHSZ);
		if (sc->sc_code == SC_FORWARD_TO_USERSPACE)
			port = ntohs(sc->sc_ingress);
	}

	portp = tfport_find_port(devp, port);
	if (portp == NULL) {
		goto done;
	}

	if (portp->tp_run_state != TFPORT_RUNSTATE_RUNNING) {
		goto done;
	}

	/*
	 * If the packet is going to a port device, we strip off the sidecar
	 * header.
	 * XXX: is there a more streams-specific idiom to use here, rather
	 * than allocating a new buffer and doing this manual offset
	 * calculation and copying?
	 */
	if (portp->tp_port != 0) {
		mblk_t *edited;
		size_t hdr_sz = ETHSZ + SCSZ;
		size_t body_sz = mblk_sz - hdr_sz;

		if ((edited = allocb(mblk_sz - SCSZ, 0)) == NULL) {
			dev_err(devp->tfp_dip, CE_NOTE, "allocb failed");
			goto done;
		}

		eth->ether_type = sc->sc_ethertype;
		bcopy(eth, edited->b_wptr, ETHSZ);
		edited->b_wptr += ETHSZ;
		bcopy(mp->b_rptr + hdr_sz, edited->b_wptr, body_sz);
		edited->b_wptr += body_sz;

		freemsgchain(mp);
		mp = edited;
	}

	mac_rx(portp->tp_mh, NULL, mp);
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
		dev_err(devp->tfp_dip, CE_WARN, "failed to register port %d",
		    portp->tp_port);
	}

	return (err);
}

static void
tfport_close_source(tfport_t *devp, tfport_source_t *srcp)
{
	if (srcp == NULL)
		return;

	if (srcp->tps_init_state & TFPORT_SOURCE_RX_SET)
		mac_rx_clear(srcp->tps_mch);

	if (srcp->tps_init_state & TFPORT_SOURCE_UNICAST_ADD &&
	    mac_unicast_remove(srcp->tps_mch, srcp->tps_muh) != 0) {
		dev_err(devp->tfp_dip, CE_WARN, "mac_unicast_remove() failed");
	}

	if (srcp->tps_init_state & TFPORT_SOURCE_NOTIFY_ADD &&
	    mac_notify_remove(srcp->tps_mnh, B_FALSE) != 0) {
		dev_err(devp->tfp_dip, CE_WARN, "mac_notify_remove() failed");
	}

	if (srcp->tps_init_state & TFPORT_SOURCE_CLIENT_OPEN)
		mac_client_close(srcp->tps_mch, 0);

	if (srcp->tps_init_state & TFPORT_SOURCE_OPEN)
		mac_close(srcp->tps_mh);

	list_destroy(&srcp->tps_ports);
	mutex_destroy(&srcp->tps_mutex);
	kmem_free(srcp, sizeof (*srcp));
}

static int
tfport_open_source(tfport_t *devp, datalink_id_t src_id,
    tfport_source_t **srcpp)
{
	tfport_source_t *srcp;
	const mac_info_t *minfop;
	int err;

	srcp = kmem_zalloc(sizeof (*srcp), KM_SLEEP);
	srcp->tps_tfport = devp;
	srcp->tps_id = src_id;
	list_create(&srcp->tps_ports, sizeof (tfport_port_t), 0);

	err = mac_open_by_linkid(src_id, &srcp->tps_mh);
	if (err != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_OPEN;

	err = mac_client_open(srcp->tps_mh, &srcp->tps_mch, "tfport", 0);
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

	err = mac_unicast_add(srcp->tps_mch, mac_buf, 0, &srcp->tps_muh, 0,
	    &mac_diag);
	if (err != 0)
		goto out;
	srcp->tps_init_state |= TFPORT_SOURCE_UNICAST_ADD;
#endif

	mac_rx_set(srcp->tps_mch, tfport_rx, srcp);
	srcp->tps_init_state |= TFPORT_SOURCE_RX_SET;

	/*
	 * XXX mac_promisc_add() needed too?
	 */

out:
	if (err == 0) {
		*srcpp = srcp;
	} else {
		tfport_close_source(devp, srcp);
	}

	return (err);
}

static void
tfport_port_fini(tfport_t *devp, tfport_port_t *portp)
{
	char name[64];
	datalink_id_t tmpid;

	(void) snprintf(name, sizeof (name), "tfport%d", portp->tp_port);
	if (portp->tp_init_state & TFPORT_INIT_DEVNET &&
	    dls_devnet_destroy(portp->tp_mh, &tmpid, B_TRUE) != 0) {
		dev_err(devp->tfp_dip, CE_WARN,
		    "%s: failed to clean up devnet for %d", name,
		    portp->tp_link_id);
	}

	if (portp->tp_init_state & TFPORT_INIT_MAC_REGISTER &&
	    mac_unregister(portp->tp_mh) != 0) {
		dev_err(devp->tfp_dip, CE_WARN,
		    "%s: failed to unregister mac", name);
	}

	mutex_destroy(&portp->tp_mutex);
	kmem_free(portp, sizeof (*portp));
}

static int
tfport_ioc_create(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_create_t *carg = karg;
	tfport_t *devp = tfport;
	tfport_source_t *srcp;
	tfport_port_t *portp = NULL;
	uchar_t mac_buf[ETHERADDRL];
	uchar_t *mac_addr;
	int err;

	if (carg->tic_port_id > 1024) {
		dev_err(devp->tfp_dip, CE_WARN, "invalid port-id");
		return (EINVAL);
	}

	if (carg->tic_mac_len == 0) {
		tfport_random_mac(mac_buf);
		mac_addr = mac_buf;
	} else if (carg->tic_mac_len == ETHERADDRL) {
		mac_addr = carg->tic_mac_addr;
	} else {
		dev_err(devp->tfp_dip, CE_WARN, "invalid mac address");
		return (EINVAL);
	}

	mutex_enter(&devp->tfp_mutex);

	/*
	 * XXX: we currently only support a single packet source.  This is
	 * all we need for the current product definition, but it is distinctly
	 * unsatisfying.  We should add support for multiple sources
	 * simultaneously.
	 */
	if ((srcp = devp->tfp_source) == NULL) {
		err = tfport_open_source(devp, carg->tic_pkt_id, &srcp);
		if (err != 0)
			goto out;
		devp->tfp_source = srcp;
	} else if (carg->tic_pkt_id != devp->tfp_source->tps_id) {
		dev_err(devp->tfp_dip, CE_WARN, "attempt to use second source");
		err = EINVAL;
		goto out;
	}

	portp = kmem_zalloc(sizeof (*portp), KM_NOSLEEP);
	if (portp == NULL) {
		err = ENOMEM;
		goto out;
	}

	mutex_init(&portp->tp_mutex, NULL, MUTEX_DRIVER, NULL);
	portp->tp_tfport = devp;
	portp->tp_run_state = TFPORT_RUNSTATE_STOPPED;
	portp->tp_port = carg->tic_port_id;
	portp->tp_link_id = carg->tic_link_id;
	portp->tp_pkt_id = carg->tic_pkt_id;
	bcopy(mac_addr, portp->tp_mac_addr, ETHERADDRL);
	portp->tp_mac_len = ETHERADDRL;
	portp->tp_ls = LINK_STATE_UNKNOWN;

	if ((err = tfport_mac_init(devp, portp)) != 0) {
		dev_err(devp->tfp_dip, CE_WARN, "tfport_init_mac() failed");
		goto out;
	}

	if ((err = dls_devnet_create(portp->tp_mh, portp->tp_link_id,
	    getzoneid())) != 0) {
		dev_err(devp->tfp_dip, CE_WARN, "dls_devnet_create() failed");
		goto out;
	}
	portp->tp_init_state |= TFPORT_INIT_DEVNET;

	list_insert_head(&devp->tfp_source->tps_ports, portp);

out:
	if (err != 0 && portp != NULL)
		tfport_port_fini(devp, portp);
	mutex_exit(&devp->tfp_mutex);

	return (err);
}

static tfport_port_t *
tfport_find(tfport_t *devp, datalink_id_t link)
{
	tfport_port_t *portp = list_head(&devp->tfp_source->tps_ports);

	while (portp != NULL && portp->tp_link_id != link) {
		portp = list_next(&devp->tfp_source->tps_ports, portp);
	}

	return (portp);
}

static int
tfport_ioc_delete(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_delete_t *darg = karg;
	tfport_t *devp = tfport;
	tfport_port_t *portp;
	datalink_id_t link = darg->tid_link_id;
	int rval = 0;

	mutex_enter(&devp->tfp_mutex);
	portp = tfport_find(devp, link);
	if (portp == NULL) {
		rval = ENOENT;
	} else {
		mutex_enter(&portp->tp_mutex);
		if (portp->tp_run_state != TFPORT_RUNSTATE_STOPPED) {
			dev_err(devp->tfp_dip, CE_WARN, "port %d is busy",
			    link);
			rval = EBUSY;
		} else {
			list_remove(&devp->tfp_source->tps_ports, portp);
		}
		mutex_exit(&portp->tp_mutex);

	}
	mutex_exit(&devp->tfp_mutex);

	if (rval == 0) {
		tfport_port_fini(devp, portp);
	}

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
	portp = tfport_find(devp, link);
	if (portp == NULL) {
		rval = ENOENT;
	} else {
		mutex_enter(&portp->tp_mutex);
		iarg->tii_port_id = portp->tp_port;
		iarg->tii_link_id = portp->tp_link_id;
		iarg->tii_pkt_id = portp->tp_pkt_id;
		iarg->tii_mac_len = MIN(portp->tp_mac_len, ETHERADDRL);
		bcopy(portp->tp_mac_addr, iarg->tii_mac_addr,
		    iarg->tii_mac_len);
		mutex_exit(&portp->tp_mutex);
	}
	mutex_exit(&devp->tfp_mutex);

	return (rval);
}

void
tfport_ioc_l2_done(ip2mac_t *ip2macp, void *arg)
{
}

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
		dev_err(devp->tfp_dip, CE_NOTE, "ipv4 addr: %s",
		    inet_ntop(AF_INET, &sin->sin_addr, buf1, sizeof (buf1)));
	} else if (addr->sa_family == AF_INET6) {
		sin6_t *sin6 = (sin6_t *)&ip2m.ip2mac_pa;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = ((sin6_t *)addr)->sin6_addr;

		char buf1[INET6_ADDRSTRLEN];
		dev_err(devp->tfp_dip, CE_NOTE, "ipv6 addr: %s on %d",
		    inet_ntop(AF_INET6, &sin6->sin6_addr, buf1, sizeof (buf1)),
		    ip2m.ip2mac_ifindex);
	} else {
		dev_err(devp->tfp_dip, CE_NOTE, "failed at %d", __LINE__);
		return (miocnak(q, mp, 0, EINVAL));
	}

	cnt++;
	(void) ip2mac(IP2MAC_RESOLVE, &ip2m, tfport_ioc_l2_done,
	    (void *)cnt, 0);
	switch (ip2m.ip2mac_err) {
	case EINPROGRESS:
		dev_err(devp->tfp_dip, CE_NOTE, "searching for %ld", cnt);
		return (miocack(q, mp, 0, 0));
	case 0:
		dev_err(devp->tfp_dip, CE_NOTE, "already loaded");
		return (miocack(q, mp, 0, 0));
	default:
		dev_err(devp->tfp_dip, CE_NOTE, "ip2mac failed: %d",
		    ip2m.ip2mac_err);
		return (miocnak(q, mp, 0, EIO));
	}
}

static void
tfport_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	tfport_port_t *portp = arg;
	tfport_t *devp = tfport;
	struct iocblk *iocp;
	int cmd;

	if (MBLKL(mp) < sizeof (struct iocblk)) {
		dev_err(devp->tfp_dip, CE_WARN,
		    "tfport: ioctl buffer too short, %lu", MBLKL(mp));
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
	case MAC_STAT_IFSPEED:
		*val = 100 * 1000000ull; /* 100 Mbps */
		break;
	case MAC_STAT_LINK_STATE:
		*val = LINK_DUPLEX_FULL;
		break;
	case MAC_STAT_LINK_UP:
		if (portp->tp_run_state == TFPORT_RUNSTATE_RUNNING)
			*val = LINK_STATE_UP;
		else
			*val = LINK_STATE_DOWN;
		break;
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

	portp->tp_run_state = TFPORT_RUNSTATE_RUNNING;
	return (0);
}

static void
tfport_m_stop(void *arg)
{
	tfport_port_t *portp = arg;

	portp->tp_run_state = TFPORT_RUNSTATE_STOPPING;
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
	return (0);
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
	tfport = kmem_zalloc(sizeof (*tfport), KM_NOSLEEP);
	if (tfport == NULL) {
		return (DDI_ENOMEM);
	} else {
		mutex_init(&tfport->tfp_mutex, NULL, MUTEX_DRIVER, NULL);
		tfport->tfp_dip = dip;
		return (DDI_SUCCESS);
	}
}

static void
tfport_dev_free(dev_info_t *dip)
{
	if (tfport != NULL) {
		mutex_destroy(&tfport->tfp_mutex);
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
			dev_err(dip, CE_WARN, "failed to allocate tfport");
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
		if (devp->tfp_source != NULL) {
			if (list_is_empty(&devp->tfp_source->tps_ports)) {
				tfport_close_source(devp, devp->tfp_source);
			} else {
				rval = DDI_FAILURE;
			}
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
		cmn_err(CE_WARN, "tfport: modinstall failed");
		goto err1;
	}

	if ((r = dld_ioc_register(TFPORT_IOC, tfport_ioc_list,
	    DLDIOCCNT(tfport_ioc_list))) != 0) {
		cmn_err(CE_WARN, "tfport: failed to register ioctls");
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
