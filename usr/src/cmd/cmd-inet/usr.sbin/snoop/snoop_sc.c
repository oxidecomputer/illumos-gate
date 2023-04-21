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
 * Copyright 2023 Oxide Computer Company
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/tofino.h>
#include "snoop.h"

#define	CODELEN 32

int
interpret_sidecar(int flags, schdr_t *sc, int len)
{
	char *next_hdr;
	uint32_t vlan_id, vlan_pri;
	ushort_t ether_type;
	bool_t display_vlan = FALSE;
	char code[CODELEN];

	if (len < sizeof (schdr_t))
		return (len);

	next_hdr = (char *)sc + sizeof (schdr_t);
	len -= sizeof (schdr_t);
	ether_type = ntohs(sc->sc_ethertype);

	/*
	 * There is no dedicated snoop module for processing 802.1Q VLAN
	 * headers.  These headers usually appear immediately after the main
	 * 14-byte ethernet header and are processed by the the snoop_ether
	 * module.  When the tofino adds a sidecar header to a packet, it is
	 * inserted after the ethernet header, separating it from the VLAN
	 * header.  Since snoop_ether doesn't display the VLAN information in
	 * that case, we will do it here.
	 */
	if (ether_type == ETHERTYPE_VLAN) {
		struct ether_vlan_extinfo vlan;

		/*
		 * Rather than failing the entire header, it seems like it would
		 * be more useful to dump the sidecar fields and report that the
		 * vlan header is truncated.  However, that doesn't seem to be
		 * standard practice for this tool.
		 */
		if (len < sizeof (struct ether_vlan_extinfo))
			return (len + sizeof (schdr_t));

		bcopy(next_hdr, &vlan, sizeof (struct ether_vlan_extinfo));
		next_hdr = next_hdr + sizeof (struct ether_vlan_extinfo);

		ether_type = ntohs(vlan.ether_type);
		vlan_pri = (uint32_t)VLAN_PRI(ntohs(vlan.ether_tci));
		vlan_id = (uint32_t)VLAN_ID(ntohs(vlan.ether_tci));
		set_vlan_id(vlan_id);
		display_vlan = TRUE;
	}

	switch (sc->sc_code) {
	case SC_FORWARD_FROM_USERSPACE:
		snprintf(code, CODELEN, "FWD_FROM_USERSPACE");
		break;
	case SC_FORWARD_TO_USERSPACE:
		snprintf(code, CODELEN, "FWD_TO_USERSPACE");
		break;
	case SC_ICMP_NEEDED:
		snprintf(code, CODELEN, "ICMP_NEEDED");
		break;
	case SC_ARP_NEEDED:
		snprintf(code, CODELEN, "ARP_NEEDED");
		break;
	case SC_NEIGHBOR_NEEDED:
		snprintf(code, CODELEN, "NDP_NEEDED");
		break;
	case SC_INVALID:
		snprintf(code, CODELEN, "INVALID");
		break;
	default:
		snprintf(code, CODELEN, "UNKNOWN (%d)"), sc->sc_code;
		break;
	}
	if (flags & F_SUM) {
		(void) snprintf(get_sum_line(), MAXLINE,
		    "SIDECAR %s Ingress=%d Egress=%d",
		    code, ntohs(sc->sc_ingress), ntohs(sc->sc_egress));
	}

	if (flags & F_DTAIL) {
		show_header("SC:   ", "Sidecar Header", sizeof (schdr_t));
		show_space();
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Code = 0x%x (%s)", sc->sc_code, code);
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Ingress port = %d", ntohs(sc->sc_ingress));
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Egress port = %d", ntohs(sc->sc_egress));
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Ethertype = %04X (%s)",
		    ntohs(ether_type), print_ethertype(ether_type));
		if (display_vlan) {
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "VLAN ID = %u", vlan_id);
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "VLAN Priority = %u", vlan_pri);
		}

		uint8_t *p = sc->sc_payload;
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Payload = %02x%02x%02x%02x %02x%02x%02x%02x "
		    "%02x%02x%02x%02x %02x%02x%02x%02x",
		    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
		    p[9], p[10], p[11], p[12], p[13], p[14], p[15], p[16]);
		show_space();
	}

	/* go to the next protocol layer */
	switch (ether_type) {
	case ETHERTYPE_IP:
		(void) interpret_ip(flags, (struct ip *)next_hdr, len);
		break;
	case ETHERTYPE_IPV6:
		(void) interpret_ipv6(flags, (ip6_t *)next_hdr, len);
		break;
	case ETHERTYPE_ARP:
		interpret_arp(flags, (struct arphdr *)next_hdr, len);
		break;
	}

	return (len);
}
