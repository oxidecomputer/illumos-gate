/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Oxide Computer Company
 */

/*
 * MDB support for the t4nex/cxgbe driver.
 */
#include <mdb/mdb_ctf.h>
#include <sys/mdb_modapi.h>
#include <sys/strsun.h>
#include "common/common.h"
#include "common/t4_msg.h"

#define	MAC_IMPL_WALK	"mac_impl_cache"

typedef struct mdb_mac_impl {
	char mi_name[32];
	void *mi_driver;
} mdb_mac_impl_t;

/* RPZ rename this cxgbe_iq_entry_64B. Actually, that's wrong, there
 * are three types of 64B entries between my event queues and rx
 * queues */
typedef struct cxgbe_rxq_entry_64b {
	struct rss_header	cre64_rss_header;
	struct cpl_rx_pkt	cre64_cpl_rx_pkt;
	uint8_t			cred_pad[24];
	struct rsp_ctrl		cre64_rsp_ctrl;
} cxgbe_rxq_entry_64b_t;

CTASSERT(sizeof (cxgbe_rxq_entry_64b_t) == 64);

typedef struct mdb_sge_iq_stats {
	uint64_t sis_processed;
} mdb_sge_iq_stats_t;

typedef struct mdb_t4_sge_iq {
	uintptr_t	desc;
	uintptr_t	cdesc;
	uint16_t	esize;
	uint16_t	qsize;
	/* uint16_t	tsi_cap; */
	uint8_t		gen;
	uintptr_t	adapter;
	mdb_sge_iq_stats_t	stats;
} mdb_t4_sge_iq_t;

typedef struct mdb_port_info {
	struct adapter *adapter;
	uintptr_t mh;
	int mtu;
	uint16_t nrxq;
	uint16_t first_rxq;
	uint16_t ntxq;
	uint16_t first_txq;
	kstat_t *ksp_info;
} mdb_port_info_t;

typedef struct mdb_sge_info {
	struct sge_rxq *rxq;
	struct sge_txq *txq;
} mdb_sge_info_t;

typedef struct mdb_adapter_params {
	unsigned char nports;
} mdb_adapter_params_t;

typedef struct mdb_adapter {
	mdb_sge_info_t sge;
        mdb_adapter_params_t params;
	struct port_info *port[MAX_NPORTS];
} mdb_adapter_t;

typedef struct mdb_cxgbe_port_info_kstats {
	kstat_named_t rx_ovflow0;
	kstat_named_t rx_ovflow1;
	kstat_named_t rx_ovflow2;
	kstat_named_t rx_ovflow3;
} mdb_cxgbe_port_info_kstats_t;

typedef struct mdb_rxbuf {
	caddr_t va;
	uint_t buf_size;
} mdb_rxbuf_t;

typedef enum walk_qtype {
	WALK_QTYPE_TX,
	WALK_QTYPE_RX,
} walk_qtype_t;

typedef struct walk_queue_data {
	bool		wqd_once;
	walk_qtype_t	wqd_type;
} walk_queue_data_t;

static int
walk_adap_init(mdb_walk_state_t *ws)
{
	if (ws->walk_addr != 0) {
		mdb_warn("'cxgbe_adap' is a global walker only");
		return (WALK_ERR);
	}

	GElf_Sym sym;

	if (mdb_lookup_by_name("t4_adapter_list", &sym) == -1) {
		mdb_warn("Failed to lookup symbol 't4_adapter_list'\n");
		return (DCMD_ERR);
	}

	ws->walk_addr = sym.st_value;
	if (mdb_layered_walk("list", ws) == -1) {
		mdb_warn("failed to walk 'list'");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
walk_adap_step(mdb_walk_state_t *ws)
{
	if (ws->walk_addr == 0)
		return (WALK_DONE);

	struct adapter adap = {0};

	if (mdb_vread(&adap, sizeof (adap), ws->walk_addr) == -1) {
		mdb_warn("failed to read t4nex`adapter at %p", ws->walk_addr);
		return (WALK_ERR);
	}

	int ret = ws->walk_callback(ws->walk_addr, &adap, ws->walk_cbdata);
	return (ret);
}

static int
walk_cxgbe_init(mdb_walk_state_t *ws)
{
	if (ws->walk_addr != 0) {
		mdb_warn("'cxgbe' is a global walker only");
		return (WALK_ERR);
	}

	if (mdb_layered_walk(MAC_IMPL_WALK, ws) == -1) {
		mdb_warn("failed to walk 'cxgbe'");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
walk_cxgbe_step(mdb_walk_state_t *ws)
{
	if (ws->walk_addr == 0)
		return (WALK_DONE);

	mdb_mac_impl_t mi = {0};
 	int ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
	    ws->walk_addr, 0);

	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", ws->walk_addr);
		return (WALK_ERR);
	}

	if (strncmp("cxgbe", mi.mi_name, 5) != 0)
		return (WALK_NEXT);

	mdb_port_info_t pi = {0};
	ret = mdb_ctf_vread(&pi, "struct t4nex`port_info", "mdb_port_info_t",
	    (uintptr_t)mi.mi_driver, 0);

	if (ret != 0) {
		mdb_warn("failed to read t4nex`port_info at %p", mi.mi_driver);
		return (WALK_ERR);
	}

	ret = ws->walk_callback((uintptr_t)mi.mi_driver, &pi, ws->walk_cbdata);
	return (ret);
}

static int
walk_queue_init(mdb_walk_state_t *ws, walk_qtype_t wtype)
{
	walk_queue_data_t *data = mdb_zalloc(sizeof (walk_queue_data_t),
	    UM_SLEEP);
	ws->walk_data = data;
	data->wqd_type = wtype;

	if (ws->walk_addr == 0) {
		data->wqd_once = false;
		int ret = mdb_layered_walk("cxgbe", ws);

		if (ret != 0) {
			mdb_warn("couldn't walk 'cxgbe'");
			return (ret);
		}
	} else {
		data->wqd_once = true;
	}

	return (WALK_NEXT);
}

static void
walk_queue_fini(mdb_walk_state_t *ws)
{
	mdb_free(ws->walk_data, sizeof (walk_queue_data_t));
}

static int
walk_queue_step(mdb_walk_state_t *ws)
{
	walk_queue_data_t *data = ws->walk_data;

	if (ws->walk_addr == 0)
		return (WALK_DONE);

	mdb_port_info_t pi = {0};
	int ret = mdb_ctf_vread(&pi, "struct t4nex`port_info",
	    "mdb_port_info_t", ws->walk_addr, 0);

	if (ret != 0) {
		mdb_warn("failed to read port_info at %p", ws->walk_addr);
		return (WALK_ERR);
	}

	mdb_adapter_t adapter = {0};
	ret = mdb_ctf_vread(&adapter, "struct t4nex`adapter", "mdb_adapter_t",
	    (uintptr_t)pi.adapter, 0);

	if (ret != 0) {
		mdb_warn("failed to read adapter at %p", pi.adapter);
		return (WALK_ERR);
	}

	uintptr_t base = 0;
	size_t size = 0;
	uint16_t count = 0;

	switch (data->wqd_type) {
	case WALK_QTYPE_TX:
		base = (uintptr_t)&adapter.sge.txq[pi.first_txq];
		size = sizeof (adapter.sge.txq[0]);
		count = pi.ntxq;
		break;
	case WALK_QTYPE_RX:
		base = (uintptr_t)&adapter.sge.rxq[pi.first_rxq];
		size = sizeof (adapter.sge.rxq[0]);
		count = pi.nrxq;
		break;
	}

	for (uint16_t i = 0; i < count; i++) {
		uintptr_t qaddr = base + (size * i);

		switch (data->wqd_type) {
		case WALK_QTYPE_TX: {
			struct sge_txq txq = {0};

			if (mdb_vread(&txq, sizeof (txq), qaddr) == -1) {
				mdb_warn("failed to read sge_txq at %p", qaddr);
				return (ret);
			}

			ret = ws->walk_callback(qaddr, &txq, ws->walk_cbdata);

			break;
		}
		case WALK_QTYPE_RX: {
			struct sge_rxq rxq = {0};

			if (mdb_vread(&rxq, sizeof (rxq), qaddr) == -1) {
				mdb_warn("failed to read sge_rxq at %p", qaddr);
				return (ret);
			}

			ret = ws->walk_callback(qaddr, &rxq, ws->walk_cbdata);

			break;
		}
		}

		if (ret != WALK_NEXT)
			return (ret);
	}

	if (data->wqd_once)
		return (WALK_DONE);

	return (ret);
}

static int
walk_rxq_init(mdb_walk_state_t *ws)
{
	return (walk_queue_init(ws, WALK_QTYPE_RX));
}

static int
walk_txq_init(mdb_walk_state_t *ws)
{
	return (walk_queue_init(ws, WALK_QTYPE_TX));
}

typedef struct walk_txq_sdesc_data {
	struct tx_sdesc *wtsd_sdesc; /* sge_txq.sdesc */
	uint16_t wtsd_tse_pidx;	    /* sge_txq.eq.tse_pidx */
	uint16_t wtsd_tse_cidx;	    /* sge_txq.eq.tse_cidx */
	uint16_t wtsd_tse_qsize;    /* sge_txq.eq.tse_qsize */
	struct tx_sdesc wtsd_entry;  /* copy of current entry */
} walk_txq_sdesc_data_t;

static int
walk_txq_sdesc_init(mdb_walk_state_t *ws)
{
	if (ws->walk_addr == 0) {
		mdb_warn("must specify cxgbe sge_txq to walk");
		return (WALK_ERR);
	}

	struct sge_txq txq = {0};

	if (mdb_vread(&txq, sizeof (txq), ws->walk_addr) == -1) {
		mdb_warn("failed to read sge_txq at %p", ws->walk_addr);
		return (WALK_ERR);
	}

	walk_txq_sdesc_data_t *data = mdb_zalloc(sizeof (*data), UM_SLEEP);
	data->wtsd_sdesc = txq.sdesc;
	data->wtsd_tse_pidx = txq.eq.pidx;
	data->wtsd_tse_cidx = txq.eq.cidx;
	data->wtsd_tse_qsize = txq.eq.qsize;

	ws->walk_data = data;
	ws->walk_addr = (uintptr_t)&data->wtsd_sdesc[data->wtsd_tse_cidx];

	return (WALK_NEXT);
}

static int
walk_txq_sdesc_step(mdb_walk_state_t *ws)
{
	walk_txq_sdesc_data_t *data = ws->walk_data;
	uintptr_t addr = ws->walk_addr;

	if (data->wtsd_tse_cidx == data->wtsd_tse_pidx) {
		return (WALK_DONE);
	}

	int ret = mdb_vread(&data->wtsd_entry, sizeof (data->wtsd_entry), addr);

	if (ret == -1) {
		mdb_warn("failed to read tx_sdesc at %p", addr);
		return (WALK_ERR);
	}

	ret = ws->walk_callback(addr, &data->wtsd_entry, ws->walk_cbdata);
	data->wtsd_tse_cidx += data->wtsd_entry.desc_used;

	if (data->wtsd_tse_cidx >= data->wtsd_tse_qsize)
		data->wtsd_tse_cidx -= data->wtsd_tse_qsize;

	ws->walk_addr = (uintptr_t)&data->wtsd_sdesc[data->wtsd_tse_cidx];

	return (ret);
}

static void
walk_txq_sdesc_fini(mdb_walk_state_t *ws)
{
	mdb_free(ws->walk_data, sizeof (walk_txq_sdesc_data_t));
}

/* --- START IQ ENTRY WALKTER */
typedef struct walk_iq_ent_data {
	mdb_t4_sge_iq_t		wied_iq;
	uintptr_t		wied_last;  /* desc + (tsi_cap * wred_sz) */
	uintptr_t		wied_start; /* starting desc */
	cxgbe_rxq_entry_64b_t	wied_entry;   /* holder for current entry */
	size_t			wied_visited; /* entries visited */
	bool			wied_pending; /* pending entries only */
} walk_iq_ent_data_t;

static int
walk_iq_ent_common_init(mdb_walk_state_t *ws, bool pending)
{
	uintptr_t addr = ws->walk_addr;

	if (ws->walk_addr == 0) {
		mdb_warn("must specify t4_sge_iq_t to walk");
		return (WALK_ERR);
	}

	walk_iq_ent_data_t *data = mdb_zalloc(sizeof (walk_iq_ent_data_t),
	    UM_SLEEP);
	mdb_t4_sge_iq_t *iq = &data->wied_iq;
	int ret = mdb_ctf_vread(iq, "sge_iq", "mdb_t4_sge_iq_t", addr, 0);

	if (ret != 0) {
		mdb_warn("failed to read sge_iq at %p", addr);
		return (WALK_ERR);
	}

	uint16_t tsi_cap = iq->qsize - 1;
	data->wied_last = iq->desc + (iq->esize * tsi_cap);
	data->wied_pending = pending;
	data->wied_visited = 0;

	if (pending)
		data->wied_start = (uintptr_t)iq->cdesc;
	else
		data->wied_start = (uintptr_t)iq->desc;

	ws->walk_data = data;
	ws->walk_addr = data->wied_start;

	return (WALK_NEXT);
}

static int
walk_iq_ent_init(mdb_walk_state_t *ws)
{
	return (walk_iq_ent_common_init(ws, false));
}

static int
walk_iq_pend_ent_init(mdb_walk_state_t *ws)
{
	return (walk_iq_ent_common_init(ws, true));
}

static bool
is_pending(uintptr_t entry_addr, cxgbe_rxq_entry_64b_t *entry,
    uintptr_t cdesc_addr, uint8_t gen)
{
	uint8_t egen =
	    (entry->cre64_rsp_ctrl.u.type_gen & F_RSPD_GEN) >> S_RSPD_GEN;
	/* mdb_warn("entry_addr=0x%p cdesc_addr=0x%p egen=%u gen=%u\n", */
	/*     entry_addr, cdesc_addr, egen, gen); */

	return ((entry_addr >= cdesc_addr && egen == gen) ||
	    (entry_addr < cdesc_addr && egen != gen));
}

static int
walk_iq_ent_step(mdb_walk_state_t *ws)
{
	uintptr_t addr = ws->walk_addr;
	walk_iq_ent_data_t *data = ws->walk_data;

	if (addr == 0 || (addr == data->wied_start && data->wied_visited > 0)) {
		return (WALK_DONE);
	}

	/* RPZ This entry type is actually incorrect, there are
	 * several types of IQ entry types */
	cxgbe_rxq_entry_64b_t *entry = &data->wied_entry;
	mdb_t4_sge_iq_t *iq = &data->wied_iq;

	if (mdb_vread(entry, iq->esize, addr) == -1) {
		mdb_warn("failed to read rxq entry bytes at %p", addr);
		return (WALK_DONE);
	}

	bool p = is_pending(addr, entry, iq->cdesc, iq->gen);
	int ret = 0;

	if (!data->wied_pending || p) {
		ret = ws->walk_callback(addr, entry, ws->walk_cbdata);

		if (ret != WALK_NEXT)
			return (ret);
	}

	data->wied_visited++;
	addr += iq->esize;

	if (addr > data->wied_last) {
		/*
		 * RPZ I need to find other places where I need to add
		 * newline to mdb_warn (places where I don't want it
		 * to automatically include summary about failed
		 * previous command.
		 */
		mdb_warn("address past end %p > %p\n", addr, data->wied_last);
		return (WALK_ERR);
	}

	if (addr == data->wied_last)
		addr = iq->desc;

	ws->walk_addr = addr;
	return (ret);
}

static void
walk_iq_ent_fini(mdb_walk_state_t *ws)
{
	mdb_free(ws->walk_data, sizeof (walk_iq_ent_data_t));
}
/* --- END IQ ENTRY WALKTER */

typedef struct walk_rxq_ent_data {
	size_t			wred_sz; /* esize */
	uintptr_t		wred_first; /* desc */
	uintptr_t		wred_last;  /* desc + (tsi_cap * wred_sz) */
	uintptr_t		wred_cdesc; /* cdesc */
	uintptr_t		wred_start; /* starting desc */
	uint8_t			wred_gen; /* gen */
	cxgbe_rxq_entry_64b_t	wred_entry;   /* holder for current entry */
	size_t			wred_visited; /* entries visited */
	bool			wred_pend_only; /* pending entries only */
} walk_rxq_ent_data_t;

static int
walk_rxq_ent_common_init(mdb_walk_state_t *ws, bool pending)
{
	struct sge_rxq rxq = {0};
	walk_rxq_ent_data_t *data = NULL;

	if (ws->walk_addr == 0) {
		mdb_warn("must specify cxgbe sge_rxq to walk");
		return (WALK_ERR);
	}

	if (mdb_vread(&rxq, sizeof (rxq), ws->walk_addr) == -1) {
		mdb_warn("failed to read sge_rxq at %p", ws->walk_addr);
		return (WALK_ERR);
	}


	data = mdb_zalloc(sizeof (walk_rxq_ent_data_t), UM_SLEEP);
	data->wred_sz = rxq.iq.esize;
	data->wred_first = (uintptr_t)rxq.iq.desc;
	uint16_t tsi_cap = rxq.iq.qsize - 1;
	data->wred_last = data->wred_first + (data->wred_sz * tsi_cap);
	data->wred_cdesc = (uintptr_t)rxq.iq.cdesc;
	data->wred_gen = rxq.iq.gen;
	data->wred_pend_only = pending;
	data->wred_visited = 0;

	if (pending)
		data->wred_start = (uintptr_t)rxq.iq.cdesc;
	else
		data->wred_start = data->wred_first;

	ws->walk_data = data;
	ws->walk_addr = data->wred_start;

	/* mdb_warn("wred_sz=%u wred_first=0x%p wred_last=%p wred_cdesc=%p " */
	/*     "wred_gen=0x%x\n", data->wred_sz, data->wred_first, data->wred_last, */
	/*     data->wred_cdesc, data->wred_gen); */

	return (WALK_NEXT);
}

static int
walk_rxq_ent_init(mdb_walk_state_t *ws)
{
	return (walk_rxq_ent_common_init(ws, false));
}

/* static int */
/* walk_rxq_pend_ent_init(mdb_walk_state_t *ws) */
/* { */
/* 	return (walk_rxq_ent_common_init(ws, true)); */
/* } */

static int
walk_rxq_ent_step(mdb_walk_state_t *ws)
{
	int ret = 0;
	uintptr_t addr = ws->walk_addr;
	walk_rxq_ent_data_t *data = ws->walk_data;

	if (addr == 0 || (addr == data->wred_start && data->wred_visited > 0)) {
		return (WALK_DONE);
	}

	if (mdb_vread(&data->wred_entry, data->wred_sz, addr) == -1) {
		mdb_warn("failed to read rxq entry bytes at %p", addr);
		return (WALK_DONE);
	}

	data->wred_visited++;
	ws->walk_addr += data->wred_sz;

	if (ws->walk_addr > data->wred_last) {
		/*
		 * RPZ I need to find other places where I need to add
		 * newline to mdb_warn (places where I don't want it
		 * to automatically include summary about failed
		 * previous command.
		 */
		mdb_warn("address past end %p > %p\n", ws->walk_addr,
		    data->wred_last);
	}

	if (ws->walk_addr == data->wred_last)
		ws->walk_addr = data->wred_first;

	bool p = is_pending(addr, &data->wred_entry, data->wred_cdesc,
	    data->wred_gen);

	if (data->wred_pend_only && !p)
		return (WALK_NEXT);

	ret = ws->walk_callback(addr, &data->wred_entry, ws->walk_cbdata);
	return (ret);
}

static void
walk_rxq_ent_fini(mdb_walk_state_t *ws)
{
	mdb_free(ws->walk_data, sizeof (walk_rxq_ent_data_t));
}

typedef struct walk_rxq_pdata_data {
	uint16_t		wrpd_esize;
	cxgbe_rxq_entry_64b_t	wrpd_entry;
	int			wrpd_mtu;      /* port_info.mtu */
	/* uint_t			wrpd_fl_align; /\* adapter.sge.fl_align *\/ */
	uint32_t		wrpd_offset; /* rxq.fl.offset */
	uint16_t		wrpd_tse_cidx; /* rxq.fl.eq.tse_cidx */
	uint16_t		wrpd_tse_qsize;
	uint8_t			wrpd_cidx_sdesc;
	struct fl_sdesc *	wrpd_sdesc;
	uint8_t			*wrpd_pdata;
	size_t			wrpd_max;
	size_t			wrpd_pdata_off;
	/* size_t			wrpd_wptr; /\* write pos *\/ */
} walk_rxq_pdata_data_t;

/*
 * RPZ For now I'm defining this here. I would then redefine it in
 * pcap_mdb.c or whever I put the dcmd code to generate a pcap file.
 * Writing this walker has made me realize mdb might want a
 * "generator" abstraction. It has a similar API to a walker, except
 * each step is generating new data to be consumed. You would then
 * have "consumer" command that are typed, and consume generators.
 * Each existing walker could be considered a consumer of a
 * `virtual_address_t`. But this all feels kind of complicated.
 *
 * SHIT This can't work. I mean, I could probably abuse it, but the
 * point of dcmds/pipelines is that what's being shared is an address
 * in the target/object file to read/do something with; not the
 * address of memory allocated by the debugger. E.g., if the pending
 * data walker allocated a bunch of debugger memory for each packet,
 * who frees it? It was to live long enough for the dcmd on the other
 * side to read those objects, but they get passed as uintpt_t/"void
 * *" values, I can't just assume they are pointers to a specific type
 * of data and then mdb_free() them. 
 */
/* typedef struct mdb_packet_data { */
/* 	uint8_t *mpd_data; */
/* 	size_t mpd_len; */
/* } mdb_packet_data_t; */

/* static int */
/* walk_rxq_pdata_init(mdb_walk_state_t *ws) */
/* { */
/* 	int ret = 0; */
/* 	struct sge_rxq rxq = {0}; */
/* 	mdb_port_info_t pi = {0}; */
/* 	mdb_adapter_t adapter = {0}; */
/* 	walk_rxq_pdata_data_t *data = NULL; */

/* 	if (ws->walk_addr == 0) { */
/* 		mdb_warn("must specify cxgbe sge_rxq to walk"); */
/* 		return (WALK_ERR); */
/* 	} */

/* 	if (mdb_vread(&rxq, sizeof (rxq), ws->walk_addr) == -1) { */
/* 		mdb_warn("failed to read sge_rxq at %p", ws->walk_addr); */
/* 		return (WALK_ERR); */
/* 	} */

/* 	ret = mdb_ctf_vread(&pi, "struct t4nex`port_info", "mdb_port_info_t", */
/* 	    (uintptr_t)rxq.port, 0); */

/* 	if (ret != 0) { */
/* 		return (WALK_ERR); */
/* 	} */

/* 	ret = mdb_ctf_vread(&adapter, "struct t4nex`adapter", "mdb_adapter_t", */
/* 	    (uintptr_t)pi.adapter, 0); */

/* 	if (ret != 0) { */
/* 		return (WALK_ERR); */
/* 	} */

/* 	data = mdb_zalloc(sizeof (walk_rxq_pdata_data_t), UM_SLEEP); */
/* 	data->wrpd_esize = rxq.iq.esize; */
/* 	data->wrpd_fl_align = adapter.sge.fl_align; */
/* 	data->wrpd_offset = rxq.fl.offset; */
/* 	data->wrpd_tse_cidx = rxq.fl.eq.tse_cidx; */
/* 	data->wrpd_tse_qsize = rxq.fl.eq.tse_qsize; */
/* 	data->wrpd_cidx_sdesc = rxq.fl.cidx_sdesc; */
/* 	data->wrpd_sdesc = rxq.fl.sdesc; */
/* 	data->wrpd_mtu = pi.mtu; */
/* 	/\* */
/* 	 * RPZ Instead or hard-coding 14 we should query the mi margin */
/* 	 * or some other field to determine max size. */
/* 	 *\/ */
/* 	data->wrpd_max = pi.mtu + 14; */
/* 	data->wrpd_pdata = mdb_zalloc(data->wrpd_max, UM_SLEEP); */
/* 	data->wrpd_pdata_off = 0; */

/* 	ws->walk_data = data; */

/* 	mdb_layered_walk("cxgbe_rxq_pending_entries", ws); */

/* 	return (WALK_NEXT); */
/* } */

/* static void */
/* t4_fl_advance_cix(uint16_t *tse_cidx, uint8_t *cidx_sdesc, */
/*     const uint16_t tse_qsize) */
/* { */
/* 	if (*cidx_sdesc >= FL_BUF_PTR_PER_HC) { */
/* 		mdb_warn("fl->cidx_sdesc >= FL_BUF_PTR_PER_HC (%u >= %u)", */
/* 		    *cidx_sdesc, FL_BUF_PTR_PER_HC); */
/* 	} */

/* 	if (*tse_cidx >= tse_qsize) { */
/* 		mdb_warn("eq->tse_cidx >= eq->tse_qsize (%u >= %u)", *tse_cidx, */
/* 		    tse_qsize); */
/* 	} */

/* 	(*cidx_sdesc)++; */

/* 	if (*cidx_sdesc == FL_BUF_PTR_PER_HC) { */
/* 		*cidx_sdesc = 0; */
/* 		(*tse_cidx)++; */

/* 		if (*tse_cidx == tse_qsize) { */
/* 			*tse_cidx = 0; */
/* 		} */
/* 	} */
/* } */

/* static int */
/* t4_fl_sdesc(struct fl_sdesc *out, const struct fl_sdesc *arr, */
/*     const uint_t eq_idx, const uint_t sdesc_idx) */
/* { */
/* 	if (sdesc_idx >= FL_BUF_PTR_PER_HC) { */
/* 		mdb_warn("sdesc_idx >= FL_BUF_PTR_PER_HC (%u >= %u)", sdesc_idx, */
/* 		    FL_BUF_PTR_PER_HC); */
/* 	} */

/* 	const uint_t idx = (eq_idx * FL_BUF_PTR_PER_HC) + sdesc_idx; */
/* 	/\* uintptr_t src = &arr[idx]; *\/ */
/* 	const uintptr_t src = (uintptr_t)(arr + idx); */
/* 	int ret = mdb_vread(out, sizeof (*out), src); */

/* 	if (ret == - 1) { */
/* 		mdb_warn("failed to read struct fl_desc at %p", src); */
/* 		return (ret); */
/* 	} */

/* 	return (0); */
/* } */

/*
 * RPZ We should remember the fl desc + offset we start at and check
 * to make sure we do not cross it while iterating data, and warn when
 * we do that this could be a sign of trouble
 */
/* static int */
/* walk_rxq_pdata_step(mdb_walk_state_t *ws) */
/* { */
/* 	int ret = 0; */
/* 	uintptr_t addr = ws->walk_addr; */
/* 	walk_rxq_pdata_data_t *data = ws->walk_data; */
/* 	cxgbe_rxq_entry_64b_t *entry = &data->wrpd_entry; */

/* 	if (addr == 0) { */
/* 		return (WALK_DONE); */
/* 	} */

/* 	if (mdb_vread(entry, data->wrpd_esize, addr) == -1) { */
/* 		mdb_warn("failed to read rxq entry bytes at %p\n", addr); */
/* 		return (WALK_DONE); */
/* 	} */

/* 	bzero(data->wrpd_pdata, data->wrpd_max); */

/* 	uint32_t dlen_nb = 0; */
/* 	mdb_nhconvert(&dlen_nb, &entry->cre64_rsp_ctrl.pldbuflen_qid, */
/* 	    sizeof (dlen_nb)); */

/* 	const bool newbuf = (dlen_nb & F_RSPD_NEWBUF); */
/* 	const uint32_t data_len = G_RSPD_LEN(dlen_nb); */

/* 	if (data_len > data->wrpd_max) { */
/* 		mdb_warn("data_len > max (%u > %u)", data_len, data->wrpd_max); */
/* 	} */

/* 	if (newbuf) { */
/* 		/\* */
/* 		 * Check for a condition which I believe shouldn't be */
/* 		 * possible. */
/* 		 *\/ */
/* 		if (data->wrpd_offset == 0) { */
/* 			mdb_warn("newbuf true AND offset == 0"); */
/* 		} */

/* 		data->wrpd_offset = 0; */

/* 		t4_fl_advance_cix(&data->wrpd_tse_cidx, &data->wrpd_cidx_sdesc, */
/* 		    data->wrpd_tse_qsize); */

/* 		/\* if (data->wrpd_cidx_sdesc >= FL_BUF_PTR_PER_HC) { *\/ */
/* 		/\* 	mdb_warn("fl->cidx_sdesc >= FL_BUF_PTR_PER_HC " *\/ */
/* 		/\* 	    "(%u >= %u)", data->wrpd_cidx_sdesc, *\/ */
/* 		/\* 	    FL_BUF_PTR_PER_HC); *\/ */
/* 		/\* } *\/ */

/* 		/\* if (data->wrpd_tse_cidx >= data->wrpd_tse_qsize) { *\/ */
/* 		/\* 	mdb_warn("eq->tse_cidx >= eq->tse_qsize (%u >= %u)", *\/ */
/* 		/\* 	    data->wrpd_tse_cidx, data->wrpd_tse_qsize); *\/ */
/* 		/\* } *\/ */

/* 		/\* data->wrpd_cidx_sdesc++; *\/ */

/* 		/\* if (data->wrpd_cidx_sdesc == FL_BUF_PTR_PER_HC) { *\/ */
/* 		/\* 	data->wrpd_cidx_sdesc = 0; *\/ */
/* 		/\* 	data->wrpd_tse_cidx++; *\/ */

/* 		/\* 	if (data->wrpd_tse_cidx == data->wrpd_tse_qsize) { *\/ */
/* 		/\* 		data->wrpd_tse_cidx == 0; *\/ */
/* 		/\* 	} *\/ */
/* 		/\* } *\/ */
/* 	} */

/* 	uint32_t len = data_len; */
/* 	while (len != 0) { */
/* 		struct fl_sdesc fl_sdesc = {0}; */
/* 		ret = t4_fl_sdesc(&fl_sdesc, data->wrpd_sdesc, */
/* 		    data->wrpd_tse_cidx, data->wrpd_cidx_sdesc); */

/* 		if (ret != 0) */
/* 			return (WALK_ERR); */

/* 		mdb_rxbuf_t rxb = {0}; */
/* 		ret = mdb_ctf_vread(&rxb, "struct rxbuf", "mdb_rxbuf_t", */
/* 		    (uintptr_t)fl_sdesc.rxb, 0); */

/* 		if (ret != 0) { */
/* 			mdb_warn("failed to read struct rxbuf at %p", */
/* 			    fl_sdesc.rxb); */
/* 			return (WALK_ERR); */
/* 		} */

/* 		if (data->wrpd_offset >= rxb.buf_size) { */
/* 			mdb_warn("(a) offset >= rxb->buf_size (%u >= %u)", */
/* 			    data->wrpd_offset, rxb.buf_size); */
/* 		} */

/* 		const uint_t copy_len = MIN(data_len, */
/* 		    rxb.buf_size - data->wrpd_offset); */
/* 		const uintptr_t data_src = */
/* 		    (uintptr_t)(rxb.va + data->wrpd_offset); */
/* 		void *data_dst = */
/* 		    (void *)(data->wrpd_pdata + data->wrpd_pdata_off); */

/* 		ret = mdb_vread(data_dst, copy_len, data_src); */

/* 		if (ret == -1) { */
/* 			mdb_warn("failed to read packet data at %p", data_src); */
/* 			return (WALK_ERR); */
/* 		} */

/* 		data->wrpd_pdata_off += copy_len; */
/* 		len -= copy_len; */
/* 		data->wrpd_offset += roundup(copy_len, data->wrpd_fl_align); */

/* 		if (data->wrpd_offset >= rxb.buf_size) { */
/* 			mdb_warn("(b) offset >= rxb->buf_size (%u >= %u)", */
/* 			    data->wrpd_offset, rxb.buf_size); */
/* 		} */

/* 		if (data->wrpd_offset == rxb.buf_size) { */
/* 			data->wrpd_offset = 0; */
/* 			t4_fl_advance_cix(&data->wrpd_tse_cidx, */
/* 			    &data->wrpd_cidx_sdesc, data->wrpd_tse_qsize); */
/* 		} */
/* 	} */

/* 	/\* */
/* 	 * N.B. We are passing the address of debugger-allocated */
/* 	 * memory, not a target address. */
/* 	 *\/ */
/* 	ret = ws->walk_callback((uintptr_t)data->wrpd_pdata, &data->wrpd_pdata, */
/* 	    ws->walk_cbdata); */

/* 	return (ret); */
/* } */

/* static void */
/* walk_rxq_pdata_fini(mdb_walk_state_t *ws) */
/* { */
/* 	walk_rxq_pdata_data_t *data = ws->walk_data; */

/* 	mdb_free(data->wrpd_pdata, data->wrpd_max); */
/* 	mdb_free(data, sizeof (walk_rxq_pdata_data_t)); */
/* } */

static int
cxgbe(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("cxgbe", "cxgbe", argc, argv) == -1) {
			mdb_warn("failed to walk cxgbe");
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-18s %-6s %-5s %-5s %-9s\n", "LINK", "ADDR",
		    "MTU", "TXQ", "RXQ", "DROPS");
	}

	mdb_port_info_t pi = {0};
	int ret = mdb_ctf_vread(&pi, "struct port_info", "mdb_port_info_t",
	    addr, 0);

	if (ret != 0) {
		mdb_warn("failed to read port_info at %p", addr);
		return (DCMD_ERR);
	}

	kstat_t ks = {0};
	if (mdb_vread(&ks, sizeof (ks), (uintptr_t)pi.ksp_info) == -1) {
		mdb_warn("failed to read kstat_t at %p", pi.ksp_info);
		return (DCMD_ERR);
	}

	mdb_cxgbe_port_info_kstats_t stats = {0};
	ret = mdb_ctf_vread(&stats, "struct cxgbe_port_info_kstats",
	    "mdb_cxgbe_port_info_kstats_t", (uintptr_t)ks.ks_data, 0);

	if (ret != 0) {
		mdb_warn("failed to read cxgbe_port_info_kstats at %p",
		    ks.ks_data);
		return (DCMD_ERR);
	}

	uint64_t drops = stats.rx_ovflow0.value.ui64;
	drops += stats.rx_ovflow1.value.ui64;
	drops += stats.rx_ovflow2.value.ui64;
	drops += stats.rx_ovflow3.value.ui64;

	mdb_mac_impl_t mi = {0};
	ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t", pi.mh, 0);
	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", pi.mh);
		return (DCMD_ERR);
	}

	mdb_printf("%-7s 0x%p %-6d %-5u %-5u %-9u\n", mi.mi_name, addr, pi.mtu,
	    pi.ntxq, pi.nrxq, drops);

	return (DCMD_OK);
}

typedef struct sum_pending_state {
	struct sge_rxq *sos_rxq;
	uintptr_t sos_rxq_addr;
	uint16_t sos_pending;
} sum_pending_state_t;

static int
sum_pending(uintptr_t rxq_entry_addr, const void *rxq_entry, void *state)
{
	const cxgbe_rxq_entry_64b_t *entry = rxq_entry;
	sum_pending_state_t *s = state;
	uintptr_t cdesc = (uintptr_t)s->sos_rxq->iq.cdesc;
	uint8_t entry_gen =
	    (entry->cre64_rsp_ctrl.u.type_gen & F_RSPD_GEN) >> S_RSPD_GEN;
	uint8_t iq_gen = s->sos_rxq->iq.gen;
	bool same_gen = entry_gen == iq_gen;

	if (rxq_entry_addr >= cdesc && same_gen) {
		s->sos_pending++;
	} else if (rxq_entry_addr < cdesc && !same_gen) {
		s->sos_pending++;
	}

	return (WALK_NEXT);
}

static int
cxgbe_rxq(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("cxgbe_rxq", "cxgbe_rxq", argc, argv) == -1) {
			mdb_warn("failed to walk 'cxgbe_rxq'");
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-18s %-6s %-12s %-6s %-10s\n", "LINK", "ADDR",
		    "SIZE", "PKTS", "PEND", "ERRORS");
	}

	/* RPZ Think about using CTF type for sge_rxq */
	struct sge_rxq rxq = {0};

	if (mdb_vread(&rxq, sizeof (rxq), addr) == -1) {
		mdb_warn("failed to read rx queue at %p", addr);
		return (DCMD_ERR);
	}

	struct port_info pi = {0};
	mdb_mac_impl_t mi = {0};

	if (mdb_vread(&pi, sizeof (pi), (uintptr_t)rxq.port) == -1) {
		mdb_warn("failed to read port_info at %p", rxq.port);
		return (DCMD_ERR);
	}

	int ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
	    (uintptr_t)pi.mh, 0);

	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", pi.mh);
		return (DCMD_ERR);
	}

	uint64_t errors = 0;
	/* errors += rxq.fl.stats.copy_fail; */
	/* errors += rxq.fl.stats.wrap_fail; */
	errors += rxq.fl.allocb_fail;

	sum_pending_state_t sum_state = {0};
	sum_state.sos_rxq = &rxq;
	sum_state.sos_rxq_addr = addr;
	/* RPZ Now that I have a pending entries walker I should just
	 * use that here. */
	ret = mdb_pwalk("cxgbe_rxq_entries", sum_pending, &sum_state, addr);

	if (ret != 0) {
		mdb_warn("failed to walk rxq entires");
		return (DCMD_ERR);
	}

	mdb_printf("%-7s 0x%p %-6u %-12u %-6u %-10u\n", mi.mi_name, addr,
	    rxq.iq.qsize, rxq.rxpkts, sum_state.sos_pending,
	    errors);

	return (DCMD_OK);
}

static int
cxgbe_txq(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("cxgbe_txq", "cxgbe_txq", argc, argv) == -1) {
			mdb_warn("failed to walk 'cxgbe_txq'");
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	/*
	 * RPZ The "FULL" column is more of a stat than a state, and
	 * the default output is about state.
	 */
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-18s %-6s %-12s %-6s %-10s %-10s %-10s %-10s "
		    "%-10s %-10s\n",
		    "LINK", "ADDR", "SIZE", "PKTS", "AVAIL", "FULL",
		    "TXB_AVAIL", "TXB_SIZE", "DHDL_AVAIL", "DHDL_TOTAL",
		    "ERRORS");
	}

	struct sge_txq txq = {0};

	if (mdb_vread(&txq, sizeof (txq), addr) == -1) {
		mdb_warn("failed to read sge_txq at %p", addr);
		return (DCMD_ERR);
	}

	struct port_info pi = {0};

	if (mdb_vread(&pi, sizeof (pi), (uintptr_t)txq.port) == -1) {
		mdb_warn("failed to read port_info at %p", txq.port);
		return (DCMD_ERR);
	}

	mdb_mac_impl_t mi = {0};

	int ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
	    (uintptr_t)pi.mh, 0);

	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", pi.mh);
		return (DCMD_ERR);
	}

	uint64_t errors = 0;
	errors += txq.dma_hdl_failed;
	errors += txq.dma_map_failed;
	errors += txq.pullup_failed;
	errors += txq.csum_failed;
	errors += txq.csum_failed;

	uint64_t full = 0;
	full += txq.qfull;
	full += txq.txb_full;

	uint16_t avail = txq.eq.avail;

	if (ret != 0) {
		mdb_warn("failed to walk rxq entires");
		return (DCMD_ERR);
	}

	mdb_printf("%-7s 0x%p %-6u %-12u %-6u %-10u %-10u %-10u %-10u %-10u "
	    "%-10u\n",
	    mi.mi_name, addr, txq.eq.qsize, txq.txpkts, avail, full,
	    txq.txb_avail, txq.txb_size, txq.tx_dhdl_avail, txq.tx_dhdl_total,
	    errors);

	return (DCMD_OK);
}

static int
count_cb(uintptr_t iq_entry_addr, const void *iq_entry, void *state)
{
	uint16_t *count = state;
	*count += 1;
	return (0);
}

/*
 * As the firmware queue is embedded in the adapter, we access it via
 * the adapter.
 */
static int
cxgbe_fwq(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	int ret = 0;

	if (!(flags & DCMD_ADDRSPEC)) {
		ret = mdb_walk_dcmd("cxgbe_adap", "cxgbe_fwq", argc, argv);

		if (ret == -1) {
			mdb_warn("failed to walk 'cxgbe_fwq'");
			return (DCMD_ERR);
		}

		return (DCMD_OK);
	}

	struct adapter adap = {0};

	if (mdb_vread(&adap, sizeof (adap), addr) == -1) {
		mdb_warn("failed to read t4nex`adapter at %p", addr);
		return (DCMD_ERR);
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-32s %-18s %-10s %-10s %-10s\n", "LINKS", "ADDR",
		    "SIZE", "PROCESSED", "PENDING");
	}

	char links[32] = {0};

	for (uint_t i = 0; i < adap.params.nports; i++) {
		mdb_port_info_t pi = {0};
		mdb_mac_impl_t mi = {0};
		uintptr_t pi_addr = (uintptr_t)adap.port[i];

		int ret = mdb_ctf_vread(&pi, "struct t4nex`port_info",
		    "mdb_port_info_t", pi_addr, 0);

		if (ret != 0) {
			mdb_warn("failed to read t4nex`port_info at %p",
			    pi_addr);
			return (-1);
		}

		ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
		    pi.mh, 0);

		if (ret != 0) {
			mdb_warn("failed to read mac_impl_t at %p", pi.mh);
			return (-1);
		}

		strcat(links, mi.mi_name);

		if (i + 1 < adap.params.nports)
			strcat(links, ",");
	}

	struct sge_iq *iq = &adap.sge.fwq;
	uintptr_t iq_vaddr = addr + offsetof (struct adapter, sge) +
	    offsetof (struct sge, fwq);
	uint16_t pending = 0;

	ret = mdb_pwalk("cxgbe_iq_pending_entries", count_cb, &pending,
	    iq_vaddr);

	if (ret != 0) {
		mdb_warn("failed to walk 'cxgbe_iq_pending_entries'");
		return (DCMD_ERR);
	}

	mdb_printf("%-32s 0x%p %-10u %-10u %-10u\n", links, addr, iq->qsize,
	    iq->stats.sis_processed, pending);

	return (DCMD_OK);
}

/* [-H[C|P|<idx>|<addr>]] */
/* [-i <idx> | -a <addr> | -C | -P ] [-H|-S] [-m]
 *
 * -m: memory addrs only
 * -S: sw desc
 * -H: hw desc
 * -C: cidx entry
 * -P: pidx entry
 * -i: entry by idx
 * -a: entry by address
 */
#define TXQ_ENT_OPT_HW_DESC	1 << 0
#define TXQ_ENT_OPT_CIDX_DESC	1 << 1
#define TXQ_ENT_OPT_PIDX_DESC	1 << 2

#define	TXQ_ENT_OPT_IDX_UNSET	UINT64_MAX

typedef struct txq_ent_state {
	struct sge_txq *tes_txq;
	uintptr_t tes_txq_addr;
	uintptr_t tes_range_start;
	uintptr_t tes_range_end;
	/* char tes_link[32]; */
	uint_t tes_opts;
} txq_ent_state_t;

static size_t
pktbytes(uintptr_t mp_addr)
{
	size_t len = 0;

	while (mp_addr != 0) {
		mblk_t mp = {0};
		int ret = mdb_vread(&mp, sizeof (mp), mp_addr);

		if (ret == -1) {
			mdb_warn("(a) failed to read mblk_t at %p", mp_addr);
			return (-1);
		}

		len += MBLKL(&mp);
		uintptr_t mp2_addr = (uintptr_t)mp.b_cont;

		while (mp2_addr != 0) {
			mblk_t mp2 = {0};
			ret = mdb_vread(&mp2, sizeof (mp2), mp2_addr);

			if (ret == -1) {
				mdb_warn("(b) failed to read mblk_t at %p",
				    mp2_addr);
				return (-1);
			}

			len += MBLKL(&mp2);
			mp2_addr = (uintptr_t)mp2.b_cont;
		}

		mp_addr = (uintptr_t)mp.b_next;
	}

	return (len);
}

static int
mp_b_next(uintptr_t mp_addr, uintptr_t *b_next)
{
	mblk_t mp = {0};
	int ret = mdb_vread(&mp, sizeof (mp), mp_addr);

	if (ret == -1) {
		mdb_warn("(c) failed to read mblk_t at %p", mp_addr);
		return (ret);
	}

	*b_next = (uintptr_t)mp.b_next;

	return (0);
}

static size_t
pktcount(uintptr_t mp_addr)
{
	size_t count = 0;

	for (; mp_addr != 0; mp_b_next(mp_addr, &mp_addr))
		count++;

	return (count);
}

/* 8 flits of 8 bytes */
#define	EQ_HC_SIZE	64

static int
txq_entry_print(uintptr_t addr, const void *entry, void *state)
{
	txq_ent_state_t *s = state;

	/* if (addr < s->tes_range_start || addr > s->tes_range_end) */
	/* 	return (WALK_NEXT); */

	const struct tx_sdesc *e = entry;
	uint16_t idx =
	    (addr - (uintptr_t)s->tes_txq->sdesc) / sizeof (struct tx_sdesc);
	/* uintptr_t cdesc = (uintptr_t)s->tes_txq->eq.cdesc; */
	uintptr_t hw_addr =
	    (uintptr_t)s->tes_txq->eq.desc + (idx * EQ_HC_SIZE);

	mdb_printf("%-5u %-18p %-18p %-8u %-8u %-8u\n", idx, addr, hw_addr,
	    pktbytes((uintptr_t)e->mp_head), pktcount((uintptr_t)e->mp_head),
	    e->desc_used);

	return (WALK_NEXT);
}

static int
cxgbe_txq_ent(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("must supply an sge_txq address\n");
		return (DCMD_ERR);
	}

	uint_t opts = 0;
	uint64_t opt_i = TXQ_ENT_OPT_IDX_UNSET;

	int ret = mdb_getopts(argc, argv,
	    'C', MDB_OPT_SETBITS, TXQ_ENT_OPT_CIDX_DESC, &opts,
	    'H', MDB_OPT_SETBITS, TXQ_ENT_OPT_HW_DESC, &opts,
	    'i', MDB_OPT_UINT64, &opt_i,
	    'P', MDB_OPT_SETBITS, TXQ_ENT_OPT_PIDX_DESC, &opts,
	    NULL);

	if (ret != argc)
		return (DCMD_USAGE);

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-5s %-18s %-18s %-8s %-8s %-8s\n", "IDX", "SW ADDR",
		    "HW ADDR", "BYTES", "PKTS", "CREDITS");
	}

	struct sge_txq txq = {0};

	if (mdb_vread(&txq, sizeof (txq), addr) == -1) {
		mdb_warn("failed to read sge_txq at %p", addr);
		return (DCMD_ERR);
	}

	/* RPZ convert to mdb_port_info? */
	struct port_info pi = {0};

	if (mdb_vread(&pi, sizeof (pi), (uintptr_t)txq.port) == -1) {
		mdb_warn("failed to read port_info at %p", txq.port);
		return (DCMD_ERR);
	}

	mdb_mac_impl_t mi = {0};

	ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
	    (uintptr_t)pi.mh, 0);

	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", pi.mh);
		return (DCMD_ERR);
	}

	txq_ent_state_t state = {0};
	state.tes_txq = &txq;
	state.tes_txq_addr = addr;
	state.tes_range_start = (uintptr_t)txq.sdesc +
	    (txq.eq.cidx * sizeof (struct tx_sdesc));
	/*
	 * PIDX points to the next entry to be produced, we subtract
	 * one to get the last entry to be produced.
	 */
	state.tes_range_end = (uintptr_t)txq.sdesc +
	    ((txq.eq.pidx - 1) * sizeof (struct tx_sdesc));
	/* strlcpy(state.res_link, mi.mi_name, sizeof (state.res_link)); */
	state.tes_opts = opts;

	if ((opts & TXQ_ENT_OPT_CIDX_DESC) != 0) {
		state.tes_range_end = state.tes_range_start;
	} else if ((opts & TXQ_ENT_OPT_PIDX_DESC) != 0) {
		state.tes_range_start = state.tes_range_end;
	}

	if (opt_i != TXQ_ENT_OPT_IDX_UNSET) {
		uintptr_t addr = (uintptr_t)&txq.sdesc[opt_i];
		struct tx_sdesc sdesc = {0};

		if (mdb_vread(&sdesc, sizeof (sdesc), addr) == -1) {
			mdb_warn("failed to read tx_sdesc at %p", addr);
			return (DCMD_ERR);
		}

		return (txq_entry_print(addr, &sdesc, &state));
	}

	ret = mdb_pwalk("cxgbe_txq_sdesc", txq_entry_print, &state, addr);

	if (ret != 0) {
		mdb_warn("failed to walk TX queue entires");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

#define	CXGBE_RXQ_ENTRIES_OPT_PEND	1 << 0

typedef struct rxq_entries_state {
	struct sge_rxq *res_rxq;
	uintptr_t res_rxq_addr;
	char res_link[32];
	uint_t res_opts;
} rxq_entries_state_t;

static int
rxq_entries_print(uintptr_t entry_addr, const void *rxq_entry, void *state)
{
	const cxgbe_rxq_entry_64b_t *entry = rxq_entry;
	rxq_entries_state_t *s = state;
	uintptr_t cdesc = (uintptr_t)s->res_rxq->iq.cdesc;
	uint8_t entry_gen = entry->cre64_rsp_ctrl.u.type_gen & F_RSPD_GEN;
	uint8_t iq_gen = s->res_rxq->iq.gen;
	bool same_gen = entry_gen == iq_gen;
	bool pend = (entry_addr >= cdesc && same_gen) ||
	    (entry_addr < cdesc && !same_gen);
	/* The device stores length in BE. */
	uint16_t len = 0;
	bool print = true;

	mdb_nhconvert(&len, &entry->cre64_cpl_rx_pkt.len, sizeof (len));

	if ((s->res_opts & CXGBE_RXQ_ENTRIES_OPT_PEND) && !pend)
		print = false;

	if (print) {
		mdb_printf("%-7s %-18p %-18p %-6u %-4s\n", s->res_link,
		    s->res_rxq_addr, entry_addr, len, pend ? "Y" : "N");
	}

	return (WALK_NEXT);
}

/*
 * RPZ I think I was supposed to update this to be like
 * cxgbe_txq_ent(), where you can't use a global/zero address, as it's
 * not meant to print an individual rxq entry but rather print the
 * entries of the specified rxq. Furthermore, perhaps we should only
 * show pending rxq entries? If someone wants to dig into non-pending
 * entries perhaps that should be a generic IQ walker or done by hand in mdb.
 *
 * I definitely need to write a generic IQ entry walker so I can count
 * pending entries for the intr/fw queues.
 */
static int
cxgbe_rxq_entries(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t opts = 0;
	int ret = mdb_getopts(argc, argv,
	    'P', MDB_OPT_SETBITS, CXGBE_RXQ_ENTRIES_OPT_PEND, &opts,
	    NULL);

	if (ret != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		/* RPZ Need to implement global cxgbe rxq walker */
		/* if (mdb_walk_dcmd("cxgbe_rxq", " */
	}

	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%-7s %-18s %-18s %-6s %-4s\n", "LINK", "RXQ",
		    "ENTRY", "LEN", "PEND");
	}

	struct sge_rxq rxq = {0};

	if (mdb_vread(&rxq, sizeof (rxq), addr) == -1) {
		mdb_warn("failed to read sge_rxq at %p", addr);
		return (DCMD_ERR);
	}

	/* RPZ use mdb_port_info? */
	struct port_info pi = {0};

	if (mdb_vread(&pi, sizeof (pi), (uintptr_t)rxq.port) == -1) {
		mdb_warn("failed to read port_info at %p", rxq.port);
		return (DCMD_ERR);
	}

	mdb_mac_impl_t mi = {0};
	ret = mdb_ctf_vread(&mi, "mac_impl_t", "mdb_mac_impl_t",
	    (uintptr_t)pi.mh, 0);

	if (ret != 0) {
		mdb_warn("failed to read mac_impl_t at %p", pi.mh);
		return (DCMD_ERR);
	}

	rxq_entries_state_t state = {0};
	state.res_rxq = &rxq;
	state.res_rxq_addr = addr;
	strlcpy(state.res_link, mi.mi_name, sizeof (state.res_link));
	state.res_opts = opts;
	ret = mdb_pwalk("cxgbe_rxq_entries", rxq_entries_print, &state, addr);

	if (ret != 0) {
		mdb_warn("failed to walk rxq entires");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

static int
print_cpl(uintptr_t addr)
{
	int len = 0;
	uint8_t cpl_op = 0;

	if (mdb_vread(&cpl_op, sizeof (cpl_op), addr) == -1) {
		mdb_warn("failed to read CPL op at %p", addr);
		return (-1);
	}

	switch (cpl_op) {
	case CPL_TX_PKT_XT: {
		struct cpl_tx_pkt_core cpl = {0};
		if (mdb_vread(&cpl, sizeof (cpl), addr) == -1) {
			mdb_warn("failed to read CPL_TX_PKT_XT");
			return (-1);
		}

		mdb_printf("CPL_TX_PKT_XT (0x%p)\n", addr);

		len = sizeof (cpl);
		uint32_t ctrl0 = 0;
		mdb_nhconvert(&ctrl0, &cpl.ctrl0, sizeof (ctrl0));
		mdb_inc_indent(2);
		mdb_printf("--- FLIT #0\n");
		mdb_printf("63:56 Opcode: 0x%X\n", G_TXPKT_OPCODE(ctrl0));
		mdb_printf("55:55 Timestamp: %u\n",
		    (ctrl0 & F_TXPKT_TSTAMP) != 0);
		mdb_printf("54:54 Stat Disable: %u\n",
		    (ctrl0 & F_TXPKT_STAT_DIS) != 0);
		mdb_printf("53:53 FCS Disable: %u\n",
		    (ctrl0 & F_TXPKT_T5_FCS_DIS) != 0);
		mdb_printf("52:52 Stat Special: %u\n",
		    (ctrl0 & F_TXPKT_SPECIAL_STAT) != 0);
		mdb_printf("51:48 Interface: %u\n", G_TXPKT_INTF(ctrl0));
		mdb_printf("47:47 Ins Outer VLAN: %u\n",
		    (ctrl0 & F_TXPKT_T5_INS_OVLAN) != 0);
		mdb_printf("46:44 Outer VLAN Idx: %u\n",
		    G_TXPKT_T5_OVLAN_IDX(ctrl0));
		mdb_printf ("43:43 Valid VF: %u\n",
		    (ctrl0 & F_TXPKT_VF_VLD) != 0);
		mdb_printf("42:40 PF: %u\n", G_TXPKT_PF(ctrl0));
		mdb_printf("39:32 VF: %u\n", G_TXPKT_VF(ctrl0));
		uint16_t pack = 0;
		mdb_nhconvert(&pack, &cpl.pack, sizeof (pack));
		mdb_printf("31:16 Pack: %u\n", pack);
		/* RPZ this is shadowing earlier len, is that okay? */
		uint16_t len = 0;
		mdb_nhconvert(&len, &cpl.len, sizeof (len));
		mdb_printf("15:0  Length: %u\n", len);

		/* Control Flit #1 */
		uint64_t ctrl1 = 0;
		mdb_nhconvert(&ctrl1, &cpl.ctrl1, sizeof (ctrl1));
		mdb_printf("--- FLIT #1\n");
		mdb_printf("63:63 L4 Chk Disable: %u\n",
		    (ctrl1 & F_TXPKT_L4CSUM_DIS) != 0);
		mdb_printf("62:62 L3 Chk Disable: %u\n",
		    (ctrl1 & F_TXPKT_IPCSUM_DIS) != 0);
		mdb_printf("61:61 IP Sec: %u\n", (ctrl1 & F_TXPKT_IPSEC) != 0);
		mdb_printf("60:60 Ins Inner VLAN: %u\n",
		    (ctrl1 & F_TXPKT_VLAN_VLD) != 0);
		mdb_printf("59:44 Inner VLAN: %u\n", G_TXPKT_VLAN(ctrl1));
		mdb_printf("43:40 Chk Type: ");
		uint8_t csum_type = G_TXPKT_CSUM_TYPE(ctrl1);

		switch (csum_type) {
		case TX_CSUM_TCP:
			mdb_printf("Generic TCP (0)\n");
			break;
		case TX_CSUM_UDP:
			mdb_printf("Generic UDP (1)\n");
			break;
		case TX_CSUM_CRC32:
			mdb_printf("CRC-32 FCoE (5)\n");
			break;
		case TX_CSUM_FCOE:
			mdb_printf("CRC-32 FCoE From End (7)\n");
			break;
		case TX_CSUM_TCPIP:
			mdb_printf("TCP+IPv4 (8)\n");
			break;
		case TX_CSUM_UDPIP:
			mdb_printf("UDP+IPv4 (9)\n");
			break;
		case TX_CSUM_TCPIP6:
			mdb_printf("TCP+IPv6 (10)\n");
			break;
		case TX_CSUM_UDPIP6:
			mdb_printf("UDP+IPv6 (11)\n");
			break;
		case TX_CSUM_IP:
			mdb_printf("IPv4 (12)\n");
			break;
		default:
			mdb_printf("Reserved (%u)\n", csum_type);
			break;
		}

		switch (csum_type) {
		case TX_CSUM_TCPIP:
		case TX_CSUM_UDPIP:
		case TX_CSUM_TCPIP6:
		case TX_CSUM_UDPIP6:
		case TX_CSUM_IP:
			/* RPZ need to swtich based on chip type? */
			mdb_printf("39:32 Eth Hdr Len: %u\n",
			    G_T6_TXPKT_ETHHDR_LEN(ctrl1));
			mdb_printf("31:20 IP Hdr Len: %u\n",
			    G_T6_TXPKT_IPHDR_LEN(ctrl1));
			/*
			 * These checksum types do not make use of the
			 * CSUM_END field.
			 */
			mdb_printf("19:12 Unused: 0x%x\n",
			    G_TXPKT_CSUM_END(ctrl1));
			break;

		case TX_CSUM_TCP:
		case TX_CSUM_UDP:
		case TX_CSUM_CRC32:
		case TX_CSUM_FCOE:
		default:
			mdb_printf("39:30 Chk Insert Offset: %u\n",
			    G_TXPKT_CSUM_LOC(ctrl1));
			mdb_printf("29:20 Chk Start Offset: %u\n",
			    G_TXPKT_CSUM_START(ctrl1));
			mdb_printf("19:12 Chk Stop Offset: %u\n",
			    G_TXPKT_CSUM_END(ctrl1));
			break;
		}

		mdb_printf("11:0  IPSec SA Idx: %u\n", G_TXPKT_SA_IDX(ctrl1));

		mdb_dec_indent(2);
		break;
	}
	default:
		mdb_printf("Unknown CPL (0x%X)\n", cpl_op);
		return (-1);
	}

	return (len);
}

static int
print_ulptx_sc(uintptr_t addr)
{
	struct ulptx_sgl sgl = {0};

	if (mdb_vread(&sgl, sizeof (sgl), addr) == -1) {
		mdb_warn("failed to read ulptx_sgl at %p", addr);
		return (-1);
	}

	int len = sizeof (sgl);
	uint32_t cmd_nsge = 0;
	mdb_nhconvert(&cmd_nsge, &sgl.cmd_nsge, sizeof (cmd_nsge));
	uint8_t sc_op = (cmd_nsge >> S_ULPTX_CMD) & M_ULPTX_CMD;

	switch (sc_op) {
	case ULP_TX_SC_DSGL:
		mdb_printf("ULPTX SUBCOMMAND DSGL (0x%p)\n", addr);

		mdb_inc_indent(2);

		mdb_printf("--- FLIT #0\n");
		mdb_printf("63:56 Subcommand: ULP_TX_SC_DSGL (0x%X)\n", sc_op);
		mdb_printf("55:54 Reserved: 0x%X\n", (cmd_nsge >> 22) & 0x7);
		mdb_printf("53:53 PCI no-snoop: %u\n", (cmd_nsge >> 21) & 0x1);
		mdb_printf("52:48 Reserved: 0x%X\n", (cmd_nsge >> 16) & 0x1F);
		mdb_printf("47:32 Num Len/Addr Pairs: %u\n",
		    G_ULPTX_NSGE(cmd_nsge));
		uint32_t len0 = 0;
		mdb_nhconvert(&len0, &sgl.len0, sizeof (len0));
		mdb_printf("31:0  Length #0: %u\n", len0);
		uint64_t addr0 = 0;
		mdb_nhconvert(&addr0, &sgl.addr0, sizeof (addr0));
		mdb_printf("63:0  Addr #0: 0x%p\n", addr0);

		mdb_dec_indent(2);

		break;

	case ULP_TX_SC_IMM:
		mdb_printf("ULPTX SUBCOMMAND IMM (0x%p)\n", addr);

		mdb_inc_indent(2);

		mdb_printf("--- FLIT #0\n");
		mdb_printf("63:56 Subcommand: TODO (0x%X)\n", sc_op);

		mdb_dec_indent(2);
		break;

	case ULP_TX_SC_NOOP:
		mdb_printf("ULPTX SUBCOMMAND NOOP (0x%p)\n", addr);
		break;
	case ULP_TX_SC_ISGL:
		mdb_printf("ULPTX SUBCOMMAND ISGL (0x%p)\n", addr);
		break;
	case ULP_TX_SC_PICTRL:
		mdb_printf("ULPTX SUBCOMMAND PICTRL (0x%p)\n", addr);
		break;
	case ULP_TX_SC_MEMRD:
		mdb_printf("ULPTX SUBCOMMAND MEMRD (0x%p)\n", addr);
		break;
	default:
		mdb_printf("ULPTX SUBCOMMAND Unknown: 0x%x (0x%p)\n", sc_op,
		    addr);
		break;
	}

	mdb_dec_indent(2);
	return (len);
}

static int
print_imm_data(uintptr_t addr, size_t len)
{
	mdb_printf("IMM DATA: %uB (0x%p)\n", len, addr);
	int ret = mdb_dumpptr(addr, len, MDB_DUMP_ALIGN | MDB_DUMP_PEDANT |
	    MDB_DUMP_ASCII | MDB_DUMP_HEADER | MDB_DUMP_TRIM, NULL, NULL);

	return (ret == 0 ? len : -1);
}

/*
 * See the T4+ Firmware Interface Specification for details on these
 * layouts.
 */
static int
cxgbe_wr(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("must specify WR address");
		return (DCMD_ERR);
	}

	uint8_t wr_op = 0;

	if (mdb_vread(&wr_op, sizeof (wr_op), addr) == -1) {
		mdb_warn("failed to read first flit");
		return (DCMD_ERR);
	}

	switch (wr_op) {
	case FW_ETH_TX_PKT_WR: {
		struct fw_eth_tx_pkt_wr wr = {0};

		if (mdb_vread(&wr, sizeof (wr), addr) == -1) {
			mdb_warn("failed to read FW_ETH_TX_PKT_WR");
			return (DCMD_ERR);
		}

		mdb_printf("FW_ETH_TX_PKT_WR (0x%p)\n", addr);
		mdb_inc_indent(2);

		uint32_t op_immdlen = 0;
		mdb_nhconvert(&op_immdlen, &wr.op_immdlen, sizeof (op_immdlen));

		uint32_t equiq_to_len16 = 0;
		mdb_nhconvert(&equiq_to_len16, &wr.equiq_to_len16,
		    sizeof (equiq_to_len16));

		const uint16_t imm_len = G_FW_WR_IMMDLEN(op_immdlen);

		mdb_printf("--- FLIT #0\n");
		mdb_printf("63:56 Opcode: 0x%X\n", G_FW_WR_OP(op_immdlen));
		mdb_printf("55:40 Reserved: 0x%X\n",
		    (op_immdlen >> 8) & 0xFFFF);
		mdb_printf("40:32 Immediate Data Len: %u\n", imm_len);
		mdb_printf("31:31 EQUIQ: %u\n", G_FW_WR_EQUIQ(equiq_to_len16));
		mdb_printf("30:30 EQUEQ: %u\n", G_FW_WR_EQUEQ(equiq_to_len16));
		mdb_printf("29:8  Reserved: 0x%X\n",
		    (equiq_to_len16 >> 8) & 0x3FFFFF);
		mdb_printf("7:0   WR Len (16B): %u\n",
		    G_FW_WR_LEN16(equiq_to_len16));
		mdb_printf("--- FLIT #1\n");
		mdb_printf("63:0  Reserved: 0x%X\n", wr.r3);
		mdb_dec_indent(2);

		/* Move onto the CPL message. */
		addr += sizeof (struct fw_eth_tx_pkt_wr);
		const int cpl_len = print_cpl(addr);

		if (cpl_len == -1)
			return (DCMD_ERR);

		addr += cpl_len;
		int ret = 0;

		/*
		 * If we have not read all the immediate data, then
		 * that implies the packet data resides in the
		 * immediate data following the CPL msg. Otherwise, we
		 * have a ULP TX SGL subcommand.
		 */
		if (imm_len > cpl_len) {
			ret = print_imm_data(addr, imm_len - cpl_len);
		} else {
			ret = print_ulptx_sc(addr);
		}

		if (ret == -1)
			return (DCMD_ERR);

		/* RPZ I'll need the code below later, for TX_PKTS_WR */

		/* mdb_printf("ULP TX MASTER CMD (0x%p)\n", addr); */
		/* struct ulp_txpkt ulpmc = {0}; */
		/* if (mdb_vread(&ulpmc, sizeof (ulpmc), addr) == -1) { */
		/* 	mdb_warn("failed to read ULP TX MC at %p", addr); */
		/* 	return (DCMD_ERR); */
		/* } */

		/* mdb_inc_indent(2); */

		/* /\* */
		/*  * NOTE: This layout changes quite a bit on T7. A */
		/*  * future version of this dcmd needs to take a chip */
		/*  * version flag to know which layout is expect. */
		/*  *\/ */
		/* mdb_printf("--- FLIT #0\n"); */
		/* uint32_t cmd_dest = 0; */
		/* mdb_nhconvert(&cmd_dest, &ulpmc.cmd_dest, sizeof (cmd_dest)); */
		/* mdb_printf("63:56 Opcode: 0x%X\n", */
		/*     (cmd_dest >> S_ULPTX_CMD) & M_ULPTX_CMD); */
		/* mdb_printf("55:55 Data Modify: %u\n", */
		/*     G_ULP_TXPKT_DATAMODIFY(cmd_dest)); */
		/* mdb_printf("54:54 Channel ID: %u\n", */
		/*     G_ULP_TXPKT_CHANNELID(cmd_dest)); */
		/* mdb_printf("53:51 ???: 0x%X\n", */
		/*     (cmd_dest >> (S_ULP_TXPKT_DEST + 3)) & 0x3); */
		/* mdb_printf("50:48 Dest: 0x%X\n", */
		/*     (cmd_dest >> S_ULP_TXPKT_DEST) & M_ULP_TXPKT_DEST); */
		/* mdb_printf("47:47 ???: 0x%X\n", (cmd_dest >> 15) & 0x1); */
		/* mdb_printf("46:36 FID: %u\n", */
		/*     (cmd_dest >> S_ULP_TXPKT_FID) & M_ULP_TXPKT_FID); */
		/* mdb_printf("35:35 Relaxed Ordering: %u\n", */
		/*     (cmd_dest >> S_ULP_TXPKT_RO) & 0x1); */
		/* mdb_printf("34:32 ???: 0x%X\n", cmd_dest & 0x7); */

		/* uint32_t ulp_len = 0; */
		/* mdb_nhconvert(&ulp_len, &ulpmc.len, sizeof (ulp_len)); */
		/* mdb_printf("31:8 ???: 0x%X\n", (ulp_len >> 8)); */
		/* mdb_printf("7:0 Length (16B): %u\n", ulp_len & 0xFF); */

		/* mdb_dec_indent(2); */

		break;
	}

	default:
		mdb_printf("Unknown WR (0x%X)\n", wr_op);
		break;
	}

	return (DCMD_OK);
}

static int
cxgbe_cpl_rx_pkt(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	if (argc != 0)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("must specify CPL_RX_PKT address");
		return (DCMD_ERR);
	}

	struct cpl_rx_pkt cpl = {0};

	if (mdb_vread(&cpl, sizeof (cpl), addr) == -1) {
		mdb_warn("failed to read cpl_rx_pkt at %p", addr);
		return (DCMD_ERR);
	}

	if (cpl.opcode != CPL_RX_PKT) {
		mdb_warn("address %p is not a valid CPL_RX_PKT\n", addr);
		return (DCMD_ERR);
	}

	/*
	 * The T4 CPL book considers the RSS as FLIT #0, but this dcmd
	 * takes the address of the start of the CPL_RX_PKT, so we
	 * start with FLIT #1.
	 */
	mdb_printf("--- FLIT #1\n");
	mdb_printf("63:56 Opcode: 0x%X\n", cpl.opcode);
	mdb_printf("55:55 IP Frag: %u\n", cpl.ip_frag);
	mdb_printf("54:54 VLAN Extract: %u\n", cpl.vlan_ex);
	mdb_printf("53:53 IPMI: %u\n", cpl.ipmi_pkt);
	/*
	 * This indicates that the csum field is populated with the IP
	 * payload checksum.
	 */
	mdb_printf("52:52 Csum Field Valid: %u\n", cpl.csum_calc);
	mdb_printf("51:48 Interface: %u\n", cpl.iff);
	uint16_t csum = 0;
	mdb_nhconvert(&csum, &cpl.csum, sizeof (csum));
	mdb_printf("47:32 Checksum: 0x%x\n", csum);
	uint16_t vlan = 0;
	mdb_nhconvert(&vlan, &cpl.vlan, sizeof (vlan));
	mdb_printf("31:16 VLAN: %u\n", vlan);
	uint16_t len = 0;
	mdb_nhconvert(&len, &cpl.len, sizeof (len));
	mdb_printf("15:0  Length: %u\n", len);

	mdb_printf("--- FLIT #2\n");
	uint32_t l2info = 0;
	mdb_nhconvert(&l2info, &cpl.l2info, sizeof (l2info));
	mdb_printf("63:60 Rx E-Channel: %u\n", G_RX_CHAN(l2info));
	/* RPZ breakout the flags via the various F_RXF_* macros */
	mdb_printf("59:52 Flags: 0x%X\n", (l2info >> 20) & 0xFF);
	/* RPZ The T5 has a field at bit 17 (S_RX_T5_PKTYPE) */
	mdb_printf("51:49 MAC Match Type: 0x%X\n", G_RX_DATYPE(l2info));
	mdb_printf("48:40 Exact Match Idx: %u\n", G_RX_MACIDX(l2info));
	/* RPZ There are three different macros:
	 *
	 * G_RX_ETHHDR_LEN: T4
	 * G_RX_T5_ETHHDR_LEN: T5
	 * G_RX_T6_ETHHDR_LEN: T6
	 *
	 * And perhaps there is a new one for T7?
	 *
	 * For now we assume T6.
	 */
	mdb_printf("39:32 Eth Hdr Len: %u\n", G_RX_T6_ETHHDR_LEN(l2info));

	uint16_t hdr_len = 0;
	mdb_nhconvert(&hdr_len, &cpl.hdr_len, sizeof (hdr_len));
	mdb_printf("31:22 IP Hdr Len: %u\n", G_RX_IPHDR_LEN(hdr_len));
	mdb_printf("21:16 TCP Hdr Len: %u\n", G_RX_TCPHDR_LEN(hdr_len));
	uint16_t err_vec = 0;
	mdb_nhconvert(&err_vec, &cpl.err_vec, sizeof (err_vec));
	mdb_printf("15:0 Errors: 0x%X\n", err_vec);

	return (DCMD_OK);
}

static const mdb_dcmd_t dcmds[] = {
	{ "cxgbe", NULL, "print info about cxgbe instance", cxgbe },
	{ "cxgbe_rxq", NULL, "print RX queue information of cxgbe instance",
	  cxgbe_rxq },
	{ "cxgbe_rxq_entries", "[-P]", "print RX queue entries",
	  cxgbe_rxq_entries },
	{ "cxgbe_txq", NULL, "print TX queue information of cxgbe instance",
	  cxgbe_txq },
	{ "cxgbe_wr", NULL, "print a Work Request (WR)", cxgbe_wr },
	{ "cxgbe_txq_ent", "[-i]", "print TX queue entries", cxgbe_txq_ent },
	{ "cxgbe_fwq", NULL, "print firmware queues", cxgbe_fwq },
	{ "cxgbe_cpl_rx_pkt", NULL, "print a CPL_RX_PKT msb",
	  cxgbe_cpl_rx_pkt },
	{ NULL }
};

static const mdb_walker_t walkers[] = {
	{ "cxgbe_adap", "walk cxgbe adapters", walk_adap_init, walk_adap_step,
	  NULL },
	{ "cxgbe", "walk cxgbe 'port_info' instances", walk_cxgbe_init,
	  walk_cxgbe_step, NULL },
	{ "cxgbe_rxq", "walk RX queues of cxgbe instance", walk_rxq_init,
	  walk_queue_step, walk_queue_fini },
	{ "cxgbe_iq_entries", "walk IQ entries", walk_iq_ent_init,
	  walk_iq_ent_step, walk_iq_ent_fini },
	{ "cxgbe_iq_pending_entries", "walk pending IQ entries",
	  walk_iq_pend_ent_init, walk_iq_ent_step, walk_iq_ent_fini },
	{ "cxgbe_rxq_entries", "walk RX queue entries", walk_rxq_ent_init,
	  walk_rxq_ent_step, walk_rxq_ent_fini },
	/* { "cxgbe_rxq_pending_entries", "walk pending RX queue entries", */
	/*   walk_rxq_pend_ent_init, walk_rxq_ent_step, walk_rxq_ent_fini }, */
	/* { "cxgbe_rxq_pending_data", "walk pending RX queue data", */
	/*   walk_rxq_pdata_init, walk_rxq_pdata_step, walk_rxq_pdata_fini }, */
	{ "cxgbe_txq", "walk TX queues of cxgbe instance", walk_txq_init,
	  walk_queue_step, walk_queue_fini },
	{ "cxgbe_txq_sdesc", "walk TX queue sdesc entries", walk_txq_sdesc_init,
	  walk_txq_sdesc_step, walk_txq_sdesc_fini },
	/* { "cxgbe_evtq", "walk event queues of cxgbe instance", walk_evtq_init, */
	/*   walk_queue_step, walk_queue_fini }, */
	/* RPZ add walkter to walk txq EQ host credits */
	{ NULL },
};

static const mdb_modinfo_t modinfo = {
	.mi_dvers = MDB_API_VERSION,
	.mi_dcmds = dcmds,
	.mi_walkers = walkers,
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
