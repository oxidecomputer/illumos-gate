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
#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# This Makefile builds the UltraSPARC-T2 Performance Counter BackEnd (PCBE).
#

UTSBASE = ../..

#
#	Define module and object file sets.
#
MODULE		= pcbe.SUNW,UltraSPARC-T2
OBJECTS		= $(N2_PCBE_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE	= $(ROOT_PSM_PCBE_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4v/Makefile.sun4v

#
#	Define targets.
#
ALL_TARGET	= $(BINARY)
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

CFLAGS += -DNIAGARA2_IMPL

#
# niagara2-specific flags
#
CPPFLAGS += -DNIAGARA2_IMPL
AS_CPPFLAGS += -DNIAGARA2_IMPL

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSBASE)/sun4v/Makefile.targ
