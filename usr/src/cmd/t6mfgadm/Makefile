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
# Copyright 2022 Oxide Computer, Inc.
#

PROG = t6mfgadm

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

CTF_MODE = link
CSTD = $(CSTD_GNU99)
# XXX remove $ORIGIN
LDLIBS += '-R$$ORIGIN' -lt6mfg -lofmt -lsocket
OBJS = t6mfgadm.o t6mfgadm_srom.o t6mfgadm_flash.o

.KEEP_STATE:

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o: %.c
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

all: $(PROG)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

include ../Makefile.targ
