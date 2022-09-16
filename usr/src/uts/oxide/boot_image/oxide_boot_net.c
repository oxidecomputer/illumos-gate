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
 * Copyright 2022 Oxide Computer Company
 */

/*
 * Oxide Image Boot: Network image source.  Fetches an appropriate ramdisk
 * image from a local boot server over Ethernet.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <net/if.h>
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
#include <sys/ramdisk.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>
#include <sys/time.h>
#include <sys/sysmacros.h>

#include "oxide_boot.h"

/*
 * Ethernet boot protocol definitions.
 */
#define	JMCBOOT_TYPE_HELLO		0x9001
#define	JMCBOOT_TYPE_OFFER		0x9102
#define	JMCBOOT_TYPE_READ		0x9003
#define	JMCBOOT_TYPE_DATA		0x9104
#define	JMCBOOT_TYPE_FINISHED		0x9005
#define	JMCBOOT_TYPE_RESET		0x9106

#define	JMCBOOT_ETHERTYPE		0x1DE0
#define	JMCBOOT_MAGIC			0x1DE12345

#define	JMCBOOT_READ_SZ			1024

#define	JMCBOOT_NOFFSETS		128

typedef struct jmc_frame_header {
	struct ether_header jfh_ether;
	uint32_t jfh_magic;
	uint32_t jfh_type;
	uint32_t jfh_len;
} __packed jmc_frame_header_t;

typedef struct jmc_frame_offer {
	jmc_frame_header_t jfo_header;
	uint64_t jfo_ramdisk_size;
	uint64_t jfo_ramdisk_data_size;
	uint8_t jfo_sha256[32];
	char jfo_dataset[128];
} __packed jmc_frame_offer_t;

#define	JMCBOOT_LEN_OFFER	(sizeof (jmc_frame_offer_t) - \
				sizeof (jmc_frame_header_t))

typedef struct jmc_frame_data {
	jmc_frame_header_t jfd_header;
	uint64_t jfd_offset;
} __packed jmc_frame_data_t;

typedef struct jmc_frame_read {
	jmc_frame_header_t jfr_header;
	uint64_t jfr_noffsets;
	uint64_t jfr_offsets[JMCBOOT_NOFFSETS];
} __packed jmc_frame_read_t;

#define	JMCBOOT_LEN_READ	(sizeof (jmc_frame_read_t) - \
				sizeof (jmc_frame_header_t))

#define	JMCBOOT_LEN_RESET	0
#define	JMCBOOT_LEN_FINISHED	0

/*
 * Ethernet protocol state machine.
 */
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

	bool je_eof;
	uint64_t je_offsets[JMCBOOT_NOFFSETS];
	hrtime_t je_offset_time[JMCBOOT_NOFFSETS];
	uint64_t je_offset;
	uint64_t je_data_size;
	mblk_t *je_q;
} jmc_ether_t;

typedef struct jmc_find_ether {
	boolean_t jfe_print_only;
	char jfe_linkname[LIFNAMSIZ];
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

	/*
	 * If we have not picked a NIC yet, accept any NIC.  If we see either a
	 * vioif NIC or an Intel NIC, prefer those for now.
	 */
	if (jfe->jfe_linkname[0] == '\0' ||
	    strncmp(ddi_driver_name(dip), "igb", 3) == 0 ||
	    strncmp(ddi_driver_name(dip), "e1000g", 6) == 0 ||
	    strncmp(ddi_driver_name(dip), "vioif", 5) == 0) {
		(void) snprintf(jfe->jfe_linkname, sizeof (jfe->jfe_linkname),
		    "%s%d", ddi_driver_name(dip), i_ddi_devi_get_ppa(dip));
	}

	return (DDI_WALK_CONTINUE);
}

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
				freemsg(m);
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
				freemsg(m);
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
	jfh->jfh_ether.ether_type = htons(JMCBOOT_ETHERTYPE);
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
	if ((m = allocb(ETHERMTU, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_header_t *jfh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfh);
	bzero(jfh, sizeof (*jfh));

	jmc_set_ether_header(je, jfh, NULL);

	jfh->jfh_magic = htonl(JMCBOOT_MAGIC);
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
	if ((m = allocb(ETHERMTU, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_read_t *jfr = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfr);
	bzero(jfr, sizeof (*jfr));

	jmc_set_ether_header(je, &jfr->jfr_header, je->je_server);

	jfr->jfr_header.jfh_magic = htonl(JMCBOOT_MAGIC);
	jfr->jfr_header.jfh_type = htonl(JMCBOOT_TYPE_READ);
	jfr->jfr_header.jfh_len = htonl(JMCBOOT_LEN_READ);

	uint64_t noffsets = 0;
	hrtime_t now = gethrtime();
	for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
		if (je->je_offsets[n] == UINT64_MAX) {
			continue;
		}

		if (je->je_offset_time[n] != 0 &&
		    now - je->je_offset_time[n] < SEC2NSEC(1)) {
			continue;
		}

		je->je_offset_time[n] = now;
		jfr->jfr_offsets[n] = htonll(je->je_offsets[n]);
		noffsets++;
	}

	if (noffsets == 0) {
		freemsg(m);
		return;
	}

	jfr->jfr_noffsets = htonll(noffsets);

	mutex_exit(&je->je_mutex);
	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
	mutex_enter(&je->je_mutex);
}

static void
jmc_send_finished(jmc_ether_t *je, mac_client_handle_t mch)
{
	mutex_exit(&je->je_mutex);
	mblk_t *m;
	if ((m = allocb(ETHERMTU, 0)) == NULL) {
		mutex_enter(&je->je_mutex);
		printf("allocb failure\n");
		return;
	}
	mutex_enter(&je->je_mutex);

	jmc_frame_header_t *jfh = (void *)m->b_wptr;
	m->b_wptr += sizeof (*jfh);
	bzero(jfh, sizeof (*jfh));

	jmc_set_ether_header(je, jfh, je->je_server);

	jfh->jfh_magic = htonl(JMCBOOT_MAGIC);
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

static int
jmc_ether_turn(oxide_boot_t *oxb, jmc_ether_t *je, mac_client_handle_t mch)
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

			/*
			 * Make sure the dataset name is correctly
			 * null-terminated.
			 */
			if (jfo->jfo_dataset[127] != '\0') {
				freemsg(m);
				continue;
			}

			/*
			 * The ramdisk has a size, and the image that we will
			 * download into the beginning of the ramdisk has an
			 * equal-or-smaller size.
			 */
			size_t size = ntohll(jfo->jfo_ramdisk_size);
			size_t data_size = ntohll(jfo->jfo_ramdisk_data_size);
			if (size < 1024 * 1024 || data_size < 1024 * 1024 ||
			    data_size > size) {
				freemsg(m);
				continue;
			}

			if (!oxide_boot_ramdisk_set_csum(oxb, jfo->jfo_sha256,
			    sizeof (jfo->jfo_sha256))) {
				/*
				 * If this image does not match the cpio
				 * archive, so we ignore it.
				 */
				printf("ignoring offer (checksum mismatch)\n");
				freemsg(m);
				continue;
			}

			bcopy(&jfh->jfh_ether.ether_shost,
			    &je->je_server, ETHERADDRL);

			printf("received offer from "
			    "%02x:%02x:%02x:%02x:%02x:%02x "
			    " -- size %lu data size %lu dataset %s\n",
			    je->je_server[0],
			    je->je_server[1],
			    je->je_server[2],
			    je->je_server[3],
			    je->je_server[4],
			    je->je_server[5],
			    size,
			    data_size,
			    jfo->jfo_dataset);

			/*
			 * Create a ramdisk of this size.
			 */
			if (!oxide_boot_ramdisk_create(oxb, size)) {
				/*
				 * If we could not open the ramdisk, just panic
				 * for now.
				 */
				panic("could not open ramdisk");
			}

			if (!oxide_boot_ramdisk_set_dataset(oxb,
			    jfo->jfo_dataset)) {
				panic("could not set ramdisk metadata");
			}

			je->je_offset = 0;
			je->je_data_size = data_size;
			je->je_state = JMCBOOT_STATE_READING;
			je->je_download_start = gethrtime();
			freemsg(m);
			return (0);
		}

		if (je->je_last_hello == 0 ||
		    gethrtime() - je->je_last_hello > SEC2NSEC(4)) {
			/*
			 * Send a broadcast frame every four seconds.
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
		    gethrtime() - je->je_last_status > SEC2NSEC(1)) {
			uint_t pct = 100UL *
			    je->je_offset / je->je_data_size;
			printf("\r receiving %016lx / %016lx (%3u%%)    \r",
			    je->je_offset, je->je_data_size, pct);
			je->je_last_status = gethrtime();
		}

		/*
		 * Check to see if we have finished all work.
		 */
		if (je->je_eof || je->je_offset >= je->je_data_size) {
			bool finished = true;
			for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
				if (je->je_offsets[n] != UINT64_MAX) {
					finished = false;
					break;
				}
			}

			if (finished) {
				uint64_t secs =
				    (gethrtime() - je->je_download_start) /
				    SEC2NSEC(1);
				printf("reached EOF at offset %lu "
				    "after %lu seconds           \n",
				    je->je_offset, secs);

				je->je_state = JMCBOOT_STATE_FINISHED;
				return (0);
			}
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

			/*
			 * Check through our list of offsets:
			 */
			uint64_t offset = ntohll(jfd->jfd_offset);
			bool found = false;
			for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
				if (offset == je->je_offsets[n]) {
					found = true;
					je->je_offsets[n] = UINT64_MAX;
					break;
				}
			}

			if (!found) {
				/*
				 * XXX This is not one we were expecting...
				 */
				printf("dropped data packet for offset %lu\n",
				    offset);
				freemsg(m);
				continue;
			}

			if (ntohl(jfd->jfd_header.jfh_len) == 8) {
				/*
				 * XXX Just the offset means we have reached
				 * EOF.  We still have to wait for all of our
				 * in flight requests to be serviced.
				 */
				je->je_eof = true;
				freemsg(m);
				continue;
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
			bzero(iov, sizeof (*iov));

			size_t total = 0;
			uint_t niov = 0;
			mblk_t *w = m;
			while (w != NULL) {
				if (MBLKL(w) > 0) {
					iov[niov].iov_base = (void *)w->b_rptr;
					iov[niov].iov_len = MBLKL(w);
					total += MBLKL(w);

					VERIFY3U(niov, <, ARRAY_SIZE(iov));
					niov++;
				}

				w = w->b_cont;
			}
			VERIFY3U(total, ==, ntohl(jfd->jfd_header.jfh_len) - 8);

			if (!oxide_boot_ramdisk_write(oxb, iov, niov, offset)) {
				panic("write failure pos %lu", offset);
			}

			freemsg(m);
		}

		/*
		 * Issue reads for offsets we still need if there are
		 * any available slots.
		 */
		bool send = false;
		hrtime_t now = gethrtime();
		if (!je->je_eof && je->je_offset < je->je_data_size) {
			/*
			 * Check to see if we have drained our existing
			 * requests before adding more...
			 */
			bool empty = true;
			for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
				if (je->je_offsets[n] != UINT64_MAX) {
					empty = false;
					break;
				}
			}

			if (empty) {
				for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
					if (je->je_offsets[n] != UINT64_MAX) {
						/*
						 * This slot is in use.
						 */
						continue;
					}

					send = true;
					je->je_offsets[n] = je->je_offset;
					je->je_offset_time[n] = 0;

					je->je_offset += JMCBOOT_READ_SZ;
				}
			}
		}

		/*
		 * Check to see if we need to send a packet with our
		 * outstanding offset list.
		 */
		for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
			if (je->je_offsets[n] == UINT64_MAX) {
				continue;
			}

			if (je->je_offset_time[n] == 0 ||
			    now - je->je_offset_time[n] > SEC2NSEC(1)) {
				send = true;
				break;
			}
		}

		if (send) {
			jmc_send_read(je, mch);
		}
		return (0);

	case JMCBOOT_STATE_FINISHED:
		jmc_send_finished(je, mch);
		if (!oxide_boot_ramdisk_set_len(oxb, je->je_offset)) {
			panic("could not set final image length");
		}
		return (1);

	default:
		panic("unexpected state %d\n", je->je_state);
	}
}

bool
oxide_boot_net(oxide_boot_t *oxb)
{
	jmc_ether_t je;
	bzero(&je, sizeof (je));
	je.je_state = JMCBOOT_STATE_REST;
	je.je_offset = 0;
	for (uint_t n = 0; n < JMCBOOT_NOFFSETS; n++) {
		je.je_offsets[n] = UINT64_MAX;
	}
	mutex_init(&je.je_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&je.je_cv, NULL, CV_DRIVER, NULL);

	jmc_find_ether_t jfe = {
		.jfe_print_only = B_TRUE,
	};

	printf("TRYING: boot net\n");

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

	if (jfe.jfe_linkname[0] == '\0') {
		printf("did not find any Ethernet!\n");
		return (false);
	}

	int r;
	mac_handle_t mh;
	printf("opening %s handle\n", jfe.jfe_linkname);
	if ((r = mac_open(jfe.jfe_linkname, &mh)) != 0) {
		printf("mac_open failed with %d\n", r);
		return (false);
	}

	printf("opening client handle\n");
	mac_client_handle_t mch;
	if ((r = mac_client_open(mh, &mch, NULL,
	    MAC_OPEN_FLAGS_USE_DATALINK_NAME)) != 0) {
		printf("failed to open client handle with %d\n", r);
		mac_close(mh);
		return (false);
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
		return (false);
	}

	/*
	 * Listen for frames...
	 */
	mac_rx_set(mch, jmc_ether_rx, &je);
	mutex_enter(&je.je_mutex);
	printf("listening for packets...\n");
	for (;;) {
		if (jmc_ether_turn(oxb, &je, mch) == 1) {
			printf("all done!\n");
			break;
		}

		(void) cv_reltimedwait(&je.je_cv, &je.je_mutex,
		    drv_usectohz(50 * 1000), TR_MICROSEC);
	}
	mutex_exit(&je.je_mutex);

	printf("closing unicast handle\n");
	(void) mac_unicast_remove(mch, muh);
	printf("closing client handle\n");
	mac_rx_clear(mch);

	printf("freeing remaining messages\n");
	mblk_t *t;
	while ((t = je.je_q) != NULL) {
		je.je_q = t->b_next;
		t->b_next = NULL;
		freemsg(t);
	}

	mac_client_close(mch, 0);
	printf("closing handle\n");
	mac_close(mh);

	mutex_destroy(&je.je_mutex);
	cv_destroy(&je.je_cv);

	return (true);
}
