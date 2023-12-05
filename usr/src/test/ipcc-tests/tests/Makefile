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

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/Makefile.ctf
CTF_MODE= link

PROGS = \
	ident \
	interface_version \
	macs \
	rot \
	status

PROGS32 = $(PROGS:%=%.32)
PROGS64 = $(PROGS:%=%.64)

CLEAN_OBJS =

ROOTOPTPKG = $(ROOT)/opt/ipcc-tests
TESTDIR = $(ROOTOPTPKG)/tests

CMDS = $(PROGS32:%=$(TESTDIR)/%) $(PROGS64:%=$(TESTDIR)/%)
$(CMDS) := FILEMODE = 0555

CSTD=		$(CSTD_GNU99)
CPPFLAGS +=	-I$(SRC)/uts/oxide

interface_version.32 := CPPFLAGS += -I$(SRC)/uts/oxide/io/ipcc
interface_version.64 := CPPFLAGS += -I$(SRC)/uts/oxide/io/ipcc

.KEEP_STATE:

install: all $(CMDS)

all: $(PROGS32) $(PROGS64)

clean:

clobber: clean
	-$(RM) $(PROGS32) $(PROGS64)

$(CMDS): $(TESTDIR) $(PROGS32) $(PROGS64)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

%.64: %.c
	$(LINK64.c) -o $@ $< $(LDLIBS64)
	$(POST_PROCESS)

%.32: %.c
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)
