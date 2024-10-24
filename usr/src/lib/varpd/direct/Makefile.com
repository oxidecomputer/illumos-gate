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
# Copyright 2015 Joyent, Inc.
#

LIBRARY =	libvarpd_direct.a
VERS =		.1
OBJECTS =	libvarpd_direct.o

include ../../../Makefile.lib
include ../../Makefile.plugin

LIBS =		$(DYNLIB)
LDLIBS +=	-lc -lumem -lnvpair
CPPFLAGS +=	-I../common

CSTD=		$(CSTD_GNU99)

SRCDIR =	../common

.KEEP_STATE:

all:	$(LIBS)

include ../../../Makefile.targ
