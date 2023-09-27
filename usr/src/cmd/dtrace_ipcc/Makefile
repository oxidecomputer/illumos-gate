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

PROG= dtrace_ipcc
OBJS= dtrace_ipcc.o

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

PLATFORM = /usr/platform/oxide
ROOTPLATFORM = $(ROOT)$(PLATFORM)
ROOTUSRARCHBIN = $(ROOTPLATFORM)/bin
ROOTUSRARCHBINPROG = $(PROG:%=$(ROOTUSRARCHBIN)/%)

CSTD= $(CSTD_GNU99)
CPPFLAGS += -I$(SRC)/uts/oxide -I$(ROOTPLATFORM)/include
LDFLAGS += -L$(ROOTPLATFORM)/lib/$(MACH64) -R$(PLATFORM)/lib/$(MACH64)
LDLIBS += -lipcc -ldevinfo

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
