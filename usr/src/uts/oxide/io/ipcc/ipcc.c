/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

/*
 * This file implements the 'ipcc' pseudo driver which provides a device node
 * and an ioctl() interface that can be used to issue requests to the service
 * processor via IPCC. More information on IPCC can be found in the block
 * comment in os/ipcc_proto.c
 *
 * Since all of the information we need for a transaction, including the
 * caller's cred, is available in the ioctl entry point, we don't keep track of
 * clients that have the device node open, preferring to construct and tear down
 * an ipcc_state_t state struct across the call into ipcc_proto.
 */

#include <sys/archsystm.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/policy.h>
#include <sys/sdt.h>
#include <sys/stat.h>
#include <sys/stdbool.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/sysmacros.h>
#include <sys/termios.h>
#include <sys/types.h>
#include <sys/gpio/dpio.h>

#include <sys/ipcc.h>
#include <sys/ipcc_proto.h>

#include <ipcc_debug.h>
#include <ipcc_drv.h>

/*
 * Globals
 */

static dev_info_t *ipcc_dip;
static char *ipcc_path, *ipcc_sp_intr_path;
static kstat_t *ipcc_kstat;
static ipcc_stats_t *ipcc_stat;

#define	BUMP_STAT(_s) atomic_inc_64(&(ipcc_stat->_s.value.ui64))

static int
ipcc_ldi_read(const ipcc_state_t *ipcc, ldi_handle_t ldih, uint8_t *buf,
    size_t *len)
{
	struct uio uio = { 0 };
	struct iovec iov = { 0 };
	int err;

	iov.iov_base = (int8_t *)buf;
	iov.iov_len = *len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_loffset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_resid = *len;

	err = ldi_read(ldih, &uio, ipcc->is_cred);
	if (err != 0)
		return (err);

	*len -= uio.uio_resid;
	return (0);
}

static int
ipcc_cb_read(void *arg, uint8_t *buf, size_t *len)
{
	const ipcc_state_t *ipcc = arg;

	return (ipcc_ldi_read(ipcc, ipcc->is_ldih, buf, len));
}

static int
ipcc_cb_write(void *arg, uint8_t *buf, size_t *len)
{
	const ipcc_state_t *ipcc = arg;
	struct uio uio;
	struct iovec iov;
	int err;

	bzero(&uio, sizeof (uio));
	bzero(&iov, sizeof (iov));
	iov.iov_base = (int8_t *)buf;
	iov.iov_len = *len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_loffset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_resid = *len;

	err = ldi_write(ipcc->is_ldih, &uio, ipcc->is_cred);
	if (err != 0)
		return (err);

	*len -= uio.uio_resid;
	return (0);
}

static void
ipcc_cb_flush(void *arg)
{
	const ipcc_state_t *ipcc = arg;

	(void) ldi_ioctl(ipcc->is_ldih, I_FLUSH, FLUSHRW, FKIOCTL,
	    ipcc->is_cred, NULL);
}

static int
ipcc_pause(uint64_t delay_ms)
{
	return (delay_sig(drv_usectohz(delay_ms * (MICROSEC / MILLISEC))));
}

static bool
ipcc_readable(void *arg)
{
	const ipcc_state_t *ipcc = arg;
	int err, rval;

	err = ldi_ioctl(ipcc->is_ldih, FIORDCHK, (intptr_t)NULL, FKIOCTL,
	    ipcc->is_cred, &rval);
	if (err != 0) {
		dev_err(ipcc_dip, CE_WARN,
		    "ioctl(FIORDCHK) failed, error %d", err);
		return (false);
	}

	return (rval > 0);
}

static bool
ipcc_writable(void *arg)
{
	const ipcc_state_t *ipcc = arg;
	int err, rval;

	err = ldi_ioctl(ipcc->is_ldih, I_CANPUT, 0, FKIOCTL, ipcc->is_cred,
	    &rval);
	if (err != 0 || rval == -1) {
		dev_err(ipcc_dip, CE_WARN,
		    "ioctl(I_CANPUT) failed, error %d, rval=%d", err, rval);
		return (false);
	}

	return (rval == 1);
}

static bool
ipcc_cb_readintr(void *arg)
{
	const ipcc_state_t *ipcc = arg;
	size_t len;
	int err;
	dpio_input_t val;

	if (!ipcc->is_sp_intr)
		return (false);

	len = sizeof (val);
	err = ipcc_ldi_read(ipcc, ipcc->is_sp_intr_ldih, (uint8_t *)&val, &len);

	if (err == 0 && len == sizeof (val)) {
		if (val == DPIO_INPUT_LOW) {
			BUMP_STAT(interrupts);
			return (true);
		}
		return (false);
	}

	dev_err(ipcc_dip, CE_WARN, "read_sp_intr got error %d, len %ld",
	    err, len);

	return (false);
}

static int
ipcc_cb_poll(void *arg, ipcc_pollevent_t ev, ipcc_pollevent_t *revp,
    uint64_t timeout_ms)
{
	ipcc_pollevent_t rev = 0;
	uint64_t elapsed = 0;
	uint64_t delay = 10;
	uint_t loops = 0;

	for (;;) {
		int ret;

		if ((ev & IPCC_INTR) != 0 && ipcc_cb_readintr(arg))
			rev |= IPCC_INTR;
		if ((ev & IPCC_POLLIN) != 0 && ipcc_readable(arg))
			rev |= IPCC_POLLIN;
		if ((ev & IPCC_POLLOUT) != 0 && ipcc_writable(arg))
			rev |= IPCC_POLLOUT;
		if (rev != 0)
			break;

		if ((ret = ipcc_pause(delay)) != 0)
			return (ret);
		elapsed += delay;
		if (timeout_ms > 0 && elapsed >= timeout_ms)
			return (ETIMEDOUT);

		/*
		 * Every 10 loops, double the delay to allow a longer
		 * sleep between retries, and more time off CPU, up to a
		 * maximum of 0.1s.
		 */
		if (++loops % 10 == 0)
			delay = MAX(delay << 1, 100);

		/*
		 * If we're under a timeout, sleep only for as long as is
		 * remaining, but clamp to at least 10ms.
		 */
		if (timeout_ms > 0)
			delay = MIN(10, MAX(delay, timeout_ms - elapsed));
	}

	*revp = rev;
	return (0);
}

static int
ipcc_cb_open(void *arg)
{
	ipcc_state_t *ipcc = arg;
	char mbuf[FMNAMESZ + 1];
	int err;

	VERIFY0(ipcc->is_open);

	VERIFY0(ldi_ident_from_dev(ipcc->is_dev, &ipcc->is_ldiid));

	err = ldi_open_by_name(ipcc_path, LDI_FLAGS,
	    ipcc->is_cred, &ipcc->is_ldih, ipcc->is_ldiid);
	if (err != 0) {
		ldi_ident_release(ipcc->is_ldiid);
		dev_err(ipcc_dip, CE_WARN,
		    "ldi open of '%s' failed", ipcc_path);
		return (err);
	}

	/*
	 * Whilst there is nothing expected to be autopushed on the DWU UART,
	 * check and pop anything that is. This also allows easier testing on
	 * commodity hardware.
	 */
	while (ldi_ioctl(ipcc->is_ldih, I_LOOK, (intptr_t)mbuf, FKIOCTL,
	    ipcc->is_cred, NULL) == 0) {
		ipcc_dbgmsg(NULL, 0, "Popping module %s", mbuf);
		err = ldi_ioctl(ipcc->is_ldih, I_POP, 0, FKIOCTL,
		    ipcc->is_cred, NULL);
		if (err != 0) {
			dev_err(ipcc_dip, CE_WARN, "Failed to pop module %s",
			    mbuf);
			VERIFY0(ldi_close(ipcc->is_ldih, LDI_FLAGS,
			    ipcc->is_cred));
			ldi_ident_release(ipcc->is_ldiid);
			return (err);
		}
	}

	ipcc->is_open = true;

	/*
	 * Currently failure to open the interrupt DPIO is not fatal.
	 */
	ipcc->is_sp_intr = false;
	if (ipcc_sp_intr_path != NULL) {
		err = ldi_open_by_name(ipcc_sp_intr_path, LDI_SP_INTR_FLAGS,
		    ipcc->is_cred, &ipcc->is_sp_intr_ldih, ipcc->is_ldiid);
		if (err != 0) {
			dev_err(ipcc_dip, CE_WARN,
			    "ldi open of '%s' failed", ipcc_sp_intr_path);
		} else {
			ipcc->is_sp_intr = true;
		}
	}

	return (0);
}

static void
ipcc_cb_close(void *arg)
{
	ipcc_state_t *ipcc = arg;
	VERIFY(ipcc->is_open);

	if (ipcc->is_sp_intr) {
		VERIFY0(ldi_close(ipcc->is_sp_intr_ldih, LDI_SP_INTR_FLAGS,
		    ipcc->is_cred));
	}

	ipcc->is_sp_intr = ipcc->is_open = false;
	VERIFY0(ldi_close(ipcc->is_ldih, LDI_FLAGS, ipcc->is_cred));
	ldi_ident_release(ipcc->is_ldiid);
}

static const ipcc_ops_t ipcc_ops = {
	.io_open	= ipcc_cb_open,
	.io_close	= ipcc_cb_close,
	.io_readintr	= ipcc_cb_readintr,
	.io_poll	= ipcc_cb_poll,
	.io_flush	= ipcc_cb_flush,
	.io_read	= ipcc_cb_read,
	.io_write	= ipcc_cb_write,
	.io_log		= ipcc_dbgmsg,
};

static int
ipcc_open(dev_t *devp, int flag, int otyp, cred_t *cr)
{
	int err;

	BUMP_STAT(opens);

	if (getminor(*devp) != IPCC_MINOR) {
		BUMP_STAT(opens_fail);
		return (ENXIO);
	}

	if (otyp != OTYP_CHR) {
		BUMP_STAT(opens_fail);
		return (EINVAL);
	}

	if ((flag & (FNDELAY | FNONBLOCK | FEXCL)) != 0) {
		BUMP_STAT(opens_fail);
		return (EINVAL);
	}

	if (crgetzoneid(cr) != GLOBAL_ZONEID) {
		BUMP_STAT(opens_fail);
		return (EPERM);
	}

	/*
	 * XXX For now we require that the caller has the SYS_CONFIG privilege.
	 */
	if ((err = secpolicy_sys_config(cr, B_FALSE)) != 0) {
		BUMP_STAT(opens_fail);
		return (err);
	}

	return (0);
}

static int
ipcc_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	if (getminor(dev) != IPCC_MINOR)
		return (ENXIO);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	return (0);
}

static int
ipcc_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *cr, int *rv)
{
	void *datap = (void *)data;
	ipcc_state_t ipcc = { 0 };
	uint_t model;
	int cflag;
	int err = 0;

	if (getminor(dev) != IPCC_MINOR)
		return (ENXIO);

	if ((mode & FREAD) == 0)
		return (EBADF);

	model = ddi_model_convert_from(mode & FMODELS);
	cflag = mode & FKIOCTL;

	switch (cmd) {
	case IPCC_GET_VERSION:
		BUMP_STAT(ioctl_version);
		*rv = IPCC_DRIVER_VERSION;
		return (0);
	}

	ipcc.is_dev = dev;
	ipcc.is_cred = cr;

	switch (cmd) {
	case IPCC_STATUS: {
		ipcc_status_t status;

		BUMP_STAT(ioctl_status);
		err = ipcc_status(&ipcc_ops, &ipcc,
		    &status.is_status, &status.is_startup);
		if (err != 0)
			break;

		if (ddi_copyout(&status, datap, sizeof (status), cflag) != 0)
			err = EFAULT;

		break;
	}
	case IPCC_IDENT: {
		ipcc_ident_t ident;

		BUMP_STAT(ioctl_ident);
		err = ipcc_ident(&ipcc_ops, &ipcc, &ident);
		if (err != 0)
			break;

		if (ddi_copyout(&ident, datap, sizeof (ident), cflag) != 0)
			err = EFAULT;

		break;
	}
	case IPCC_MACS: {
		ipcc_mac_t mac;

		BUMP_STAT(ioctl_macs);

		err = ipcc_macs(&ipcc_ops, &ipcc, &mac);
		if (err != 0)
			break;

		if (ddi_copyout(&mac, datap, sizeof (mac), cflag) != 0)
			err = EFAULT;

		break;
	}
	case IPCC_KEYLOOKUP: {
		ipcc_keylookup_t kl;
#ifdef _MULTI_DATAMODEL
		ipcc_keylookup32_t kl32;
#endif
		uint8_t *buf;

		BUMP_STAT(ioctl_keylookup);

		switch (model) {
#ifdef _MULTI_DATAMODEL
		case DDI_MODEL_ILP32:
			if (ddi_copyin(datap, &kl32, sizeof (kl32), cflag) != 0)
				return (EFAULT);

			bzero(&kl, sizeof (kl));
			kl.ik_key = kl32.ik_key;
			kl.ik_buflen = kl32.ik_buflen;
			kl.ik_buf = (uint8_t *)(uintptr_t)kl32.ik_buf;
			break;
#endif /* _MULTI_DATAMODEL */
		case DDI_MODEL_NONE:
			if (ddi_copyin(datap, &kl, sizeof (kl), cflag) != 0)
				return (EFAULT);
			break;
		default:
			return (ENOTSUP);
		}

		if (kl.ik_buflen == 0 ||
		    kl.ik_buflen > IPCC_KEYLOOKUP_MAX_PAYLOAD) {
			err = EINVAL;
			break;
		}

		buf = kmem_zalloc(kl.ik_buflen, KM_SLEEP);

		err = ipcc_keylookup(&ipcc_ops, &ipcc, &kl, buf);
		if (err != 0)
			goto keylookup_done;

		if (kl.ik_datalen > kl.ik_buflen) {
			err = EOVERFLOW;
			goto keylookup_done;
		}

		if (ddi_copyout(buf, kl.ik_buf, kl.ik_datalen, cflag) != 0) {
			err = EFAULT;
			goto keylookup_done;
		}

		switch (model) {
#ifdef _MULTI_DATAMODEL
		case DDI_MODEL_ILP32:
			kl32.ik_datalen = kl.ik_datalen;
			kl32.ik_result = kl.ik_result;
			if (ddi_copyout(&kl32, datap, sizeof (kl32),
			    cflag) != 0) {
				err = EFAULT;
			}
			break;
#endif /* _MULTI_DATAMODEL */
		case DDI_MODEL_NONE:
			if (ddi_copyout(&kl, datap, sizeof (kl), cflag) != 0)
				err = EFAULT;
			break;
		default:
			return (ENOTSUP);
		}

keylookup_done:
		kmem_free(buf, kl.ik_buflen);
		break;
	}
	case IPCC_ROT: {
		ipcc_rot_t *rot;

		BUMP_STAT(ioctl_rot);
		rot = kmem_zalloc(sizeof (*rot), KM_SLEEP);

		if (ddi_copyin(datap, rot, sizeof (*rot), cflag) != 0) {
			err = EFAULT;
			goto rot_done;
		}

		err = ipcc_rot(&ipcc_ops, &ipcc, rot);
		if (err != 0)
			goto rot_done;

		if (ddi_copyout(rot, datap, sizeof (*rot), cflag) != 0)
			err = EFAULT;

rot_done:
		kmem_free(rot, sizeof (*rot));
		break;
	}
	case IPCC_IMAGEBLOCK: {
		ipcc_imageblock_t ib;
#ifdef _MULTI_DATAMODEL
		ipcc_imageblock32_t ib32;
#endif
		uint8_t *data;
		size_t datal;

		switch (model) {
#ifdef _MULTI_DATAMODEL
		case DDI_MODEL_ILP32:
			if (ddi_copyin(datap, &ib32, sizeof (ib32), cflag) != 0)
				return (EFAULT);

			bzero(&ib, sizeof (ib));
			bcopy(ib32.ii_hash, ib.ii_hash, sizeof (ib32.ii_hash));
			ib.ii_offset = ib32.ii_offset;
			ib.ii_buflen = ib32.ii_buflen;
			ib.ii_buf = (uint8_t *)(uintptr_t)ib32.ii_buf;
			break;
#endif /* _MULTI_DATAMODEL */
		case DDI_MODEL_NONE:
			if (ddi_copyin(datap, &ib, sizeof (ib), cflag) != 0)
				return (EFAULT);
			break;
		default:
			return (ENOTSUP);
		}

		if (ib.ii_buflen == 0 || ib.ii_buflen > IPCC_MAX_DATA_SIZE) {
			err = EINVAL;
			break;
		}

		err = ipcc_acquire_channel(&ipcc_ops, &ipcc);
		if (err != 0)
			break;
		err = ipcc_imageblock(&ipcc_ops, &ipcc, ib.ii_hash,
		    ib.ii_offset, &data, &datal);
		if (err != 0)
			goto imageblock_done;

		datal = MIN(datal, ib.ii_buflen);

		if (datal > 0) {
			if (ddi_copyout(data, ib.ii_buf, datal, cflag) != 0) {
				err = EFAULT;
				goto imageblock_done;
			}
		}

		ib.ii_datalen = datal;

		switch (model) {
#ifdef _MULTI_DATAMODEL
		case DDI_MODEL_ILP32:
			ib32.ii_datalen = ib.ii_datalen;
			if (ddi_copyout(&ib32, datap, sizeof (ib32),
			    cflag) != 0) {
				err = EFAULT;
			}
			break;
#endif /* _MULTI_DATAMODEL */
		case DDI_MODEL_NONE:
			if (ddi_copyout(&ib, datap, sizeof (ib), cflag) != 0)
				err = EFAULT;
			break;
		default:
			return (ENOTSUP);
		}

imageblock_done:
		ipcc_release_channel(&ipcc_ops, &ipcc, true);
		break;
	}
	case IPCC_INVENTORY: {
		ipcc_inventory_t *inv;

		BUMP_STAT(ioctl_inventory);

		inv = kmem_zalloc(sizeof (*inv), KM_NOSLEEP_LAZY);
		if (inv == NULL) {
			err = ENOMEM;
			break;
		}

		if (ddi_copyin(datap, inv, offsetof(ipcc_inventory_t,
		    iinv_res), cflag) != 0) {
			err = EFAULT;
			goto inventory_done;
		}

		err = ipcc_inventory(&ipcc_ops, &ipcc, inv);
		if (err != 0)
			goto inventory_done;

		if (ddi_copyout(inv, datap, sizeof (*inv), cflag) != 0)
			err = EFAULT;
inventory_done:
		kmem_free(inv, sizeof (*inv));
		break;
	}
	case IPCC_KEYSET: {
		ipcc_keyset_t *kset;

		BUMP_STAT(ioctl_keyset);

		kset = kmem_zalloc(sizeof (*kset), KM_NOSLEEP_LAZY);
		if (kset == NULL) {
			err = ENOMEM;
			break;
		}

		if (ddi_copyin(datap, kset, sizeof (*kset), cflag) != 0) {
			err = EFAULT;
			goto keyset_done;
		}

		err = ipcc_keyset(&ipcc_ops, &ipcc, kset);
		if (err != 0)
			goto keyset_done;

		/*
		 * We only need to copy out the result, which is the first
		 * field of the struct, placed before iks_key.
		 */
		if (ddi_copyout(kset, datap, offsetof(ipcc_keyset_t, iks_key),
		    cflag) != 0) {
			err = EFAULT;
		}
keyset_done:
		kmem_free(kset, sizeof (*kset));
		break;
	}
	default:
		BUMP_STAT(ioctl_unknown);
		err = ENOTTY;
		break;
	}

	VERIFY0(ipcc.is_open);
	return (err);
}

static void
ipcc_cleanup(dev_info_t *dip)
{
	ipcc_dip = NULL;
	if (ipcc_kstat != NULL) {
		kstat_delete(ipcc_kstat);
		ipcc_kstat = NULL;
		ipcc_stat = NULL;
	}
	if (ipcc_path != NULL) {
		strfree(ipcc_path);
		ipcc_path = NULL;
	}
	if (ipcc_sp_intr_path != NULL) {
		strfree(ipcc_sp_intr_path);
		ipcc_sp_intr_path = NULL;
	}
	ddi_remove_minor_node(dip, NULL);
}

static int
ipcc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	const struct {
		const char *impl;
		const char *path;
	} path_lookup[] = {
		{ "Oxide,Gimlet", "/devices/huashan@0,0/dwu@1:0,cu", },
	};

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	/* We only allow a single instance. */
	if (ddi_get_instance(dip) != 0) {
		dev_err(dip, CE_WARN, "Asked to attach non-zero instance");
		return (DDI_FAILURE);
	}

	if (ipcc_dip != NULL) {
		dev_err(dip, CE_WARN, "Asked to attach a second instance");
		return (DDI_FAILURE);
	}

	if (ddi_create_minor_node(dip, IPCC_NODE_NAME, S_IFCHR,
	    IPCC_MINOR, DDI_PSEUDO, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "Unable to create minor node");
		return (DDI_FAILURE);
	}

	/*
	 * Use persistent kstats so they are not lost over a module unload/load.
	 */
	ipcc_kstat = kstat_create(IPCC_DRIVER_NAME, 0, "statistics",
	    "misc", KSTAT_TYPE_NAMED,
	    sizeof (ipcc_stats_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT);
	if (ipcc_kstat == NULL) {
		dev_err(dip, CE_WARN, "kstat_create failed");
		goto fail;
	}
	ipcc_stat = (ipcc_stats_t *)ipcc_kstat->ks_data;

	kstat_named_init(&ipcc_stat->opens, "total_opens",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->opens_fail, "total_open_failures",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->interrupts, "total_interrupts",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_version, "total_version_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_status, "total_status_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_ident, "total_ident_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_macs, "total_mac_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_keylookup, "total_keylookup_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_rot, "total_rot_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_inventory, "total_inventory_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_keyset, "total_keyset_req",
	    KSTAT_DATA_UINT64);
	kstat_named_init(&ipcc_stat->ioctl_unknown, "total_unknown_req",
	    KSTAT_DATA_UINT64);
	kstat_install(ipcc_kstat);

	/* Check if there is an override path defined in the driver conf */
	char *path;
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    IPCC_PROP_PATH, &path) == DDI_PROP_SUCCESS) {
		ipcc_path = ddi_strdup(path, KM_SLEEP);
		ddi_prop_free(path);
	} else {
		const char *impl = ddi_node_name(ddi_root_node());

		for (uint_t i = 0; i < ARRAY_SIZE(path_lookup); i++) {
			if (strcmp(impl, path_lookup[i].impl) == 0) {
				ipcc_path = ddi_strdup(path_lookup[i].path,
				    KM_SLEEP);
				break;
			}
		}
		if (ipcc_path == NULL) {
			dev_err(dip, CE_WARN, "Could not determine uart path");
			goto fail;
		}
	}

	if ((ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    IPCC_PROP_SP_INTR_PATH, &path)) != DDI_PROP_SUCCESS) {
		dev_err(dip, CE_WARN, "Could not retrieve '%s' property",
		    IPCC_PROP_SP_INTR_PATH);
		goto fail;
	} else {
		ipcc_sp_intr_path = ddi_strdup(path, KM_SLEEP);
		ddi_prop_free(path);
	}

	ipcc_dbgmsg_init();
	ddi_report_dev(dip);

	ipcc_dbgmsg(NULL, 0, "Using UART device '%s'", ipcc_path);
	ipcc_dbgmsg(NULL, 0, "Using SP interrupt DPIO '%s'",
	    ipcc_sp_intr_path != NULL ? ipcc_sp_intr_path : "NONE");

	ipcc_dip = dip;
	return (DDI_SUCCESS);

fail:
	ipcc_cleanup(dip);
	return (DDI_FAILURE);
}

static int
ipcc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	ipcc_cleanup(dip);
	ipcc_dbgmsg_fini();

	return (DDI_SUCCESS);
}

static int
ipcc_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
	case DDI_INFO_DEVT2INSTANCE:
		if (getminor((dev_t)arg) != IPCC_MINOR)
			return (DDI_FAILURE);
		break;
	default:
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = ipcc_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)ddi_get_instance(ipcc_dip);
		break;
	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static struct cb_ops ipcc_cb_ops = {
	.cb_open		= ipcc_open,
	.cb_close		= ipcc_close,
	.cb_strategy		= nulldev,
	.cb_print		= nulldev,
	.cb_dump		= nodev,
	.cb_read		= nodev,
	.cb_write		= nodev,
	.cb_ioctl		= ipcc_ioctl,
	.cb_devmap		= nodev,
	.cb_mmap		= nodev,
	.cb_segmap		= nodev,
	.cb_chpoll		= nochpoll,
	.cb_prop_op		= ddi_prop_op,
	.cb_str			= NULL,
	.cb_flag		= D_MP,
	.cb_rev			= CB_REV,
	.cb_aread		= nodev,
	.cb_awrite		= nodev,
};

static struct dev_ops ipcc_dev_ops = {
	.devo_rev		= DEVO_REV,
	.devo_refcnt		= 0,
	.devo_getinfo		= ipcc_info,
	.devo_identify		= nulldev,
	.devo_probe		= nulldev,
	.devo_attach		= ipcc_attach,
	.devo_detach		= ipcc_detach,
	.devo_reset		= nodev,
	.devo_cb_ops		= &ipcc_cb_ops,
	.devo_bus_ops		= NULL,
	.devo_power		= nodev,
	.devo_quiesce		= ddi_quiesce_not_needed,
};

static struct modldrv ipcc_modldrv = {
	.drv_modops		= &mod_driverops,
	.drv_linkinfo		= "SP/Host Comms Driver",
	.drv_dev_ops		= &ipcc_dev_ops
};

static struct modlinkage ipcc_modlinkage = {
	.ml_rev			= MODREV_1,
	.ml_linkage		= {
		&ipcc_modldrv,
		NULL
	}
};

int
_init(void)
{
	return (mod_install(&ipcc_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&ipcc_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&ipcc_modlinkage));
}
