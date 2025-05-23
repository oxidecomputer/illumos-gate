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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2019 Joyent, Inc.
# Copyright 2025 Oxide Computer Company
#
#	This makefile drives the production of the ip driver
#	kernel module.
#
#	intel implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= ip
OBJECTS		= $(IP_OBJS:%=$(OBJS_DIR)/%)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
ROOTLINK	= $(ROOT_STRMOD_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/inet/ip

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOTLINK) $(ROOT_CONFFILE)

CFLAGS		+= $(CINLINEFLAGS)

CERRWARN	+= -_gcc=-Wno-parentheses
CERRWARN	+= -_gcc=-Wno-unused-label
CERRWARN	+= -_gcc=-Wno-unused-function
CERRWARN	+= -_gcc=-Wno-unused-variable
CERRWARN	+= -_gcc=-Wno-switch
CERRWARN	+= $(CNOWARN_UNINIT)
CERRWARN	+= -_gcc=-Wno-type-limits

# false positives
SMOFF += index_overflow

# need work still
$(OBJS_DIR)/igmp.o := SMOFF += shift_to_zero
$(OBJS_DIR)/tnet.o := SMOFF += shift_to_zero
SMOFF += signed,all_func_returns
SMOFF += signed_integer_overflow_check

#
# To get the BPF header files included by ipnet.h
#
INC_PATH	+= -I$(UTSBASE)/common/io/bpf

#
# Depends on md5 and swrand (for SCTP). SCTP needs to depend on
# swrand as it needs random numbers early on during boot before
# kCF subsystem can load swrand.
#
LDFLAGS		+= -Nmisc/md5 -Ncrypto/swrand -Nmisc/hook -Nmisc/neti

#
# Depends on the congestion control framework for TCP connections.
# We make several different algorithms available by default.
#
LDFLAGS		+= -N misc/cc -N cc/cc_sunreno -N cc/cc_newreno -N cc/cc_cubic

#
# Depends on utility methods in mac_provider for manipulating mblk/dblk
# state and header length info for drivers.
#
LDFLAGS		+= -N misc/mac

#
# For now, disable these warnings; maintainers should endeavor
# to investigate and remove these for maximum coverage.
# Please do not carry these forward to new Makefiles.
#

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS) $(SISCHECK_DEPS)

clean:		$(CLEAN_DEPS) $(SISCLEAN_DEPS)

# Need to clobber all build types due to ipctf.a
clobber:	$(CLOBBER_DEPS) $(SISCLEAN_DEPS) \
		clobber.obj64 clobber.debug64


install:	$(INSTALL_DEPS) $(SISCHECK_DEPS)

$(ROOTLINK):	$(ROOT_STRMOD_DIR) $(ROOTMODULE)
	-$(RM) $@; ln $(ROOTMODULE) $@

#
#	Include common targets.
#
include $(UTSBASE)/intel/Makefile.targ

#
# The ip CTF data is merged into the genunix module because these types are
# complex and heavily shared.  The genunix build will execute one of the
# rules below to create an archive, ipctf.a, containing the ip objects.  The
# real ip will be uniquified against genunix later in the build, and will
# emerge containing very few types.
#
$(OBJS_DIR)/ipctf.a: $(OBJECTS)
	-$(RM) $@
	$(AR) -r $@ $(OBJECTS)

$(OBJECTS): $(OBJS_DIR)

CLOBBERFILES += $(OBJS_DIR)/ipctf.a

ipctf.obj64: FRC
	@BUILD_TYPE=OBJ64 VERSION='$(VERSION)' $(MAKE) obj64/ipctf.a

ipctf.debug64: FRC
	@BUILD_TYPE=DBG64 VERSION='$(VERSION)' $(MAKE) debug64/ipctf.a
