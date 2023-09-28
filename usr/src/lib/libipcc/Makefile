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

include		../Makefile.lib

HDRS =		libipcc.h
DHDRS =		libipcc_dt.h
HDRDIR =	common
SUBDIRS = $(MACH)
$(BUILD64)SUBDIRS += $(MACH64)

ARCH=		oxide

ROOTHDRDIR =	$(ROOT)/usr/platform/$(ARCH)/include

all :=		TARGET = all
clean :=	TARGET = clean
clobber :=	TARGET = clobber
install :=	TARGET = install

CLEANFILES += $(DHDRS:%=$(HDRDIR)/%)

.KEEP_STATE:

all clean clobber: $(SUBDIRS)

install:	$(SUBDIRS) install_h

install_h:	$(ROOTHDRS)

check:		$(CHECKHDRS)

$(SUBDIRS): $(DHDRS:%=$(HDRDIR)/%)
	@cd $@; pwd; $(MAKE) $(TARGET) ARCH=$(ARCH)

$(HDRDIR)/%.h: $(HDRDIR)/%.d
	$(DTRACE) -xnolibs -h -s $< -o $@

FRC:

include ../Makefile.targ
