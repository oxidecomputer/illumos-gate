#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2023 Oxide Computer Company
#

MODULE = oxhc
ARCH = oxide
CLASS = arch

MODULESRCS = oxhc.c oxhc_ipcc.c oxhc_ic.c bitext.o

include ../../Makefile.plugin

CPPFLAGS += -I$(SRC)/uts/oxide -I../../common/zen
LDLIBS += -ldevinfo -lrename -lnvpair

%.o: $(SRC)/common/bitext/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)
