#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2025 Oxide Computer Company
#

MODULE = apob.so
MDBTGT = raw

MODSRCS = apob.c
MODSRCS += genoa_impl.c milan_impl.c turin_impl.c

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/Makefile.cmd.64
include $(SRC)/cmd/mdb/intel/Makefile.amd64
include $(SRC)/cmd/mdb/oxide/Makefile.oxide
include $(SRC)/cmd/mdb/Makefile.module

MODSRCS_DIR = ../unix

CPPFLAGS += -DAPOB_RAW_DMOD
CPPFLAGS += -I$(SRC)/uts/oxide
CPPFLAGS += -I$(SRC)/uts/intel
