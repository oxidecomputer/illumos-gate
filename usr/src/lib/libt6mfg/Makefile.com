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
# Copyright 2020 Joyent, Inc.
#

LIBRARY =	libt6mfg.a
VERS =		.1
OBJECTS =	libt6mfg.o

include ../../Makefile.lib

SRCDIR =	../common
LIBS =		$(DYNLIB)
CSTD =		$(GNU_C99)
# XXX remove origin
LDLIBS +=	-lc -ldevinfo '-R$$ORIGIN' -lispi

.KEEP_STATE:

all: $(LIBS)

include ../../Makefile.targ
