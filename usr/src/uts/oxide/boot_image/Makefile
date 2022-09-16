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
# Copyright 2022 Oxide Computer Company
#

#
# Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE =		../..

#
# Define the module and object file sets.
#
MODULE =		boot_image
OBJECTS =		$(BOOT_IMAGE_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE =		$(ROOT_PSM_MISC_DIR)/$(MODULE)

#
# Include common rules.
#
include $(UTSBASE)/oxide/Makefile.oxide

#
# Define targets
#
ALL_TARGET =		$(BINARY)
INSTALL_TARGET =	$(BINARY) $(ROOTMODULE)

#
# Overrides
#
INC_PATH +=		-I$(UTSBASE)/oxide/$(MODULE)

#
# Driver depends on mac
#
LDFLAGS +=		-N misc/mac

#
# Default build targets.
#
.KEEP_STATE:

def: $(DEF_DEPS)

all: $(ALL_DEPS)

clean: $(CLEAN_DEPS)

clobber: $(CLOBBER_DEPS)

install: $(INSTALL_DEPS)

#
# Include common targets.
#
include $(UTSBASE)/oxide/Makefile.targ
