#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright (c) 2018, Joyent, Inc.
# Copyright 2023 Jason King

UTSBASE	=	../..

#
#	Define the module and object file sets.
#
MODULE		= tpm
OBJECTS		= $(TPM_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io/tpm

MAPFILE		= $(CONF_SRCDIR)/$(MODULE).mapfile

#
#	Include common rules.
#
# For now, TPM only delivers on x86/64 platforms, so only build
# with the Intel rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

# This is for everything except /usr/include/tss/
CPPFLAGS	+= -I$(ROOT)/usr/include

# This is for /usr/include/tss/, which is not built in the ON consolidation
#CPPFLAGS	+= -I$(ADJUNCT_PROTO)/usr/include

# The loader sources include most of the definitions we want, so we
# use those instead of duplicating them.
#CPPFLAGS	+= -I$(SRC)/boot/efi/include/

MAPFILES	+= ddi

LDFLAGS		+= -M $(MAPFILE)
LDFLAGS		+= -N misc/acpica -N misc/kcf

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

install:	$(INSTALL_DEPS)

include $(UTSBASE)/Makefile.mapfile
include $(UTSBASE)/intel/Makefile.targ

#$(OBJECTS): $(OBJS_DIR)
