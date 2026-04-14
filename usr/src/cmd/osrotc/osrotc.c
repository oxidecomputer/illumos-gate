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
 * Userspace tool for the Oxide OS RoT driver.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <openssl/asn1t.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <os_rot.h>

#include "osrotc.h"

#define	EXIT_USAGE	2

/* BEGIN CSTYLED */
IMPLEMENT_ASN1_FUNCTIONS(DICE_FWID)
IMPLEMENT_ASN1_FUNCTIONS(DICE_TCBINFO)
/* END CSTYLED */

static void __NORETURN
usage(const char *format, ...)
{
	const char *progname = getprogname();

	if (format != NULL) {
		va_list ap;

		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
		(void) fputc('\n', stderr);
	}

	(void) fprintf(stderr, "usage:  %s <subcommand> [<args>]\n\n",
	    progname);

	(void) fprintf(stderr,
	    "\tmeasurement-set [-a]\n"
	    "\t\tPrint the full set of measurements across every layer.\n"
	    "\t\tBy default, only measurements for Oxide software are\n"
	    "\t\tshown.\n\n"
	    "\t\t-a\t\tshow all measurements, including AMD firmware\n"
	    "\n");

	(void) fprintf(stderr,
	    "\tcert-chain [-v | -w file]\n"
	    "\t\tPrint the certificate chain used to verify attestations\n"
	    "\t\tproduced by the attest subcommand, including the leaf\n"
	    "\t\tcertificate used to sign them. By default, show each\n"
	    "\t\tcertificate's serial, issuer, subject, and a summary of\n"
	    "\t\tthe TCBInfo extension (FWIDs and flags), if present.\n\n"
	    "\t\t-v\t\tverbose output (as printed by openssl\n"
	    "\t\t\t\tx509 -text), along with the decoded\n"
	    "\t\t\t\tTCBInfo extension, if present\n"
	    "\t\t-w file\t\twrite the certificate chain in PEM\n"
	    "\t\t\t\tformat to file instead of printing it\n\n");

	(void) fprintf(stderr,
	    "\tattest -n nonce -w file\n"
	    "\t\tPrint an attestation (384-bit ECDSA signature) over the\n"
	    "\t\tcurrent set of measurements and the given nonce.\n\n"
	    "\t\t-n nonce\trandom 48-byte nonce to guarantee freshness\n"
	    "\t\t-w file\t\twrite the DER-encoded signature to file\n");

	exit(EXIT_USAGE);
}

static void
print_hex(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++)
		printf("%02x", buf[i]);
}

/*
 * Fetch the entire DPE certificate chain into a freshly-allocated buffer.
 * Sets *sizep on success.  Caller must free the result.
 */
static uint8_t *
fetch_cert_chain(int fd, uint32_t *sizep)
{
	os_rot_certs_t hdr = {0};
	os_rot_certs_t *certs;
	size_t sz;
	uint8_t *out;

	if (ioctl(fd, OS_ROT_IOC_GET_CERTS, &hdr) != 0)
		err(EXIT_FAILURE, "OS_ROT_IOC_GET_CERTS (size query) failed");
	if (hdr.osrc_error != OS_ROT_E_OK) {
		errx(EXIT_FAILURE, "failed to get certificate chain size: %s "
		    "(%d)", os_rot_strerror(hdr.osrc_error), hdr.osrc_error);
	}

	if (hdr.osrc_chain_size == 0) {
		*sizep = 0;
		return (NULL);
	}

	sz = sizeof (os_rot_certs_t) + hdr.osrc_chain_size;
	certs = calloc(1, sz);
	if (certs == NULL) {
		err(EXIT_FAILURE,
		    "failed to allocate buffer for cert chain request");
	}

	certs->osrc_chain_size = hdr.osrc_chain_size;
	if (ioctl(fd, OS_ROT_IOC_GET_CERTS, certs) != 0) {
		free(certs);
		err(EXIT_FAILURE, "OS_ROT_IOC_GET_CERTS failed");
	}
	if (certs->osrc_error != OS_ROT_E_OK) {
		os_rot_error_t error = certs->osrc_error;

		free(certs);
		errx(EXIT_FAILURE, "failed to get certificate chain: %s (%d)",
		    os_rot_strerror(error), error);
	}

	out = malloc(certs->osrc_chain_size);
	if (out == NULL) {
		free(certs);
		err(EXIT_FAILURE,
		    "failed to allocate buffer for cert chain output");
	}
	(void) memcpy(out, certs->osrc_chain, certs->osrc_chain_size);
	*sizep = certs->osrc_chain_size;
	free(certs);

	return (out);
}


/*
 * Fetchs the entire DPE certificate chain and parses them into a stack of X509
 * certificates.  Caller must free the result with sk_X509_pop_free().
 */
static STACK_OF(X509) *
get_cert_chain(int fd)
{
	uint8_t *chain;
	uint32_t chain_size;
	STACK_OF(X509) *certs = NULL;
	const uint8_t *certp, *end;
	uint_t cert_count = 0;

	chain = fetch_cert_chain(fd, &chain_size);
	if (chain == NULL || chain_size == 0) {
		warnx("got empty certificate chain");
		return (NULL);
	}

	certs = sk_X509_new_null();
	if (certs == NULL)
		err(EXIT_FAILURE, "failed to allocate cert stack");

	certp = chain;
	end = chain + chain_size;

	while (certp < end) {
		X509 *cert = d2i_X509(NULL, &certp, end - certp);
		if (cert == NULL) {
			warnx("failed to parse certificate %u at offset %zu",
			    cert_count, (size_t)(certp - chain));
			ERR_print_errors_fp(stderr);
			sk_X509_pop_free(certs, X509_free);
			free(chain);
			return (NULL);
		}
		if (!sk_X509_push(certs, cert)) {
			warnx("failed to add certificate %u to stack",
			    cert_count);
			X509_free(cert);
			sk_X509_pop_free(certs, X509_free);
			free(chain);
			return (NULL);
		}
		cert_count++;
	}

	free(chain);
	return (certs);
}

static void
do_attest(int fd, const uint8_t *nonce, uint8_t *sig)
{
	os_rot_attest_t req = {0};

	(void) memcpy(req.osra_nonce, nonce, OS_ROT_HASH_SIZE);

	if (ioctl(fd, OS_ROT_IOC_ATTEST, &req) != 0)
		err(EXIT_FAILURE, "OS_ROT_IOC_ATTEST failed");
	if (req.osra_error != OS_ROT_E_OK) {
		errx(EXIT_FAILURE, "failed to attest: %s (%d)",
		    os_rot_strerror(req.osra_error), req.osra_error);
	}

	(void) memcpy(sig, req.osra_sig, OS_ROT_SIG_SIZE);
}

static void
print_guid(const uint8_t *g)
{
	printf("%02x%02x%02x%02x-"
	    "%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	    g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7],
	    g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static bool
isprint_n(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (!isprint(buf[i]))
			return (false);
	}
	return (true);
}

/*
 * Retrieve the parsed TCBInfo from a certificate, or NULL if absent.
 * Caller must free the result with DICE_TCBINFO_free().
 */
static DICE_TCBINFO *
get_cert_tcbinfo(X509 *cert)
{
	ASN1_OBJECT *obj;
	int ext_idx;
	X509_EXTENSION *ext;
	const ASN1_OCTET_STRING *ext_data;
	const unsigned char *p;
	DICE_TCBINFO *tcbinfo = NULL;

	obj = OBJ_txt2obj(TCBINFO_OID, 1);
	if (obj == NULL)
		return (NULL);

	ext_idx = X509_get_ext_by_OBJ(cert, obj, -1);
	ASN1_OBJECT_free(obj);
	if (ext_idx < 0)
		return (NULL);

	ext = X509_get_ext(cert, ext_idx);
	ext_data = X509_EXTENSION_get_data(ext);
	p = ext_data->data;

	tcbinfo = d2i_DICE_TCBINFO(NULL, &p, ext_data->length);
	if (tcbinfo == NULL) {
		warnx("failed to parse DICE TCBInfo");
		ERR_print_errors_fp(stderr);
	}

	return (tcbinfo);
}

static void
print_cert_tcbinfo(const DICE_TCBINFO *tcbinfo, const char *indent,
    bool verbose)
{
	if (tcbinfo == NULL)
		return;

	printf("%sTCBInfo:\n", indent);

	if (verbose && tcbinfo->vendor != NULL)
		printf("%s  Vendor: %.*s\n", indent,
		    tcbinfo->vendor->length, tcbinfo->vendor->data);

	if (verbose && tcbinfo->model != NULL)
		printf("%s  Model: %.*s\n", indent,
		    tcbinfo->model->length, tcbinfo->model->data);

	if (verbose && tcbinfo->version != NULL)
		printf("%s  Version: %.*s\n", indent,
		    tcbinfo->version->length, tcbinfo->version->data);

	if (verbose && tcbinfo->svn != NULL)
		printf("%s  SVN: %ld\n", indent,
		    ASN1_INTEGER_get(tcbinfo->svn));

	if (verbose && tcbinfo->layer != NULL)
		printf("%s  Layer: %ld\n", indent,
		    ASN1_INTEGER_get(tcbinfo->layer));

	if (verbose && tcbinfo->index != NULL)
		printf("%s  Index: %ld\n", indent,
		    ASN1_INTEGER_get(tcbinfo->index));

	if (tcbinfo->fwids != NULL) {
		int n = sk_DICE_FWID_num(tcbinfo->fwids);

		/*
		 * The type field [9] may contain packed 16-byte GUIDs, one
		 * per FWID, identifying each measured component.
		 * Alternatively, treat as 4-byte (possibly ASCII) tags.
		 */
		const uint8_t *guids = NULL, *tags = NULL;
		int nguids = 0, ntags = 0;
		if (tcbinfo->type != NULL && tcbinfo->type->length == n * 16) {
			guids = tcbinfo->type->data;
			nguids = n;
		} else if (tcbinfo->type != NULL &&
		    tcbinfo->type->length == n * 4) {
			tags = tcbinfo->type->data;
			ntags = n;
		}

		printf("%s  FWIDs:\n", indent);
		for (int i = 0; i < n; i++) {
			DICE_FWID *fwid = sk_DICE_FWID_value(tcbinfo->fwids, i);
			int nid = OBJ_obj2nid(fwid->hashAlg);
			const char *alg = (nid != NID_undef) ?
			    OBJ_nid2sn(nid) : "unknown";
			printf("%s    - %s: ", indent, alg);
			for (int j = 0; j < fwid->digest->length; j++)
				printf("%02x", fwid->digest->data[j]);
			if (i < nguids) {
				printf("\n%s      GUID: ", indent);
				print_guid(guids + i * 16);
			}
			if (i < ntags) {
				const uint8_t *t = tags + i * 4;
				printf("\n%s      Tag: %02x%02x%02x%02x",
				    indent, t[0], t[1], t[2], t[3]);
				if (isprint_n(t, 4))
					printf(" ('%.*s')", 4, t);
			}
			printf("\n");
		}
	}

	if (tcbinfo->flags != NULL) {
		struct {
			int index;
			const char *active;
			const char *inactive;
			bool unexpected;
		} known_flags[] = {
			{
				.index = DICE_TCBINFO_F_NOT_CONFIGURED,
				.active = "Not Configured",
				.unexpected = true
			},
			{
				.index = DICE_TCBINFO_F_NOT_SECURE,
				.active = "Unsecure CPU",
				.inactive = "Secure CPU"
			},
			{
				.index = DICE_TCBINFO_F_RECOVERY,
				.active = "Recovery",
				.unexpected = true
			},
			{
				.index = DICE_TCBINFO_F_DEBUG,
				.active = "Debug / CPU Unlocked",
				.inactive = "No Debug / CPU Locked"
			},
		};
		bool first = true;
		uint32_t flags = 0;
		int end;

		/*
		 * Figure out how many bits we have deducting any bits which are
		 * just trailing padding.
		 */
		end = tcbinfo->flags->length * NBBY;
		if (tcbinfo->flags->flags & ASN1_STRING_FLAG_BITS_LEFT)
			end -= tcbinfo->flags->flags & 0x7;

		/*
		 * The 1.00 version of the spec we otherwise assume is limited
		 * to just 4 defined bits, but later versions expanded it to 32.
		 * Anything beyond 32 is completely unexpected.
		 */
		if (end > 32) {
			warnx("OperationalFlags encountered with %u bits ",
			    end);
		}

		for (int i = 0; i < end; i++)
			if (ASN1_BIT_STRING_get_bit(tcbinfo->flags, i))
				flags |= ((uint32_t)1 << i);

		printf("%s  Flags: 0x%x", indent, flags);

		for (size_t i = 0; i < ARRAY_SIZE(known_flags); i++) {
			bool set = (flags & (1 << known_flags[i].index)) != 0;

			if (!set && known_flags[i].inactive == NULL)
				continue;

			/*
			 * Clear this bit so we're left with only unknown flags
			 * at the end.
			 */
			flags &= ~(1 << known_flags[i].index);

			if (first)
				printf("\n%s         ", indent);
			else
				printf(", ");
			first = false;

			if (set) {
				printf("%s%s", known_flags[i].active,
				    known_flags[i].unexpected ?
				    " (Unexpected)": "");
			} else {
				printf("%s", known_flags[i].inactive);
			}
		}

		/*
		 * Any bits remaining set are ones we don't recognize so let's
		 * explicitly call them out.
		 */
		if (flags != 0)
			printf(", Unknown (0x%x)", flags);

		printf("\n");
	}

	if (verbose && tcbinfo->vendorInfo != NULL) {
		printf("%s  VendorInfo: ", indent);
		for (int i = 0; i < tcbinfo->vendorInfo->length; i++)
			printf("%02x", tcbinfo->vendorInfo->data[i]);
		printf("\n");
	}

	if (verbose && tcbinfo->type != NULL) {
		printf("%s  Type: ", indent);
		for (int i = 0; i < tcbinfo->type->length; i++)
			printf("%02x", tcbinfo->type->data[i]);
		printf("\n");
	}
}

/*
 * Convert a raw ECDSA signature (r || s) to DER format.  Returns a newly
 * allocated buffer containing the DER signature, or NULL on error.  Sets
 * *der_len to the length of the DER signature on success.
 * Caller must free result with OPENSSL_free().
 */
static uint8_t *
ecdsa_sig_to_der(const uint8_t *sigp, size_t sig_len, size_t *der_len)
{
	ECDSA_SIG *sig;
	BIGNUM *r = NULL, *s = NULL;
	uint8_t *buf = NULL;
	int len;

	if ((sig_len % 2) != 0) {
		warnx("Unexpected ECDSA signature length (%zu)", sig_len);
		return (NULL);
	}

	sig = ECDSA_SIG_new();
	if (sig == NULL)
		return (NULL);

	r = BN_bin2bn(sigp, sig_len / 2, NULL);
	s = BN_bin2bn(sigp + sig_len / 2, sig_len / 2, NULL);
	if (r == NULL || s == NULL) {
		BN_free(r);
		BN_free(s);
		ECDSA_SIG_free(sig);
		return (NULL);
	}

	if (!ECDSA_SIG_set0(sig, r, s)) {
		BN_free(r);
		BN_free(s);
		ECDSA_SIG_free(sig);
		return (NULL);
	}

	len = i2d_ECDSA_SIG(sig, &buf);
	ECDSA_SIG_free(sig);

	if (len <= 0)
		return (NULL);

	*der_len = (size_t)len;
	return (buf);
}

static void
print_name(const X509_NAME *name)
{
	const int nids[] = {
		NID_commonName,
		NID_organizationName,
		NID_organizationalUnitName,
	};

	if (name == NULL)
		return;

	for (size_t i = 0; i < ARRAY_SIZE(nids); i++) {
		int last = -1;
		int next = -1;

		while ((next = X509_NAME_get_index_by_NID(name, nids[i],
		    next)) != -1) {
			last = next;
		}

		if (last != -1) {
			X509_NAME_ENTRY *entry =
			    X509_NAME_get_entry(name, last);
			ASN1_STRING *data = X509_NAME_ENTRY_get_data(entry);
			unsigned char *s = NULL;
			int len = ASN1_STRING_to_UTF8(&s, data);

			if (len >= 0) {
				(void) fwrite(s, 1, len, stdout);
				OPENSSL_free(s);
			}
			return;
		}
	}
}

static bool
parse_hex(const char *hex, uint8_t *out, size_t nbytes)
{
	size_t hex_len = strlen(hex);
	if (hex_len != nbytes * 2) {
		warnx("expected %zu hex characters (got %zu)",
		    nbytes * 2, hex_len);
		return (false);
	}
	for (size_t i = 0; i < nbytes; i++) {
		if (sscanf(&hex[i * 2], "%2hhx", &out[i]) != 1) {
			warnx("invalid hex at position %zu", i * 2);
			return (false);
		}
	}
	return (true);
}

/*
 * Checks if the given certificate is the DPE alias certificate which we can
 * identify by its subject CN: "DPE_ALIAS_<unique device id>".
 */
static bool
cert_is_dpe_alias(X509 *cert)
{
	char cn[sizeof (AMD_DPE_ALIAS_CN_PREFIX)] = {0};

	if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert),
	    NID_commonName, cn, sizeof (cn)) <= 0)
		return (false);

	return (strncmp(cn, AMD_DPE_ALIAS_CN_PREFIX,
	    sizeof (AMD_DPE_ALIAS_CN_PREFIX) - 1) == 0);
}

static int
cmd_measurement_set(int fd, int argc, char **argv)
{
	STACK_OF(X509) *certs = NULL;
	uint_t entry = 0;
	bool all = false;
	int start = 0;
	int c;

	optind = 0;
	while ((c = getopt(argc, argv, ":a")) != -1) {
		switch (c) {
		case 'a':
			all = true;
			break;
		case '?':
		default:
			usage("measurement-set: unknown option -%c", optopt);
		}
	}

	if (argc - optind > 0)
		usage("measurement-set: unexpected arguments");

	certs = get_cert_chain(fd);
	if (certs == NULL)
		return (EXIT_FAILURE);

	/*
	 * By default, only show the measurements we extend: the phase 1 image
	 * in the DPE alias certificate and everything layered after it.  With
	 * -a, also include the AMD firmware components that precede it.  If
	 * no alias certificate is found, fall back to showing everything.
	 */
	if (!all) {
		for (int ci = 0; ci < sk_X509_num(certs); ci++) {
			if (cert_is_dpe_alias(sk_X509_value(certs, ci))) {
				start = ci;
				break;
			}
		}
	}

	printf(" ##  %-36s  %-6s  %-96s  %s\n",
	    "COMPONENT (GUID / TAG)", "ALG", "DIGEST", "LAYER NAME");

	for (int ci = start; ci < sk_X509_num(certs); ci++) {
		X509 *cert = sk_X509_value(certs, ci);
		bool is_alias = cert_is_dpe_alias(cert);
		uint_t nfwids, nguids, ntags;
		const uint8_t *guids, *tags;

		DICE_TCBINFO *tcbinfo = get_cert_tcbinfo(cert);
		if (tcbinfo == NULL)
			continue;

		if (tcbinfo->fwids == NULL) {
			warnx("certificate %d has DICE TCBInfo extension with "
			    "no FWIDs, skipping", ci);
			DICE_TCBINFO_free(tcbinfo);
			continue;
		}

		nfwids = sk_DICE_FWID_num(tcbinfo->fwids);
		nguids = ntags = 0;
		guids = tags = NULL;
		if (tcbinfo->type != NULL &&
		    tcbinfo->type->length ==
		    nfwids * AMD_DICE_LAYER_FWID_TYPE_LEN) {
			guids = tcbinfo->type->data;
			nguids = nfwids;
		} else if (tcbinfo->type != NULL &&
		    tcbinfo->type->length ==
		    nfwids * AMD_DPE_LAYER_FWID_TYPE_LEN) {
			tags = tcbinfo->type->data;
			ntags = nfwids;
		}

		for (uint_t i = 0; i < nfwids; i++) {
			DICE_FWID *fwid = sk_DICE_FWID_value(tcbinfo->fwids, i);
			int nid = OBJ_obj2nid(fwid->hashAlg);
			const char *alg = (nid != NID_undef) ?
			    OBJ_nid2sn(nid) : "unknown";

			printf("[%02u] ", entry++);

			if (is_alias && nfwids == 1) {
				printf("%36s", "Phase 1");
			} else if (i < nguids) {
				print_guid(guids + i * 16);
			} else if (i < ntags) {
				const uint8_t *t = tags + i * 4;
				if (memcmp(t, OS_ROT_TYPE_OXP2,
				    OS_ROT_TYPE_SIZE) == 0)
					printf("%36s", "Phase 2");
				else if (memcmp(t, OS_ROT_TYPE_OXRD,
				    OS_ROT_TYPE_SIZE) == 0)
					printf("%36s", "Ramdisk");
				else if (isprint_n(t, 4))
					printf("%-32c%.4s", ' ', t);
				else
					printf("%-28c%02x%02x%02x%02x",
					    ' ', t[0], t[1], t[2], t[3]);
			}

			printf("  %-6s  ", alg);
			print_hex(fwid->digest->data, fwid->digest->length);
			printf("  ");
			print_name(X509_get_subject_name(cert));
			printf("\n");
		}

		DICE_TCBINFO_free(tcbinfo);
	}

	sk_X509_pop_free(certs, X509_free);
	return (EXIT_SUCCESS);
}

static int
cmd_cert_chain(int fd, int argc, char **argv)
{
	const char *file_str = NULL;
	bool verbose = false;
	STACK_OF(X509) *certs = NULL;
	FILE *cert_fp = NULL;
	int c;
	int ret = EXIT_SUCCESS;

	optind = 0;
	while ((c = getopt(argc, argv, ":vw:")) != -1) {
		switch (c) {
		case 'v':
			verbose = true;
			break;
		case 'w':
			file_str = optarg;
			break;
		case ':':
			usage("cert-chain: option -%c requires an argument",
			    optopt);
		case '?':
		default:
			usage("cert-chain: unknown option -%c", optopt);
		}
	}
	if (argc - optind > 0)
		usage("cert-chain: unexpected arguments");

	if (verbose && file_str != NULL)
		usage("cert-chain: -v and -w are mutually exclusive");

	certs = get_cert_chain(fd);
	if (certs == NULL)
		return (EXIT_FAILURE);

	if (file_str != NULL) {
		cert_fp = fopen(file_str, "w");
		if (cert_fp == NULL) {
			warn("failed to open '%s'", file_str);
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	for (int i = 0; i < sk_X509_num(certs); i++) {
		X509 *cert = sk_X509_value(certs, i);

		if (cert_fp != NULL) {
			if (!PEM_write_X509(cert_fp, cert)) {
				warnx("failed to write cert to '%s'", file_str);
				ERR_print_errors_fp(stderr);
				ret = EXIT_FAILURE;
				break;
			}
			continue;
		}

		printf("[%d] ", i);

		if (verbose) {
			X509_print_fp(stdout, cert);
		} else {
			const ASN1_INTEGER *serial =
			    X509_get0_serialNumber(cert);

			printf("Certificate:\n  Serial: ");
			print_hex(ASN1_STRING_get0_data(serial),
			    ASN1_STRING_length(serial));
			printf("\n  Issuer: ");
			print_name(X509_get_issuer_name(cert));
			printf("\n  Subject: ");
			print_name(X509_get_subject_name(cert));
			printf("\n");
		}

		DICE_TCBINFO *tcbinfo = get_cert_tcbinfo(cert);
		if (tcbinfo != NULL) {
			print_cert_tcbinfo(tcbinfo, "  ", verbose);
			DICE_TCBINFO_free(tcbinfo);
		}
	}

out:
	if (cert_fp != NULL)
		(void) fclose(cert_fp);
	sk_X509_pop_free(certs, X509_free);
	return (ret);
}

static int
cmd_attest(int fd, int argc, char **argv)
{
	const char *file_str = NULL, *nonce_str = NULL;
	uint8_t nonce[OS_ROT_HASH_SIZE];
	uint8_t sig[OS_ROT_SIG_SIZE];
	int c;
	int ret = EXIT_SUCCESS;
	FILE *sig_fp = NULL;
	size_t der_len;
	uint8_t *der_buf;

	optind = 0;
	while ((c = getopt(argc, argv, ":n:w:")) != -1) {
		switch (c) {
		case 'n':
			nonce_str = optarg;
			break;
		case 'w':
			file_str = optarg;
			break;
		case ':':
			usage("attest: option -%c requires an argument",
			    optopt);
		case '?':
		default:
			usage("attest: unknown option -%c", optopt);
		}
	}
	if (argc - optind > 0)
		usage("attest: unexpected arguments");

	if (nonce_str == NULL)
		usage("attest: %u byte nonce (-n) required", OS_ROT_HASH_SIZE);

	if (file_str == NULL)
		usage("attest: -w argument required");

	if (!parse_hex(nonce_str, nonce, OS_ROT_HASH_SIZE)) {
		errx(EXIT_FAILURE, "attest: failed to parse nonce '%s'",
		    nonce_str);
	}

	do_attest(fd, nonce, sig);

	der_buf = ecdsa_sig_to_der(sig, OS_ROT_SIG_SIZE, &der_len);
	if (der_buf == NULL) {
		warnx("failed to encode signature as DER");
		ret = EXIT_FAILURE;
		goto out;
	}

	if ((sig_fp = fopen(file_str, "w")) == NULL) {
		warn("failed to open '%s'", file_str);
		OPENSSL_free(der_buf);
		ret = EXIT_FAILURE;
		goto out;
	}

	if (fwrite(der_buf, 1, der_len, sig_fp) != der_len) {
		warn("failed to write signature to '%s'", file_str);
		OPENSSL_free(der_buf);
		ret = EXIT_FAILURE;
		goto out;
	}

	OPENSSL_free(der_buf);

out:
	if (sig_fp != NULL)
		(void) fclose(sig_fp);
	return (ret);
}

int
main(int argc, char **argv)
{
	const char *subcmd;
	int fd;
	int c;
	int ret;

	while ((c = getopt(argc, argv, ":h")) != -1) {
		switch (c) {
		case 'h':
			usage(NULL);
		case '?':
		default:
			usage("unknown option -%c", optopt);
		}
	}

	if (argc - optind < 1)
		usage("subcommand required");

	subcmd = argv[optind];
	argc -= optind + 1;
	argv += optind + 1;
	optind = 0;

	if ((fd = open(OS_ROT_DEV, O_RDWR)) < 0)
		err(EXIT_FAILURE, "failed to open %s", OS_ROT_DEV);

	if (strcmp(subcmd, "measurement-set") == 0)
		ret = cmd_measurement_set(fd, argc, argv);
	else if (strcmp(subcmd, "cert-chain") == 0)
		ret = cmd_cert_chain(fd, argc, argv);
	else if (strcmp(subcmd, "attest") == 0)
		ret = cmd_attest(fd, argc, argv);
	else
		usage("unknown subcommand: %s", subcmd);

	(void) close(fd);
	return (ret);
}
