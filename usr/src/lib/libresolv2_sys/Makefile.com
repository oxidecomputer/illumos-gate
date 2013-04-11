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
# Copyright 2019, Joyent, Inc.
# Copyright 2025 Oxide Computer Company

PARENT =	$(SRC)/lib/libresolv2

LIBRARY= libresolv_sys.a
VERS= .2

# include object definitions
include $(PARENT)/Makefile.obj

LOCFLAGS +=	-I$(PARENT)/include
CPPFLAGS +=	-DRESOLV_SYS -DRESOLV_HEADER="<resolv_sys.h>"

.KEEP_STATE:

all:	$(LIBS)

# include library targets
include ../../Makefile.targ

pics/%.o: $(PARENT)/common/bsd/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/dst/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/inet/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/irs/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/isc/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/nameser/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/resolv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(PARENT)/common/sunw/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)
