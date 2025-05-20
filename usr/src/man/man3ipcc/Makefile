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
		libipcc_rot_send.3ipcc		\
		libipcc_status.3ipcc

.KEEP_STATE:

include		$(SRC)/man/Makefile.man

install:	$(ROOTMANFILES) $(MANLINK_SRCS)
	$(MANLINK_INSTALL)
