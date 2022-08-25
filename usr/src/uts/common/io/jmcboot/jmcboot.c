#include <sys/types.h>
#include <sys/stdbool.h>

#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/ddi.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/ddidevmap.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/sunldi.h>
#include <sys/ramdisk.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>

/*
 * Linkage structures
 */
static struct modlmisc jmcboot_modlmisc = {
	.misc_modops =				&mod_miscops,
	.misc_linkinfo =			"jmcboot",
};

static struct modlinkage jmcboot_modlinkage = {
	.ml_rev =				MODREV_1,
	.ml_linkage =				{ &jmcboot_modlmisc, NULL },
};

int
_init(void)
{
	return (mod_install(&jmcboot_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&jmcboot_modlinkage));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&jmcboot_modlinkage, mi));
}


typedef struct jmc_find_ether {
	boolean_t jfe_print_only;
} jmc_find_ether_t;

static int
jmc_find_ether(dev_info_t *dip, void *arg)
{
	jmc_find_ether_t *jfe = arg;

	if (i_ddi_devi_class(dip) == NULL ||
	    strcmp(i_ddi_devi_class(dip), ESC_NETWORK) != 0) {
		/*
		 * We do not think that this is a network interface.
		 */
		return (DDI_WALK_CONTINUE);
	}

	if (i_ddi_attach_node_hierarchy(dip) != DDI_SUCCESS) {
		return (DDI_WALK_CONTINUE);
	}

	if (jfe->jfe_print_only) {
		printf("    %s%d\n",
		    ddi_driver_name(dip),
		    i_ddi_devi_get_ppa(dip));
	}

	return (DDI_WALK_CONTINUE);
}

typedef enum jmc_ether_state {
	JMCBOOT_STATE_REST,
	JMCBOOT_STATE_READING,
	JMCBOOT_STATE_FINISHED,
} jmc_ether_state_t;

typedef struct jmc_ether {
	kmutex_t je_mutex;
	kcondvar_t je_cv;
	uint64_t je_npkts;
	ether_addr_t je_macaddr;
	ether_addr_t je_server;

	jmc_ether_state_t je_state;
	hrtime_t je_download_start;
	hrtime_t je_last_hello;
	hrtime_t je_last_status;
	bool je_reset;
	uint64_t je_ramdisk_size;

	uint64_t je_offset;
	mblk_t *je_q;

	ldi_ident_t je_li;
	ldi_handle_t je_rd_ctl;
	ldi_handle_t je_rd_disk;
} jmc_ether_t;

#define	JMCBOOT_TYPE_HELLO		0x9001
#define	JMCBOOT_TYPE_OFFER		0x9102
#define	JMCBOOT_TYPE_READ		0x9003
#define	JMCBOOT_TYPE_DATA		0x9104
#define	JMCBOOT_TYPE_FINISHED		0x9005
#define	JMCBOOT_TYPE_RESET		0x9106

#define	JMCBOOT_MAGIC			0x1DE12345

typedef struct jmc_frame_header {
	struct ether_header jfh_ether;
	uint32_t jfh_magic;
	uint32_t jfh_type;
	uint32_t jfh_len;
} __packed jmc_frame_header_t;

typedef struct jmc_frame_offer {
	jmc_frame_header_t jfo_header;
	uint64_t jfo_ramdisk_size;
} __packed jmc_frame_offer_t;

#define	JMCBOOT_LEN_OFFER	(sizeof (jmc_frame_offer_t) - \
				sizeof (jmc_frame_header_t))

typedef struct jmc_frame_data {
	jmc_frame_header_t jfd_header;
	uint64_t jfd_offset;
} __packed jmc_frame_data_t;

typedef struct jmc_frame_read {
	jmc_frame_header_t jfr_header;
	uint64_t jfr_offset;
	uint64_t jfr_length;
} __packed jmc_frame_read_t;

#define	JMCBOOT_LEN_READ	(sizeof (jmc_frame_read_t) - \
				sizeof (jmc_frame_header_t))

#define	JMCBOOT_LEN_RESET	0
#define	JMCBOOT_LEN_FINISHED	0

static void
jmc_ether_rx(void *arg, mac_resource_handle_t mrh, mblk_t *m,
    boolean_t is_loopback)
{
	jmc_ether_t *je = arg;

	if (is_loopback) {
		goto drop;
	}

	while (m != NULL) {
		mutex_enter(&je->je_mutex);
		bool reset = je->je_reset;
		mutex_exit(&je->je_mutex);

		if (reset) {
			goto drop;
		}

		mblk_t *next = m->b_next;
		m->b_next = NULL;

		if (m->b_cont != NULL) {
			mblk_t *nm;
			if ((nm = msgpullup(m, sizeof (jmc_frame_header_t))) ==
			    NULL) {
				goto next;
			}
			freemsg(m);
			m = nm;
		}

		if (MBLKL(m) < sizeof (jmc_frame_header_t)) {
			goto next;
		}

		jmc_frame_header_t *jfh = (void *)m->b_rptr;
		if (ntohl(jfh->jfh_magic) != JMCBOOT_MAGIC) {
			goto next;
		}

		/*
		 * Decide what to do with this message type.
		 */
		switch (ntohl(jfh->jfh_type)) {
		case JMCBOOT_TYPE_OFFER:
			if (ntohl(jfh->jfh_len) != JMCBOOT_LEN_OFFER) {
				goto next;
			} else {
				/*
				 * Pull the whole message up.
				 */
				mblk_t *nm;
				if ((nm = msgpullup(m, -1)) == NULL) {
					goto next;
				}
				m = nm;
			}
			break;
		case JMCBOOT_TYPE_DATA:
			if (ntohl(jfh->jfh_len) > 1476) {
				goto next;
			} else {
				/*
				 * Pull up the offset portion of the frame.
				 */
				size_t pu = sizeof (jmc_frame_data_t);
				mblk_t *nm;
				if ((nm = msgpullup(m, pu)) == NULL) {
					goto next;
				}
				m = nm;
			}
			break;
		case JMCBOOT_TYPE_RESET:
			if (ntohl(jfh->jfh_len) != JMCBOOT_LEN_RESET) {
				goto next;
			}
			mutex_enter(&je->je_mutex);
			je->je_reset = true;
			mutex_exit(&je->je_mutex);
			goto drop;
		default:
			goto next;
		}

		mutex_enter(&je->je_mutex);
		if (je->je_q == NULL) {
			je->je_q = m;
		} else {
			mblk_t *t = je->je_q;
			while (t->b_next != NULL) {
				t = t->b_next;
			}
			t->b_next = m;
		}
		m = NULL;
		cv_broadcast(&je->je_cv);
		mutex_exit(&je->je_mutex);

next:
		freemsg(m);
		m = next;
	}

drop:
	while (m != NULL) {
		mblk_t *next = m->b_next;

		m->b_next = NULL;
		freemsg(m);
		m = next;
	}
}

static void
jmc_set_ether_header(jmc_ether_t *je, jmc_frame_header_t *jfh,
    uchar_t *addr)
{
	jfh->jfh_ether.ether_type = htons(0x1DE0);
	(void) memcpy(&jfh->jfh_ether.ether_shost, je->je_macaddr, ETHERADDRL);
	if (addr == NULL) {
		/*
		 * Broadcast address:
		 */
		(void) memset(&jfh->jfh_ether.ether_dhost, 0xFF, ETHERADDRL);
	} else {
		(void) memcpy(&jfh->jfh_ether.ether_dhost, addr, ETHERADDRL);
	}
}

static void
jmc_send_hello(jmc_ether_t *je, mac_client_handle_t mch)
{
	mutex_exit(&je->je_mutex);
	mblk_t *m;
	if ((m = allocb(1000, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_header_t *jfh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfh);
	bzero(jfh, sizeof (*jfh));

	jmc_set_ether_header(je, jfh, NULL);

	jfh->jfh_magic = htonl(0x1DE12345);
	jfh->jfh_type = htonl(JMCBOOT_TYPE_HELLO);
	int len = snprintf((char *)m->b_wptr, 128,
	    "Hello!  I'd like to buy a ramdisk please.");
	m->b_wptr += len;
	jfh->jfh_len = htonl(len);

	mutex_exit(&je->je_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&je->je_mutex);
}

static void
jmc_send_read(jmc_ether_t *je, mac_client_handle_t mch)
{
	mutex_exit(&je->je_mutex);
	mblk_t *m;
	if ((m = allocb(1000, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_read_t *jfr = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfr);
	bzero(jfr, sizeof (*jfr));

	jmc_set_ether_header(je, &jfr->jfr_header, je->je_server);

	jfr->jfr_header.jfh_magic = htonl(0x1DE12345);
	jfr->jfr_header.jfh_type = htonl(JMCBOOT_TYPE_READ);
	jfr->jfr_header.jfh_len = htonl(JMCBOOT_LEN_READ);
	jfr->jfr_offset = htonll(je->je_offset);
	jfr->jfr_length = htonll(1024);

	mutex_exit(&je->je_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&je->je_mutex);
}

static void
jmc_send_finished(jmc_ether_t *je, mac_client_handle_t mch)
{
	mutex_exit(&je->je_mutex);
	mblk_t *m;
	if ((m = allocb(1000, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_header_t *jfh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfh);
	bzero(jfh, sizeof (*jfh));

	jmc_set_ether_header(je, jfh, je->je_server);

	jfh->jfh_magic = htonl(0x1DE12345);
	jfh->jfh_type = htonl(JMCBOOT_TYPE_FINISHED);
	jfh->jfh_len = htonl(JMCBOOT_LEN_FINISHED);

	mutex_exit(&je->je_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&je->je_mutex);
}

static mblk_t *
jmc_next(jmc_ether_t *je)
{
	mblk_t *m;

	if ((m = je->je_q) != NULL) {
		je->je_q = m->b_next;
		m->b_next = NULL;
		VERIFY3U(MBLKL(m), >=, sizeof (jmc_frame_header_t));
	}

	return (m);
}

static bool
jmc_ramdisk_create(jmc_ether_t *je)
{
	int r;

	if (je->je_rd_ctl == 0) {
		int flag = FEXCL | FREAD | FWRITE;

		printf("opening ramdisk control device\n");
		if ((r = ldi_open_by_name("/devices/pseudo/ramdisk@1024:ctl",
		    flag, kcred, &je->je_rd_ctl, je->je_li)) != 0) {
			printf("control device open failure %d\n", r);
			return (false);
		}
	}

	int rv;
	struct rd_ioctl ri;
	bzero(&ri, sizeof (ri));
	(void) snprintf(ri.ri_name, sizeof (ri.ri_name), "rpool");
	ri.ri_size = je->je_ramdisk_size;

	printf("creating ramdisk of size %lu\n", je->je_ramdisk_size);
	if ((r = ldi_ioctl(je->je_rd_ctl, RD_CREATE_DISK, (intptr_t)&ri,
	    FWRITE | FKIOCTL, kcred, &rv)) != 0) {
		printf("ramdisk create failure %d\n", r);
		return (false);
	}

	printf("opening ramdisk device\n");
	if ((r = ldi_open_by_name("/devices/pseudo/ramdisk@1024:rpool",
	    FREAD | FWRITE, kcred, &je->je_rd_disk, je->je_li)) != 0) {
		printf("ramdisk open failure\n");
		return (false);
	}

	return (true);
}

static int
jmc_ether_turn(jmc_ether_t *je, mac_client_handle_t mch)
{
	mblk_t *m;

	if (je->je_reset) {
		/*
		 * XXX
		 */
		panic("need reset");
	}

	switch (je->je_state) {
	case JMCBOOT_STATE_REST:
		/*
		 * First, check to see if we have any offers.
		 */
		while ((m = jmc_next(je)) != NULL) {
			jmc_frame_header_t *jfh = (void *)m->b_rptr;

			if (ntohl(jfh->jfh_type) != JMCBOOT_TYPE_OFFER) {
				freemsg(m);
				continue;
			}

			jmc_frame_offer_t *jfo = (void *)m->b_rptr;
			VERIFY3U(MBLKL(m), >=, sizeof (*jfo));

			if (ntohll(jfo->jfo_ramdisk_size) < 1024 * 1024) {
				freemsg(m);
				continue;
			}

			bcopy(&jfh->jfh_ether.ether_shost,
			    &je->je_server, ETHERADDRL);

			printf("received offer from "
			    "%02x:%02x:%02x:%02x:%02x:%02x "
			    " -- size %lu\n",
			    je->je_server[0],
			    je->je_server[1],
			    je->je_server[2],
			    je->je_server[3],
			    je->je_server[4],
			    je->je_server[5],
			    ntohll(jfo->jfo_ramdisk_size));

			/*
			 * Create a ramdisk of this size.
			 */
			je->je_ramdisk_size = ntohll(jfo->jfo_ramdisk_size);
			if (!jmc_ramdisk_create(je)) {
				/*
				 * If we could not open the ramdisk, just panic
				 * for now.
				 */
				panic("could not open ramdisk");
			}

			je->je_offset = 0;
			je->je_state = JMCBOOT_STATE_READING;
			je->je_download_start = gethrtime();
			freemsg(m);
			jmc_send_read(je, mch);
			return (0);
		}

		if (je->je_last_hello == 0 ||
		    gethrtime() - je->je_last_hello > 1000000000UL) {
			/*
			 * Send a broadcast frame at most once per second.
			 */
			printf("hello...\n");
			jmc_send_hello(je, mch);
			je->je_last_hello = gethrtime();
		}
		return (0);

	case JMCBOOT_STATE_READING:
		if (je->je_last_status == 0) {
			printf("\n");
		}
		if (je->je_last_status == 0 ||
		    gethrtime() - je->je_last_status > 1000000000UL) {
			uint_t pct = 100UL *
			    je->je_offset / je->je_ramdisk_size;
			printf("\r receiving %016lx / %016lx (%3u%%)    \r",
			    je->je_offset, je->je_ramdisk_size, pct);
			je->je_last_status = gethrtime();
		}

		/*
		 * Check to see if we have any data messages.
		 */
		while ((m = jmc_next(je)) != NULL) {
			jmc_frame_header_t *jfh = (void *)m->b_rptr;

			if (ntohl(jfh->jfh_type) != JMCBOOT_TYPE_DATA) {
				freemsg(m);
				continue;
			}

			jmc_frame_data_t *jfd = (void *)m->b_rptr;
			VERIFY3U(MBLKL(m), >=, sizeof (*jfd));

			if (ntohll(jfd->jfd_offset) != je->je_offset) {
				/*
				 * XXX This is not the one we were expecting...
				 */
				freemsg(m);
				continue;
			}

			if (ntohl(jfd->jfd_header.jfh_len) == 8) {
				/*
				 * XXX Just the offset means we have reached
				 * EOF.
				 */
				uint64_t secs =
				    (gethrtime() - je->je_download_start) /
				    1000000000UL;
				printf("reached EOF at offset %lu "
				    "after %lu seconds\n",
				    je->je_offset, secs);

				freemsg(m);
				je->je_state = JMCBOOT_STATE_FINISHED;
				jmc_send_finished(je, mch);
				return (0);
			}

			/*
			 * Trim out the header, leaving only the data we
			 * received.
			 */
			m->b_rptr += sizeof (*jfd);

			/*
			 * Write the data into the ramdisk at the expected
			 * offset.
			 */
			iovec_t iov[32];
			uio_t uio;

			bzero(&uio, sizeof (uio));
			bzero(iov, sizeof (*iov));

			size_t total = 0;
			mblk_t *w = m;
			while (w != NULL) {
				if (MBLKL(w) > 0) {
					iov[uio.uio_iovcnt].iov_base =
					    (void *)w->b_rptr;
					iov[uio.uio_iovcnt].iov_len =
					    MBLKL(w);
					total += MBLKL(w);

					uio.uio_iovcnt++;
					VERIFY3U(uio.uio_iovcnt, <, 32);
				}

				w = w->b_cont;
			}
			VERIFY3U(total, ==, ntohl(jfd->jfd_header.jfh_len) - 8);

			uio.uio_iov = iov;
			uio.uio_loffset = je->je_offset;
			uio.uio_segflg = UIO_SYSSPACE;
			uio.uio_resid = total;

			int r;
			if ((r = ldi_write(je->je_rd_disk, &uio, kcred)) != 0) {
				/*
				 * XXX
				 */
				panic("write failure pos %lu: %d\n",
				    je->je_offset, r);
			}

			if (uio.uio_resid != 0) {
				panic("write resid at %lu was %ld",
				    je->je_offset, uio.uio_resid);
			}

			je->je_last_hello = gethrtime();
			je->je_offset += total;
			jmc_send_read(je, mch);
			freemsg(m);
		}

		if (je->je_last_hello == 0 ||
		    gethrtime() - je->je_last_hello > 1000000000UL) {
			/*
			 * Resend our read request in case it was lost.
			 */
			jmc_send_read(je, mch);
			je->je_last_hello = gethrtime();
		}
		return (0);

	case JMCBOOT_STATE_FINISHED:
		jmc_send_finished(je, mch);
		return (1);

	default:
		panic("unexpected state %d\n", je->je_state);
	}
}

static int
jmc_ether(void)
{
	jmc_ether_t je;
	bzero(&je, sizeof (je));
	je.je_li = ldi_ident_from_anon();
	je.je_state = JMCBOOT_STATE_REST;
	mutex_init(&je.je_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&je.je_cv, NULL, CV_DRIVER, NULL);

	jmc_find_ether_t jfe = {
		.jfe_print_only = B_TRUE,
	};

	/*
	 * First, force everything which can attach to do so.  The device class
	 * is not derived until at least one minor mode is created, so we
	 * cannot walk the device tree looking for a device class of
	 * ESC_NETWORK until everything is attached.
	 */
	printf("attaching stuff...\n");
	(void) ndi_devi_config(ddi_root_node(), NDI_CONFIG | NDI_DEVI_PERSIST |
	    NDI_NO_EVENT | NDI_DRV_CONF_REPROBE);

	/*
	 * We need to find and attach the Ethernet device we want.
	 */
	printf("Ethernet interfaces:\n");
	ddi_walk_devs(ddi_root_node(), jmc_find_ether, &jfe);
	printf("\n");

	/*
	 * XXX For now, assume "vioif0" is the business.
	 */
	int r;
	mac_handle_t mh;
	const char *mname = "vioif0";
	printf("opening %s handle\n", mname);
	if ((r = mac_open(mname, &mh)) != 0) {
		printf("mac_open failed with %d\n", r);
		return (-1);
	}

	printf("opening client handle\n");
	mac_client_handle_t mch;
	if ((r = mac_client_open(mh, &mch, NULL,
	    MAC_OPEN_FLAGS_USE_DATALINK_NAME)) != 0) {
		printf("failed to open client handle with %d\n", r);
		mac_close(mh);
		return (-1);
	}

	/*
	 * Lets find out our MAC address!
	 */
	mac_unicast_primary_get(mh, je.je_macaddr);
	printf("MAC address is %02X:%02X:%02X:%02X:%02X:%02X\n",
	    je.je_macaddr[0],
	    je.je_macaddr[1],
	    je.je_macaddr[2],
	    je.je_macaddr[3],
	    je.je_macaddr[4],
	    je.je_macaddr[5]);

	/*
	 * Add unicast handle?
	 */
	mac_unicast_handle_t muh;
	mac_diag_t diag;
	if (mac_unicast_add(mch, NULL, MAC_UNICAST_PRIMARY, &muh, 0, &diag) !=
	    0) {
		printf("mac unicast add failure (diag %d)\n", diag);
		mac_client_close(mch, 0);
		mac_close(mh);
		return (-1);
	}

	/*
	 * Listen for frames...
	 */
	mac_rx_set(mch, jmc_ether_rx, &je);
	mutex_enter(&je.je_mutex);
	printf("listening for packets...\n");
	for (;;) {
		if (jmc_ether_turn(&je, mch) == 1) {
			printf("all done!\n");
			break;
		}

		(void) cv_reltimedwait(&je.je_cv, &je.je_mutex,
		    drv_usectohz(1000000), TR_SEC);
	}
	mutex_exit(&je.je_mutex);

	printf("closing unicast handle\n");
	(void) mac_unicast_remove(mch, muh);
	printf("closing client handle\n");
	mac_rx_clear(mch);
	mac_client_close(mch, 0);
	printf("closing handle\n");
	mac_close(mh);

	if (je.je_rd_disk != 0) {
		printf("closing ramdisk\n");
		if ((r = ldi_close(je.je_rd_disk, FREAD | FWRITE,
		    kcred)) != 0) {
			printf("ramdisk close failure %d\n", r);
		}
		je.je_rd_disk = 0;
	}
	if (je.je_rd_ctl != 0) {
		printf("closing ramdisk control\n");
		if ((r = ldi_close(je.je_rd_ctl, FREAD | FWRITE | FEXCL,
		    kcred)) != 0) {
			printf("ramdisk control close failure %d\n", r);
		}
		je.je_rd_ctl = 0;
	}

	/*
	 * XXX We need to free je_q here.
	 */
	mutex_destroy(&je.je_mutex);
	cv_destroy(&je.je_cv);

	return (0);
}

int
jmcboot(char *path, size_t pathsz)
{
	printf("in jmcboot!\n");
	if (jmc_ether() == 0) {
		(void) snprintf(path, pathsz, "/pseudo/ramdisk@1024:rpool");
		return (0);
	}

	return (-1);
}
