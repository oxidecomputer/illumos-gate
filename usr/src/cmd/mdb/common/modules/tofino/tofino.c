/*
 * Copyright 2022 Oxide Computer Company
 */

#include <sys/mdb_modapi.h>
#include <sys/tofino.h>
#include <sys/sunndi.h>
#include <sys/tofino_impl.h>

/*ARGSUSED*/
static int
count_bufs(uintptr_t addr, const void *foo, void *cb_arg)
{
	uint32_t *cnt = cb_arg;
	(*cnt)++;
	return (0);
}

static int
dcmd_tfdr(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	tfpkt_dr_t dr;
	char *drt;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("\nUsage: address::tfdr\n");
		return (DCMD_ERR);
	}

	if (mdb_vread(&dr, sizeof (dr), addr) != sizeof (dr)) {
		mdb_warn("\nfailed to read tfpkt_dr_t");
		return (DCMD_ERR);
	}

	switch (dr.tfdrp_type) {
		case TF_PKT_DR_TX: drt = "TX"; break;
		case TF_PKT_DR_CMP: drt = "CMP"; break;
		case TF_PKT_DR_FM: drt = "FM"; break;
		case TF_PKT_DR_RX: drt = "RX"; break;
		default: drt = "BAD";
	}

	if (!(flags & DCMD_LOOP) || (flags & DCMD_LOOPFIRST)) {
		mdb_printf("%8s  %7s  %4s  %-16s  %5s  %6s  %6s\n",
		    "NAME", "CFG REG", "TYPE", "ADDR", "DEPTH", "HEAD", "TAIL");
	}
	mdb_printf("%8s  %07llx  %-4s  %016llx  %5d  %6llx  %6llx\n",
	    dr.tfdrp_name, dr.tfdrp_reg_base, drt, dr.tfdrp_virt_base,
	    dr.tfdrp_depth, dr.tfdrp_head, dr.tfdrp_tail);

	return (DCMD_OK);
}

typedef struct tfdr_walk_state_s {
	int tfdr_num;
	int tfdr_idx;
	uintptr_t *tfdr_addrs;
} tfdr_walk_state_t;

static int
tfdr_walk_init(mdb_walk_state_t *wsp)
{
	tfpkt_t tfpkt;
	tfdr_walk_state_t *tp = NULL;
	uintptr_t *addrs = NULL;
	int num, idx;
	size_t sz;

	if (wsp->walk_addr == 0) {
		mdb_warn("\nUsage: <tfpkt address>::tfdr\n");
		return (WALK_ERR);
	}

	if (mdb_vread(&tfpkt, sizeof (tfpkt), wsp->walk_addr) != sizeof (tfpkt)) {
		mdb_warn("\nfailed to read tfpkt_t");
		goto fail;
	}

	num = TF_PKT_CMP_CNT + TF_PKT_FM_CNT + TF_PKT_TX_CNT + TF_PKT_RX_CNT;
	sz = num * sizeof (uintptr_t);

	if ((addrs = (uintptr_t *)mdb_alloc(sz, 0)) == NULL) {
		mdb_warn("\nout of memory\n");
		goto fail;
	}

	if ((tp = (tfdr_walk_state_t *)mdb_alloc(sizeof (*tp), 0)) == NULL) {
		mdb_warn("\nout of memory\n");
		goto fail;
	}
	tp->tfdr_idx = 0;
	tp->tfdr_num = num;
	tp->tfdr_addrs = addrs;

	idx = 0;
	for (int i = 0; i < TF_PKT_RX_CNT; i++)
		addrs[idx++] = (uintptr_t)(&tfpkt.tfp_rx_drs[i]);
	for (int i = 0; i < TF_PKT_FM_CNT; i++)
		addrs[idx++] = (uintptr_t)(&tfpkt.tfp_fm_drs[i]);
	for (int i = 0; i < TF_PKT_TX_CNT; i++)
		addrs[idx++] = (uintptr_t)(&tfpkt.tfp_tx_drs[i]);
	for (int i = 0; i < TF_PKT_CMP_CNT; i++)
		addrs[idx++] = (uintptr_t)(&tfpkt.tfp_cmp_drs[i]);

	wsp->walk_data = tp;
	return (WALK_NEXT);

fail:
	if (tp != NULL)
		mdb_free(tp, sizeof (*tp));
	if (addrs != NULL)
		mdb_free(addrs, sz);

	return (WALK_ERR);
}

static int
tfdr_walk_step(mdb_walk_state_t *wsp)
{
	tfdr_walk_state_t *tp = wsp->walk_data;
	uintptr_t drp;
	tfpkt_dr_t dr;

	if (tp->tfdr_idx >= tp->tfdr_num)
		return (WALK_DONE);

	drp = tp->tfdr_addrs[tp->tfdr_idx++];
	if (mdb_vread(&dr, sizeof (dr), drp) != sizeof (dr)) {
		mdb_warn("\nfailed to read DR at %llx\n", drp);
		return (WALK_ERR);
	}
	return (wsp->walk_callback((uintptr_t)drp, &dr, wsp->walk_cbdata));
}

static void
tfdr_walk_fini(mdb_walk_state_t *wsp)
{
	tfdr_walk_state_t *tp = wsp->walk_data;
	size_t sz;

	if (tp != NULL) {
		sz = tp->tfdr_num * sizeof (uintptr_t);
		mdb_free(tp->tfdr_addrs, sz);
		mdb_free(tp, sizeof (*tp));
	}
}

static int
dcmd_tfpkt(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	tfpkt_t tfpkt, *ptr;
	uintptr_t rxf, rxp, rxl, txf, txp, txl;
	uint32_t rx_free, rx_pushed, rx_loaned, tx_free, tx_pushed, tx_loaned;

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("\nUsage: address::tfpkt\n");
		return (DCMD_ERR);
	}

	if (mdb_vread(&tfpkt, sizeof (tfpkt), addr) != sizeof (tfpkt)) {
		mdb_warn("\nfailed to read tfpkt_t");
		return (DCMD_ERR);
	}

	ptr = (tfpkt_t *)addr;
	rxf = (uintptr_t)&ptr->tfp_rxbufs_free;
	rxp = (uintptr_t)&ptr->tfp_rxbufs_pushed;
	rxl = (uintptr_t)&ptr->tfp_rxbufs_loaned;
	txf = (uintptr_t)&ptr->tfp_txbufs_free;
	txp = (uintptr_t)&ptr->tfp_txbufs_pushed;
	txl = (uintptr_t)&ptr->tfp_txbufs_loaned;

	if (mdb_pwalk("list", count_bufs, &rx_free, rxf) < 0) {
		mdb_warn("\nfailed to count rxbufs_free");
	}
	if (mdb_pwalk("list", count_bufs, &rx_pushed, rxp) < 0) {
		mdb_warn("\nfailed to count rxbufs_pushed");
	}
	if (mdb_pwalk("list", count_bufs, &rx_loaned, rxl) < 0) {
		mdb_warn("\nfailed to count rxbufs_loaned");
	}
	if (mdb_pwalk("list", count_bufs, &tx_free, txf) < 0) {
		mdb_warn("\nfailed to count txbufs_free");
	}
	if (mdb_pwalk("list", count_bufs, &tx_pushed, txp) < 0) {
		mdb_warn("\nfailed to count txbufs_pushed");
	}
	if (mdb_pwalk("list", count_bufs, &tx_loaned, rxl) < 0) {
		mdb_warn("\nfailed to count txbufs_loaned");
	}
	mdb_printf("pkt hander:  %p\n", tfpkt.tfp_pkt_hdlr);
	mdb_printf("rx freelist: %llx (%3d bufs)\n", rxf, rx_free);
	mdb_printf("rx pushed:   %llx (%3d bufs)\n", rxp, rx_pushed);
	mdb_printf("rx loaned:   %llx (%3d bufs)\n", rxl, rx_loaned);
	mdb_printf("tx freelist: %llx (%3d bufs)\n", txf, tx_free);
	mdb_printf("tx pushed:   %llx (%3d bufs)\n", txp, tx_pushed);
	mdb_printf("tx loaned:   %llx (%3d bufs)\n", txl, tx_loaned);

	return (DCMD_OK);
}

static int
tofino_walk_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr != 0) {
		mdb_warn("tofino walk does not support local walks\n");
		return (WALK_ERR);
	}

	if (mdb_readvar(&wsp->walk_addr, "tofino_statep") == -1) {
		mdb_warn("failed to read 'tofino_statep'");
		return (WALK_ERR);
	}

	if (mdb_layered_walk("softstate", wsp) == -1) {
		mdb_warn("cannot walk tofino_state");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
tofino_walk_step(mdb_walk_state_t *wsp)
{
	tofino_devstate_t tds;
	struct dev_info dev_info;
	tofino_t tf;
	uintptr_t tfp;

	mdb_vread(&tds, sizeof(tds), wsp->walk_addr);
	mdb_vread(&dev_info, sizeof(dev_info), (uintptr_t)(tds.tds_dip));
	tfp = (uintptr_t)(dev_info.devi_driver_data);
	mdb_vread(&tf, sizeof(tf), tfp);

	return (wsp->walk_callback(tfp, &tf, wsp->walk_cbdata));
}

static const mdb_dcmd_t dcmds[] = {
	{ "tfpkt", ":", "tofino packet handler state", dcmd_tfpkt},
	{ "tfdr", ":", "tofino descriptor ring", dcmd_tfdr},
	{ NULL }
};

static const mdb_walker_t walkers[] = {
	{ "tofino", "walk list of tofino devices",
		tofino_walk_init, tofino_walk_step , NULL},
	{ "tfdr", "walk all DRs in a tfpkt_t",
		tfdr_walk_init, tfdr_walk_step , tfdr_walk_fini},
	{ NULL },

};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, dcmds, walkers
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
