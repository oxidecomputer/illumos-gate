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
#include <sys/ilstr.h>

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

static struct sidecar_code {
	char *scc_long;
	char *scc_short;
} sidecar_codes[UINT8_MAX + 1] = {
	[SC_FORWARD_FROM_USERSPACE] =	{ "FWD_FROM_USERSPACE",	"FOUT" },
	[SC_FORWARD_TO_USERSPACE] =	{ "FWD_TO_USERSPACE",	"FWIN" },
	[SC_ICMP_NEEDED] =		{ "ICMP_NEEDED",	"ICMP" },
	[SC_ARP_NEEDED] =		{ "ARP_NEEDED",		"ARPN" },
	[SC_NEIGHBOR_NEEDED] =		{ "NDP_NEEDED",		"NDPN" },
	[SC_INVALID] =			{ "INVALID",		"INVL" },
};

int
interpret_sidecar(int flags, schdr_t *sc, int iplen, int len)
{
	char *data;
	int udplen;
	int sunrpc;
	char *pname;
	char code[CODELEN];
	struct sidecar_code *scc;
	uint_t ingress, egress;

	if (len < sizeof (schdr_t))
		return (len);

	data = (char *)sc + sizeof (schdr_t);
	len -= sizeof (schdr_t);

	scc = &sidecar_codes[sc->sc_code & UINT8_MAX];
	ingress = ntohs(sc->sc_ingress);
	egress = ntohs(sc->sc_egress);

	if (flags & F_ALLSUM) {
		ilstr_t s;

		ilstr_init_prealloc(&s, get_sum_line(), MAXLINE);

		ilstr_append_str(&s, "SIDECAR ");
		if (scc->scc_long != NULL) {
			ilstr_append_str(&s, scc->scc_long);
		} else {
			ilstr_aprintf(&s, "Code=0x%x", sc->sc_code);
		}
		ilstr_aprintf(&s, "  Ingress=%d Egress=%d",
		    ntohs(sc->sc_ingress), ntohs(sc->sc_egress));

		ilstr_fini(&s);
	} else if (flags & F_SUM) {
		ilstr_t *p = get_prefix();

		ilstr_append_str(p, "SC/");
		if (scc->scc_short != NULL) {
			ilstr_aprintf(p, "%-4s", scc->scc_short);
		} else {
			ilstr_aprintf(p, "0x%02x", sc->sc_code);
		}
		if (ingress != 0) {
			ilstr_aprintf(p, "-i%03d", ingress);
		}
		if (egress != 0) {
			ilstr_aprintf(p, "-e%03d", egress);
		}
		ilstr_append_str(p, ": ");
	}

	if (flags & F_DTAIL) {
		show_header("SC:   ", "Sidecar Header", sizeof (schdr_t));
		show_space();
		if (scc->scc_long != NULL) {
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Code = 0x%x (%s)", sc->sc_code, scc->scc_long);
		} else {
			(void) snprintf(get_line(0, 0), get_line_remain(),
			    "Code = 0x%x", sc->sc_code);
		}
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
