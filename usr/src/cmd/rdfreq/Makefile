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
# Copyright 2024 Oxide Computer Company
#

PROG = rdfreq

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

.KEEP_STATE:

CSTD = $(CSTD_GNU99)
CTF_MODE = link

CPPFLAGS += -I../../uts/common

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

check:	$(PROG).c
	$(CSTYLE) -pP $(PROG).c

clean:
	$(RM) $(OBJS)

include ../Makefile.targ
