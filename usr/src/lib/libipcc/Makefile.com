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

LIBRARY =	libipcc.a
VERS =		.1
OBJECTS =	libipcc.o
DTRACE_OBJS =	libipcc_dt.o

include ../../Makefile.lib

ROOTLIBDIR =	$(ROOT)/usr/platform/$(ARCH)/lib
ROOTLIBDIR64 =	$(ROOTLIBDIR)/$(MACH64)

CPPFLAGS +=	-I../common -I$(SRC)/uts/oxide
CSTD = $(CSTD_GNU99)
LIBS =		$(DYNLIB)
LDLIBS +=	-lc -lz -lnvpair -lrename
NATIVE_LIBS +=	libz.so

SRCDIR =	../common

.KEEP_STATE:

all:	$(LIBS)

$(ROOTLIBDIR) $(ROOTLIBDIR64):
	$(INS.dir) $@

EXTPICS = $(DTRACE_OBJS:%=pics/%)

pics/%.o: ../common/%.d $(PICS)
	$(COMPILE.d) -C -s $< -o $@ $(PICS)
	$(POST_PROCESS_O)

include ../../Makefile.targ
