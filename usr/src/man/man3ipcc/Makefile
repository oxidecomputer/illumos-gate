#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#

#
# Copyright 2024 Oxide Computer company
#

include		$(SRC)/Makefile.master

MANSECT =	3ipcc

MANFILES =					\
		libipcc_err.3ipcc		\
		libipcc_ident.3ipcc		\
		libipcc_imageblock.3ipcc	\
		libipcc_init.3ipcc		\
		libipcc_inv.3ipcc		\
		libipcc_keylookup.3ipcc		\
		libipcc_keyset.3ipcc		\
		libipcc_mac_all.3ipcc		\
		libipcc_status.3ipcc

MANLINKS =					\
		libipcc_errmsg.3ipcc		\
		libipcc_fini.3ipcc		\
		libipcc_ident_free.3ipcc	\
		libipcc_ident_model.3ipcc	\
		libipcc_ident_rev.3ipcc		\
		libipcc_ident_serial.3ipcc	\
		libipcc_inv_data.3ipcc		\
		libipcc_inv_free.3ipcc		\
		libipcc_inv_hdl_fini.3ipcc	\
		libipcc_inv_hdl_init.3ipcc	\
		libipcc_inv_name.3ipcc		\
		libipcc_inv_status_str.3ipcc	\
		libipcc_inv_type.3ipcc		\
		libipcc_keylookup_free.3ipcc	\
		libipcc_mac_addr.3ipcc		\
		libipcc_mac_bootstrap.3ipcc	\
		libipcc_mac_count.3ipcc		\
		libipcc_mac_free.3ipcc		\
		libipcc_mac_nic.3ipcc		\
		libipcc_mac_stride.3ipcc	\
		libipcc_startup_options.3ipcc	\
		libipcc_strerror.3ipcc		\
		libipcc_syserr.3ipcc

libipcc_errmsg.3ipcc		:= LINKSRC = libipcc_err.3ipcc
libipcc_fini.3ipcc		:= LINKSRC = libipcc_init.3ipcc
libipcc_ident_free.3ipcc	:= LINKSRC = libipcc_ident.3ipcc
libipcc_ident_model.3ipcc	:= LINKSRC = libipcc_ident.3ipcc
libipcc_ident_rev.3ipcc		:= LINKSRC = libipcc_ident.3ipcc
libipcc_ident_serial.3ipcc	:= LINKSRC = libipcc_ident.3ipcc
libipcc_inv_data.3ipcc		:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_free.3ipcc		:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_hdl_fini.3ipcc	:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_hdl_init.3ipcc	:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_name.3ipcc		:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_status_str.3ipcc	:= LINKSRC = libipcc_inv.3ipcc
libipcc_inv_type.3ipcc		:= LINKSRC = libipcc_inv.3ipcc
libipcc_keylookup_free.3ipcc	:= LINKSRC = libipcc_keylookup.3ipcc
libipcc_mac_addr.3ipcc		:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_mac_bootstrap.3ipcc	:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_mac_count.3ipcc		:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_mac_free.3ipcc		:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_mac_nic.3ipcc		:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_mac_stride.3ipcc	:= LINKSRC = libipcc_mac_all.3ipcc
libipcc_startup_options.3ipcc	:= LINKSRC = libipcc_status.3ipcc
libipcc_strerror.3ipcc		:= LINKSRC = libipcc_err.3ipcc
libipcc_syserr.3ipcc		:= LINKSRC = libipcc_err.3ipcc

.KEEP_STATE:

include		$(SRC)/man/Makefile.man

install:	$(ROOTMANFILES) $(ROOTMANLINKS)
