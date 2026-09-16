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
# Copyright 2026 Oxide Computer Company
#

PROG= hsmp
OBJS= hsmp.o

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

CSTD= $(CSTD_GNU17)
LDLIBS += -lhsmp -lkstat -lm -lofmt

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:
	-$(RM) $(OBJS)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

include ../Makefile.targ
