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

PROG= t6init
OBJS=	t6init.o
MANIFEST= t6init.xml
SVCMETHOD= svc-t6init

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

PLATFORM = /usr/platform/oxide
ROOTPLATFORM = $(ROOT)$(PLATFORM)
ROOTUSRARCHBIN = $(ROOTPLATFORM)/bin
ROOTUSRARCHBINPROG = $(PROG:%=$(ROOTUSRARCHBIN)/%)
ROOTMANIFESTDIR = $(ROOTSVCSYSTEM)

CSTD= $(CSTD_GNU99)
CPPFLAGS += -I$(SRC)/uts/oxide -I$(ROOTPLATFORM)/include
CPPFLAGS += -I$(SRC)/uts/common/io/pciex/
LDFLAGS += -L$(ROOTPLATFORM)/lib/$(MACH64) -R$(PLATFORM)/lib/$(MACH64)
LDLIBS += -lt6mfg -lcfgadm -ldevinfo -lsocket -lumem -lipcc

.KEEP_STATE:

all: $(PROG)

install: $(ROOTMANIFEST) $(ROOTSVCMETHOD) $(ROOTUSRARCHBINPROG)

check:	$(CHKMANIFEST)

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
