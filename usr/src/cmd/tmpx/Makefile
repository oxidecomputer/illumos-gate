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
# Copyright 2023 Oxide Computer Company
#

PROG= tmpx
OBJS= tmpx.o

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

ROOTUSRARCHBIN = $(ROOT)/usr/platform/oxide/bin
ROOTUSRARCHBINPROG = $(PROG:%=$(ROOTUSRARCHBIN)/%)

CSTD= $(CSTD_GNU99)
LDLIBS += -lkstat -lzonecfg

.KEEP_STATE:

all: $(PROG)

install: $(ROOTUSRARCHBINPROG)

clean:
	-$(RM) $(OBJS)

$(ROOTUSRARCHBIN):
	$(INS.dir)

$(ROOTUSRARCHBIN)/%: $(ROOTUSRARCHBIN) %
	$(INS.file)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

include ../Makefile.targ
