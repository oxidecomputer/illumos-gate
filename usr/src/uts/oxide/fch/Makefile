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
# Copyright 2022 Oxide Computer Co.
#

UTSBASE	= ../..

MODULE		= fch
OBJECTS		= $(FCH_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

include $(UTSBASE)/oxide/Makefile.oxide

ALL_TARGET	= $(BINARY)
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

LDFLAGS		+= -N misc/busra

.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

install:	$(INSTALL_DEPS)

include $(UTSBASE)/oxide/Makefile.targ
