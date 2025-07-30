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
 * This file is part of the Chelsio T4 support code.
 *
 * Copyright (C) 2011-2013 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/queue.h>

#include "t4nex.h"
#include "common/common.h"
#include "common/t4_regs.h"
#ifdef ENABLE_CUDBG
#include "cudbg.h"
#endif

/* helpers */
static int pci_rw(struct adapter *sc, void *data, int flags, int write);
static int reg_rw(struct adapter *sc, void *data, int flags, int write);
static int regdump(struct adapter *sc, void *data, int flags);
static int get_devlog(struct adapter *sc, void *data, int flags);
static int flash_fw(struct adapter *, void *, int);
/* RPZ: The cudbg code might be moving to a firmware impl, ignore for now. */
#ifdef ENABLE_CUDBG
static int get_cudbg(struct adapter *, void *, int);
#endif

int
t4_ioctl(struct adapter *sc, int cmd, void *data, int mode)
{
	int rc = ENOTSUP;

	switch (cmd) {
	case T4_IOCTL_PCIGET32:
	case T4_IOCTL_PCIPUT32:
		rc = pci_rw(sc, data, mode, cmd == T4_IOCTL_PCIPUT32);
		break;
	case T4_IOCTL_GET32:
	case T4_IOCTL_PUT32:
		rc = reg_rw(sc, data, mode, cmd == T4_IOCTL_PUT32);
		break;
	case T4_IOCTL_REGDUMP:
		rc = regdump(sc, data, mode);
		break;
	case T4_IOCTL_DEVLOG:
		rc = get_devlog(sc, data, mode);
		break;
	case T4_IOCTL_LOAD_FW:
		rc = flash_fw(sc, data, mode);
		break;
#ifdef ENABLE_CUDBG
	case T4_IOCTL_GET_CUDBG:
		rc = get_cudbg(sc, data, mode);
		break;
#endif
	default:
		return (EINVAL);
	}

	return (rc);
}

static int
pci_rw(struct adapter *sc, void *data, int flags, int write)
{
	struct t4_reg32_cmd r;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0)
		return (EFAULT);

	/* address must be 32 bit aligned */
	r.reg &= ~0x3;

	if (write != 0) {
		pci_config_put32(sc->pci_regh, r.reg, r.value);
	} else {
		r.value = pci_config_get32(sc->pci_regh, r.reg);
		if (ddi_copyout(&r, data, sizeof (r), flags) < 0)
			return (EFAULT);
	}

	return (0);
}

static int
reg_rw(struct adapter *sc, void *data, int flags, int write)
{
	struct t4_reg32_cmd r;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0)
		return (EFAULT);

	/* Register address must be 32 bit aligned */
	r.reg &= ~0x3;

	if (write != 0)
		t4_write_reg(sc, r.reg, r.value);
	else {
		r.value = t4_read_reg(sc, r.reg);
		if (ddi_copyout(&r, data, sizeof (r), flags) < 0)
			return (EFAULT);
	}

	return (0);
}

/*
 * Return a version number to identify the type of adapter.  The scheme is:
 * - bits 0..9: chip version
 * - bits 10..15: chip revision
 * - bits 16..23: register dump version
 */
static inline unsigned int
mk_adap_vers(const struct adapter *sc)
{
	return (CHELSIO_CHIP_VERSION(sc->params.chip) |
	    (CHELSIO_CHIP_RELEASE(sc->params.chip) << 10) | (1 << 16));
}

static int
regdump(struct adapter *sc, void *data, int flags)
{
	struct t4_regdump r;

	if (ddi_copyin(data, &r, sizeof (r), flags) < 0) {
		return (EFAULT);
	}

	unsigned int reglen = t4_get_regs_len(sc);
	int rc = 0;
	if (r.len < reglen) {
		rc = ENOBUFS;
		goto out;
	}

	uint8_t *buf = kmem_zalloc(reglen, KM_SLEEP);
	t4_get_regs(sc, buf, reglen);
	if (ddi_copyout(buf, r.data, reglen, flags) < 0) {
		rc = EFAULT;
	}
	kmem_free(buf, reglen);

out:
	r.version = mk_adap_vers(sc);
	r.len = reglen;

	if (ddi_copyout(&r, data, sizeof (r), flags) < 0) {
		return (EFAULT);
	}

	return (rc);
}

static int
get_devlog(struct adapter *sc, void *data, int flags)
{
	struct devlog_params *dparams = &sc->params.devlog;
	struct t4_devlog dl = {0};
	int rc = 0;

	/* The devlog params have not been initialized yet. */
	if (dparams->size == 0)
		return (EIO);

	if (ddi_copyin(data, &dl, sizeof (dl), flags) < 0)
		return (EFAULT);

	dl.t4dl_ncores = sc->params.ncores;

	if (dl.t4dl_nentries < dparams->nentries) {
		dl.t4dl_nentries = dparams->nentries;
		rc = ddi_copyout(&dl, data, sizeof (dl), flags);
		return ((rc == 0) ? ENOBUFS : EFAULT);
	}

	dl.t4dl_nentries = dparams->nentries;
	const size_t len = dparams->nentries * sizeof (struct fw_devlog_e);
	struct fw_devlog_e *entries = kmem_zalloc(len, KM_NOSLEEP);

	if (entries == NULL)
		return (ENOMEM);

	rc = -t4_memory_rw(sc, sc->params.drv_memwin, dparams->memtype,
	    dparams->start, len, (void *)entries, T4_MEMORY_READ);
	if (rc != 0)
		goto done;

	/* Copyout device log buffer and then carrier buffer */
	if (ddi_copyout(entries, (void *)((uintptr_t)data + sizeof (dl)), len,
	    flags) < 0)
		rc = EFAULT;

	if (ddi_copyout(&dl, data, sizeof (dl), flags) < 0)
		rc = EFAULT;

done:
	kmem_free(entries, len);
	return (rc);
}

static int
flash_fw(struct adapter *sc, void *data, int flags)
{
	unsigned int mbox = M_PCIE_FW_MASTER + 1;
	struct t4_ldfw fw;
	u8 *ptr = NULL;
	int rc = 0;

	if (ddi_copyin(data, &fw, sizeof (struct t4_ldfw), flags) < 0)
		return (EFAULT);

	if (!fw.len)
		return (EINVAL);

	ptr = (u8 *)kmem_zalloc(fw.len, KM_NOSLEEP);
	if (ptr == NULL)
		return (ENOMEM);

	if (ddi_copyin((void *)((uintptr_t)data + sizeof (fw)), ptr, fw.len,
	    flags) < 0) {
		kmem_free(ptr, fw.len);
		return (EFAULT);
	}

	if (sc->flags & TAF_INIT_DONE)
		mbox = sc->mbox;

	rc = -t4_fw_upgrade(sc, mbox, ptr, fw.len, true);
	ddi_ufm_update(sc->ufm_hdl);

	kmem_free(ptr, fw.len);

	return (rc);
}

#ifdef ENABLE_CUDBG
static int
get_cudbg(struct adapter *sc, void *data, int flags)
{
	struct t4_cudbg_dump dump;
	struct cudbg_init *cudbg;
	void *handle, *buf;
	int size;
	int rc = 0;

	if (ddi_copyin(data, &dump, sizeof (struct t4_cudbg_dump), flags) < 0)
		return (EFAULT);

	size = dump.len;
	buf = (u8 *)kmem_zalloc(dump.len, KM_NOSLEEP);
	if (buf == NULL)
		return (ENOMEM);

	handle = cudbg_alloc_handle();
	if (handle == NULL) {
		rc = ENOMEM;
		goto free;
	}

	cudbg = cudbg_get_init(handle);
	cudbg->adap = sc;
	cudbg->print = cxgb_printf;

	ASSERT3U(sizeof (cudbg->dbg_bitmap), ==, sizeof (dump->bitmap));
	memcpy(cudbg->dbg_bitmap, dump.bitmap, sizeof (cudbg->dbg_bitmap));

	rc = cudbg_collect(handle, buf, &dump.len);
	if (rc != 0) {
		cxgb_printf(sc->dip, CE_WARN, "cudbg collect failed\n");
		goto exit;
	}

	if (ddi_copyout(buf, (void *)((uintptr_t)data + sizeof (dump)),
	    dump.len, flags) < 0) {
		rc = EFAULT;
	}

	if (ddi_copyout(&dump, data, sizeof (dump), flags) < 0) {
		rc = EFAULT;
	}
exit:
	cudbg_free_handle(handle);
free:
	kmem_free(buf, size);

	return (rc);
}
#endif
