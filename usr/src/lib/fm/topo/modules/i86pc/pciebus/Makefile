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

MODULE = pciebus
ARCH = i86pc
CLASS = arch
UTILDIR = ../../common/pciebus
MODULESRCS = \
	topo_pcie.c \
	topo_pcie_cfgspace.c \
	topo_pcie_prop.c \
	topo_pcie_util.c \
	bitext.c \
	topo_pcie_$(ARCH).c

include ../../Makefile.plugin

LDLIBS += -ldevinfo -lpcidb -lfmd_agent

CPPFLAGS += -I$(SRC)/uts/common -I$(SRC)/uts/intel -I$(UTILDIR)

%.o: $(SRC)/common/bitext/%.c
	$(COMPILE.c) $<
	$(CTFCONVERT_O)

