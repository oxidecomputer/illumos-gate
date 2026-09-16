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
# Copyright 2026 Oxide Computer Company
#

include		$(SRC)/Makefile.master

MANSECT =	3hsmp

MANFILES =					\
		libhsmp_boost.3hsmp		\
		libhsmp_err.3hsmp		\
		libhsmp_init.3hsmp		\
		libhsmp_power.3hsmp		\
		libhsmp_telemetry.3hsmp		\
		libhsmp_version.3hsmp

.KEEP_STATE:

include		$(SRC)/man/Makefile.man

install:	$(ROOTMANFILES) $(MANLINK_SRCS)
	$(MANLINK_INSTALL)
