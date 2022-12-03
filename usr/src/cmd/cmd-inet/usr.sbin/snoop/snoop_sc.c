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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/udp.h>
#include <sys/tofino.h>
#include "snoop.h"

#define	CODELEN 32

int
interpret_sidecar(int flags, struct schdr *sc, int iplen, int len)
{
	char *data;
	int udplen;
	int sunrpc;
	char *pname;
	char code[CODELEN];

	if (len < sizeof (struct schdr))
		return (len);

	data = (char *)sc + sizeof (struct schdr);
	len -= sizeof (struct schdr);

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
		snprintf(code, CODELEN, "Code=0x%x", sc->sc_code);
		break;
	}
	(void) snprintf(get_sum_line(), MAXLINE,
	    "SIDECAR %s Ingress=%d Egress=%d",
	    code, ntohs(sc->sc_ingress), ntohs(sc->sc_egress));

	if (flags & F_DTAIL) {
		show_header("SC:   ", "Sidecar Header", sizeof (struct schdr));
		show_space();
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Code = 0x%x (%s)", sc->sc_code, code);
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Ingress port = %d", ntohs(sc->sc_ingress));
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Egress port = %d", ntohs(sc->sc_egress));
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Ethertype = %04X (%s)",
		    ntohs(sc->sc_ethertype), print_ethertype(sc->sc_ethertype));

		uint8_t *p = sc->sc_payload;
		(void) snprintf(get_line(0, 0), get_line_remain(),
		    "Payload = %02x%02x%02x%02x %02x%02x%02x%02x "
		    "%02x%02x%02x%02x %02x%02x%02x%02x",
		    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
		    p[9], p[10], p[11], p[12], p[13], p[14], p[15], p[16]);
		show_space();
	}

	/* go to the next protocol layer */
	switch (ntohs(sc->sc_ethertype)) {
	case ETHERTYPE_IP:
		(void) interpret_ip(flags, (struct ip *)data, len);
		break;
	case ETHERTYPE_IPV6:
		(void) interpret_ipv6(flags, (ip6_t *)data, len);
		break;
	case ETHERTYPE_ARP:
		interpret_arp(flags, (struct arphdr *)data, len);
		break;
	}

	return (len);
}
