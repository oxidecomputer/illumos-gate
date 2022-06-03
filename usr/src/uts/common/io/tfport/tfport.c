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

#include <sys/policy.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/dls.h>
#include <sys/dld_ioc.h>
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
#include <sys/tofino_impl.h>
#include <sys/tofino_pkt.h>
#include <sys/tfport.h>

#include "tfport_impl.h"

#define	ETHSZ (sizeof(struct ether_header))
#define	SCSZ (sizeof(struct schdr))

#define	TFPORTINFO		"Tofino Switch Network Driver"

static tfport_t *tfport;
static dev_info_t *tfport_dip;

static void tfport_rx(void *, void *, size_t);
static void tfport_cmp(void);

static int tfport_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int tfport_attach(dev_info_t *, ddi_attach_cmd_t);
static int tfport_detach(dev_info_t *, ddi_detach_cmd_t);
static int tfport_probe(dev_info_t *);

/* MAC callback function declarations */
static int tfport_m_start(void *);
static void tfport_m_stop(void *);
static int tfport_m_promisc(void *, boolean_t);
static int tfport_m_multicst(void *, boolean_t, const uint8_t *);
static int tfport_m_unicst(void *, const uint8_t *);
static int tfport_m_stat(void *, uint_t, uint64_t *);
static void tfport_m_ioctl(void *, queue_t *, mblk_t *);
static mblk_t *tfport_m_tx(void *, mblk_t *);

DDI_DEFINE_STREAM_OPS(tfport_dev_ops, nulldev, tfport_probe, tfport_attach,
    tfport_detach, nodev, tfport_getinfo, D_MP, NULL,
    ddi_quiesce_not_supported);

static mac_callbacks_t tfport_m_callbacks = {
	MC_IOCTL,
	tfport_m_stat,
	tfport_m_start,
	tfport_m_stop,
	tfport_m_promisc,
	tfport_m_multicst,
	tfport_m_unicst,
	tfport_m_tx,
	NULL,
	tfport_m_ioctl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static int tfport_ioc_create(void *, intptr_t, int, cred_t *, int *);
static int tfport_ioc_delete(void *, intptr_t, int, cred_t *, int *);
static int tfport_ioc_info(void *, intptr_t, int, cred_t *, int *);

static dld_ioc_info_t tfport_ioc_list[] = {
	{TFPORT_IOC_CREATE, DLDCOPYINOUT, sizeof (tfport_ioc_create_t),
	    tfport_ioc_create, secpolicy_dl_config},
	{TFPORT_IOC_DELETE, DLDCOPYIN, sizeof (tfport_ioc_delete_t),
	    tfport_ioc_delete, secpolicy_dl_config},
	{TFPORT_IOC_INFO, DLDCOPYINOUT, sizeof (tfport_ioc_info_t),
	    tfport_ioc_info, NULL},
};

/*ARGSUSED*/
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
tfport_probe(dev_info_t *dip)
{
	return (DDI_PROBE_SUCCESS);
}

static int
tfport_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	const int max_inst = 16;
	int instance;
	tofino_pkt_cookie_t cookie;

	dev_err(dip, CE_NOTE,"%s", __func__);
	switch (cmd) {
	case DDI_ATTACH:
		ASSERT(tfport != NULL);
		ASSERT(tfport_dip == NULL);

		if ((instance = ddi_get_instance(dip)) != 0) {
			/* we only allow instance 0 to attach */
			dev_err(dip, CE_WARN, "attempted to attach instance %d",
			    instance);
			return (DDI_FAILURE);
		}

		/*
		 * There is only a single tofino device per system (for now at
		 * least), so we stop when successfully register.
		 */
		for (instance = 0; instance < max_inst; instance++) {
			int err = tofino_pkt_register(instance, &cookie,
			    (void *)tfport, tfport_rx, tfport_cmp);
			if (err == 0)
				break;
			if (err != ENXIO) {
				dev_err(dip, CE_WARN,
				    "failed to register with tofino pkt handler");
				return (DDI_FAILURE);
			}
		}

		if (instance == max_inst) {
			dev_err(dip, CE_WARN, "failed to find a tofino device");
			return (DDI_FAILURE);
		}

		if (dld_ioc_register(TFPORT_IOC, tfport_ioc_list,
		    DLDIOCCNT(tfport_ioc_list)) != 0) {
			dev_err(dip, CE_WARN, "failed to register ioctls");
			tofino_pkt_unregister(instance, cookie);
			return (DDI_FAILURE);
		}

		tfport_dip = dip;
		tfport->tfp_dip = dip;
		tfport->tfp_instance = instance;
		tfport->tfp_pkt_cookie = cookie;
		ddi_set_driver_private(dip, tfport);

		dev_err(dip, CE_NOTE, "%s() - success", __func__);
		return (DDI_SUCCESS);

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
tfport_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	tfport_t *tfp;

	dev_err(dip, CE_NOTE,"%s", __func__);
	switch (cmd) {
	case DDI_DETACH:
		tfp = (tfport_t *)ddi_get_driver_private(dip);
		ASSERT(tfp == tfport);
		if (tfp->tfp_refcnt > 0)
			return (DDI_FAILURE);

		dld_ioc_unregister(TFPORT_IOC);
		tofino_pkt_unregister(tfp->tfp_instance, tfp->tfp_pkt_cookie);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * Return the device associated with this port.  If no such device exists,
 * return the system device.
 */
static tfport_port_t *
tfport_find_port(tfport_t *tfp, int port)
{
	tfport_port_t *portp, *port0p;

	port0p = NULL;
	portp = list_head(&tfp->tfp_ports); 
	while (portp != NULL) {
		if (portp->tp_port == port) {
			return (portp);
		} else if (portp->tp_port == 0) {
			port0p = portp;
		}
		portp = list_next(&tfp->tfp_ports, portp);
	}

	return (port0p);
}

static int
tfport_tx_one(tfport_port_t *portp, mblk_t *mp_head)
{
	tfport_t *tfp = portp->tp_tfport;
	void *tf_cookie = tfp->tfp_pkt_cookie;
	caddr_t tx_buf, tx_wp;
	size_t full_sz = msgsize(mp_head);

	/*
	 * If this is from a port device, we need to insert a sidecar header
	 * after the ethernet header, so the ASIC knows which port the packet
	 * should egress.
	 */
	if (portp->tp_port != 0)
		full_sz += SCSZ;

	if ((tx_buf = tofino_tx_alloc(tf_cookie, full_sz)) == NULL) {
		return (-1);
	}

	tx_wp = tx_buf;

	/* Copy the ethernet header into the transfer buffer */
	bcopy(mp_head->b_rptr, tx_wp, ETHSZ);
	tx_wp += ETHSZ;

	/*
	 * If needed, construct the sidecar header and update the ethernet header
	 */
	if (portp->tp_port != 0) {
		struct ether_header *eth = (struct ether_header *)(tx_buf);
		struct schdr *sc = (struct schdr *)tx_wp;

		sc->sc_code = SC_FORWARD_FROM_USERSPACE;
		sc->sc_ingress = 0;
		sc->sc_egress = htons(portp->tp_port);
		sc->sc_ethertype = eth->ether_type;
		eth->ether_type = htons(ETHERTYPE_SIDECAR);
		tx_wp += SCSZ;
	}

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

	if (tofino_tx(tf_cookie, tx_buf, full_sz) != 0) {
		tofino_tx_free(tf_cookie, tx_buf);
		return (-1);
	}

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

static int
tfport_init_mac(tfport_t *dev, tfport_port_t *port)
{
	mac_register_t *mac;
	int rval;

	mac = mac_alloc(MAC_VERSION);
	if (mac == NULL) {
		return ENOMEM;
	}

	/* Register the new device with the mac(9e) framework */
	mac->m_driver = port;
	mac->m_dip = dev->tfp_dip;
	mac->m_instance = port->tp_port;
	mac->m_src_addr = port->tp_mac_addr;
	mac->m_callbacks = &tfport_m_callbacks;
	mac->m_min_sdu = 0;
	mac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mac->m_max_sdu = ETHERMTU;
	mac->m_margin = VLAN_TAGSZ;
	rval = mac_register(mac, &port->tp_mh);
	mac_free(mac);

	if (rval == 0) {
		mac_link_update(port->tp_mh, LINK_STATE_UP);
		mac_tx_update(port->tp_mh);
	} else {
		dev_err(dev->tfp_dip, CE_WARN, "failed to register port %d",
		    port->tp_port);
	}

	return (rval);
}

static void
get_mac_str(uchar_t *m, char *str)
{
	(void) sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
	    m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* ARGSUSED */
static int
tfport_ioc_create(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_create_t *carg = karg;
	tfport_t *dev = tfport;
	tfport_port_t *portp;
	char mac[20];
	int rval;

	if (carg->tic_port_id > 1024) {
		dev_err(dev->tfp_dip, CE_WARN, "invalid port-id");
		return (EINVAL);
	}

	if (carg->tic_mac_len == 0) {
		carg->tic_mac_len = ETHERADDRL;
		(void) random_get_pseudo_bytes(carg->tic_mac_addr, ETHERADDRL);
		/* Ensure MAC address is not multicast and is local */
		carg->tic_mac_addr[0] = (carg->tic_mac_addr[0] & ~1) | 2;
	} else if (carg->tic_mac_len < ETHERADDRL || carg->tic_mac_len > MAXMACADDRLEN) {
		dev_err(dev->tfp_dip, CE_WARN, "invalid macaddr");
		return (EINVAL);
	}
	get_mac_str(carg->tic_mac_addr, mac);

	portp = kmem_zalloc(sizeof (*portp), KM_NOSLEEP);
	if (portp == NULL)
		return (ENOMEM);

	/* XXX: refcnt? */
	portp->tp_tfport = dev;
	portp->tp_runstate = TPORT_RUNSTATE_STOPPED;
	portp->tp_port = carg->tic_port_id;
	portp->tp_link_id = carg->tic_link_id;
	portp->tp_mac_len = carg->tic_mac_len;
	bcopy(carg->tic_mac_addr, portp->tp_mac_addr, portp->tp_mac_len);
	portp->tp_ls = LINK_STATE_UNKNOWN;

	/* XXX: check for uniqueness before doing all this work.  Make td_mutex
	 * a rw lock
	 */
	if ((rval = tfport_init_mac(dev, portp)) != 0)
		goto out;

	if ((rval = dls_devnet_create(portp->tp_mh, portp->tp_link_id,
	    getzoneid())) != 0) {
		goto out;
	}

	mutex_init(&portp->tp_mutex, NULL, MUTEX_DRIVER, NULL);
	mutex_enter(&dev->tfp_mutex);
	list_insert_head(&dev->tfp_ports, portp);
	dev->tfp_refcnt++;
	mutex_exit(&dev->tfp_mutex);

out:
	if (rval != 0)
		kmem_free(portp, sizeof(*portp));

	return (rval);
}

static tfport_port_t *
tfport_find(tfport_t *dev, datalink_id_t link)
{
	tfport_port_t *portp;

	for (portp = list_head(&dev->tfp_ports);
	    portp != NULL && portp->tp_link_id != link;
	    portp = list_next(&dev->tfp_ports, portp))
	    ;

	dev_err(dev->tfp_dip, CE_NOTE, "link %d %sfound", 
	    link, portp == NULL ? "not " : "");

	return (portp);
}

/* ARGSUSED */
static int
tfport_ioc_delete(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_delete_t *darg = karg;
	tfport_t *dev = tfport;
	tfport_port_t *portp;
	datalink_id_t link = darg->tid_link_id;
	int rval = 0;

	mutex_enter(&dev->tfp_mutex);
	portp = tfport_find(dev, link);
	if (portp == NULL) {
		dev_err(dev->tfp_dip, CE_WARN, "deleting non-existant link: %d", link);
		rval = ENOENT;
	} else { 
		mutex_enter(&portp->tp_mutex);
		if (portp->tp_runstate != TPORT_RUNSTATE_STOPPED) {
			dev_err(dev->tfp_dip, CE_WARN, "port %d is busy", link);
			rval = EBUSY;
		} else {
			dev_err(dev->tfp_dip, CE_NOTE, "removing %d", link);
			list_remove(&dev->tfp_ports, portp);
			dev->tfp_refcnt--;
		}
		mutex_exit(&portp->tp_mutex);

	}
	mutex_exit(&dev->tfp_mutex);

	if (rval == 0) {
		datalink_id_t tmpid;

		dev_err(dev->tfp_dip, CE_NOTE, "cleaning up %d", link);
		if (dls_devnet_destroy(portp->tp_mh, &tmpid, B_TRUE) != 0)
			dev_err(dev->tfp_dip, CE_WARN, "error destroying devnet");
		ASSERT(portp->tp_link_id == tmpid);

		if (mac_unregister(portp->tp_mh) != 0)
			dev_err(dev->tfp_dip, CE_WARN, "error unregistering with mac");

		mutex_destroy(&portp->tp_mutex);
	}

	return (rval);
}


/* ARGSUSED */
static int
tfport_ioc_info(void *karg, intptr_t arg, int mode, cred_t *cred, int *rvalp)
{
	tfport_ioc_info_t *iarg = karg;
	tfport_t *dev = tfport;
	tfport_port_t *portp;
	datalink_id_t link = iarg->tii_link_id;
	int rval = 0;

	mutex_enter(&dev->tfp_mutex);
	portp = tfport_find(dev, link);
	if (portp == NULL) {
		dev_err(dev->tfp_dip, CE_NOTE, "returning ENOENT");
		rval = ENOENT;
	} else { 
		mutex_enter(&portp->tp_mutex);
		iarg->tii_port_id = portp->tp_port;
		iarg->tii_link_id = portp->tp_link_id;
		iarg->tii_mac_len = portp->tp_mac_len;
		bcopy(portp->tp_mac_addr, iarg->tii_mac_addr, portp->tp_mac_len);
		mutex_exit(&portp->tp_mutex);
	}
	mutex_exit(&dev->tfp_mutex);
	
	return (rval);
}

/*ARGSUSED*/
static void
tfport_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	cmn_err(CE_NOTE, "%s", __func__);
	miocnak(q, mp, 0, ENOTSUP);
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
		if (portp->tp_runstate == TPORT_RUNSTATE_RUNNING)
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

	portp->tp_runstate = TPORT_RUNSTATE_RUNNING;
	cmn_err(CE_NOTE, "%s(%d)", __func__, portp->tp_port);
	return (0);
}

static void
tfport_m_stop(void *arg)
{
	tfport_port_t *portp = arg;

	if (portp->tp_loaned_bufs == 0) {
		cmn_err(CE_NOTE, "%s(%d)", __func__, portp->tp_port);
		portp->tp_runstate = TPORT_RUNSTATE_STOPPED;
	} else {
		cmn_err(CE_NOTE, "%s(%d) - pending return of loaned bufs",
		    __func__, portp->tp_port);
		portp->tp_runstate = TPORT_RUNSTATE_STOPPING;
	}
}

static int
tfport_m_promisc(void *arg, boolean_t on)
{
	tfport_port_t *portp = arg;

	portp->tp_promisc = on;
	return (0);
}

/*ARGSUSED*/
static int
tfport_m_multicst(void *arg, boolean_t add, const uint8_t *addrp)
{
	return (0);
}

static int
tfport_m_unicst(void *arg, const uint8_t *macaddr)
{
	tfport_port_t *portp = arg;

	cmn_err(CE_NOTE, "%s", __func__);
	(void) memcpy(portp->tp_mac_addr, macaddr, ETHERADDRL);
	return (0);
}

static void
tfport_rx(void *arg, void *vaddr, size_t sz)
{
	struct ether_header *eth = NULL;
	struct schdr *sc = NULL;
	uint32_t port = 0;

	tfport_t *tfp = arg;
	caddr_t addr = (caddr_t)vaddr;
	size_t mblk_sz = sz;

	tfport_port_t *portp;
	mblk_t *mp;

	// dev_err(tfp->tfp_dip, CE_NOTE, "%s(%p, %ld)", __func__, addr, sz);
	if (sz < ETHSZ) {
		goto done;
	}

	/*
	 * Look for a sidecar header to determine whether the packet should be
	 * sent to a port device or the service device.
	*/
	eth = (struct ether_header *)addr;
	if (ntohs(eth->ether_type) == ETHERTYPE_SIDECAR) {
		if (sz < ETHSZ + SCSZ) {
			goto done;
		}
		sc = (struct schdr *)(addr + ETHSZ);
		if (sc->sc_code == SC_FORWARD_TO_USERSPACE) {
			port = ntohs(sc->sc_ingress);
		}
	}

	/*
	 * If the packet is going to a port device, we strip off the sidecar
	 * header.  If the packet should go to the service device, but that
	 * device doesn't exist, we drop it.
	 */
	portp = tfport_find_port(tfp, port);
	if (portp == NULL) {
		goto done;
	} else if (portp->tp_port != 0) {
		eth->ether_type = sc->sc_ethertype;
		mblk_sz -= SCSZ;
	}

	if ((mp = allocb(mblk_sz, 0)) == NULL) {
		dev_err(tfp->tfp_dip, CE_NOTE, "%s - allocb failed", __func__);
		goto done;
	}

	if (mblk_sz == sz) {
		bcopy(addr, mp->b_rptr, sz);
	} else {
		size_t body = mblk_sz - ETHSZ;
		bcopy(addr, mp->b_rptr, ETHSZ);
		bcopy(addr + ETHSZ + SCSZ, mp->b_rptr + ETHSZ, body);
	}

	mp->b_wptr = mp->b_rptr + mblk_sz;
	mac_rx(portp->tp_mh, NULL, mp);

done:
	tofino_rx_done(tfp->tfp_pkt_cookie, addr, mblk_sz);
}

static void
tfport_cmp(void)
{
	cmn_err(CE_NOTE, "%s()", __func__);
}

static struct modldrv tfport_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	TFPORTINFO,		/* short description */
	&tfport_dev_ops		/* driver specific ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &tfport_modldrv, NULL
};

static tfport_t *
tfport_dev_alloc()
{
	tfport_t *tfp;

	tfp = kmem_zalloc(sizeof (*tfp), KM_NOSLEEP);
	if (tfp != NULL) {
		mutex_init(&tfp->tfp_mutex, NULL, MUTEX_DRIVER, NULL);
		list_create(&tfp->tfp_ports, sizeof (tfport_port_t), 0);
	}
	return (tfp);
}

static void
tfport_dev_free(tfport_t *tfp)
{
	list_destroy(&tfp->tfp_ports);
	mutex_destroy(&tfp->tfp_mutex);
	kmem_free(tfp, sizeof (*tfp));
}

int
_init(void)
{
	static const char buildtime[] = "Built " __DATE__ " at " __TIME__ ;
	tfport_t *tfp;
	int status;

	cmn_err(CE_NOTE, "tfport:%s() - %s", __func__, buildtime);

	if ((tfp = tfport_dev_alloc()) == NULL)
		return (ENOMEM);

	mac_init_ops(&tfport_dev_ops, "tfport");
	status = mod_install(&modlinkage);
	if (status == DDI_SUCCESS) {
		tfport = tfp;
	} else {
		mac_fini_ops(&tfport_dev_ops);
		tfport_dev_free(tfp);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	if (tfport != NULL) {
		mutex_enter(&tfport->tfp_mutex);
		if (tfport->tfp_refcnt != 0) {
			mutex_exit(&tfport->tfp_mutex);
			return DDI_FAILURE;
		}
		mutex_exit(&tfport->tfp_mutex);
	}

	status = mod_remove(&modlinkage);
	if (status == DDI_SUCCESS)
		mac_fini_ops(&tfport_dev_ops);

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
