#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/ddi.h>
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

typedef struct jmc_ether {
	kmutex_t je_mutex;
	kcondvar_t je_cv;
	uint64_t je_npkts;
	ether_addr_t je_macaddr;
} jmc_ether_t;

typedef struct jmc_ether_header {
	uint32_t jeh_magic;
	char jeh_message[128];
} __packed jmc_ether_header_t;

static void
jmc_ether_rx(void *arg, mac_resource_handle_t mrh, mblk_t *m,
    boolean_t is_loopback)
{
	jmc_ether_t *je = arg;

	if (is_loopback) {
		goto drop;
	}

	mutex_enter(&je->je_mutex);
	je->je_npkts++;
	cv_broadcast(&je->je_cv);
	mutex_exit(&je->je_mutex);

drop:
	while (m != NULL) {
		mblk_t *next = m->b_next;

		m->b_next = NULL;
		freemsg(m);
		m = next;
	}
}

static void
jmc_announce(jmc_ether_t *je, mac_client_handle_t mch)
{
	mblk_t *m;

	if ((m = allocb(1000, 0)) == NULL) {
		printf("allocb failure\n");
		return;
	}

	struct ether_header *ether = (void *)m->b_rptr;
	m->b_wptr += sizeof (struct ether_header);

	(void) memset(&ether->ether_dhost, 0xFF, ETHERADDRL);
	(void) memcpy(&ether->ether_shost, je->je_macaddr, ETHERADDRL);
	ether->ether_type = htons(0x1DE0);

	jmc_ether_header_t jeh;
	bzero(&jeh, sizeof (jeh));
	jeh.jeh_magic = BE_32(0x1DE12345);
	(void) snprintf(jeh.jeh_message, sizeof (jeh.jeh_message),
	    "Greetings! We have seen %lu frames.", je->je_npkts);

	(void) memcpy(m->b_wptr, &jeh, sizeof (jeh));
	m->b_wptr += sizeof (jeh);

	(void) mac_tx(mch, m, 0, MAC_DROP_ON_NO_DESC, NULL);
}

static void
jmc_ether(void)
{
	jmc_ether_t je;
	bzero(&je, sizeof (je));
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
		return;
	}

	printf("opening client handle\n");
	mac_client_handle_t mch;
	if ((r = mac_client_open(mh, &mch, NULL,
	    MAC_OPEN_FLAGS_USE_DATALINK_NAME)) != 0) {
		printf("failed to open client handle with %d\n", r);
		mac_close(mh);
		return;
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
		return;
	}

	/*
	 * Listen for frames...
	 */
	mac_rx_set(mch, jmc_ether_rx, &je);
	mutex_enter(&je.je_mutex);
	printf("listening for packets...\n");
	hrtime_t last_bcast = 0;
	uint64_t last_npkts = 0;
	while (je.je_npkts < 100) {
		if (last_bcast == 0 ||
		    gethrtime() - last_bcast > 1000000000UL) {
			/*
			 * Send a broadcast frame at most once per second.
			 */
			printf("announce...\n");
			jmc_announce(&je, mch);
			last_bcast = gethrtime();
		}

		if (last_npkts != je.je_npkts) {
			printf("npkts %lu -> %lu!\n", last_npkts, je.je_npkts);
			last_npkts = je.je_npkts;
		} else {
			printf("waiting...\n");
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

	mutex_destroy(&je.je_mutex);
	cv_destroy(&je.je_cv);
}

int
jmcboot(void)
{
	printf("in jmcboot!\n");
	jmc_ether();
	return (0);
}
