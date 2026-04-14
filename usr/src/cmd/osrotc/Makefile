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

UTSBASE =	$(SRC)/uts

PROG =		osrotc

include ../Makefile.cmd
include ../Makefile.cmd.64
include ../Makefile.ctf

CSTD =		$(CSTD_GNU17)
CTF_MODE =	link

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(UTSBASE)/oxide/io

LDLIBS +=	-lcrypto
NATIVE_LIBS +=	libcrypto.so

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJS)

include ../Makefile.targ
