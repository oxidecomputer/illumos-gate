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
# Copyright 2024 Oxide Computer Company
#

PROG= usmn udf apobdump

include ../Makefile.cmd

ROOTCMDDIR = $(ROOTLIB)
CPPFLAGS += -I$(SRC)/uts/intel/io/amdzen
CFLAGS += $(CCVERBOSE)
CSTD = $(CSTD_GNU99)
COMMON_OBJS = apob_common.o

apobdump := EXTRA_OBJS += apob_common.o

.KEEP_STATE:

all: $(PROG)

$(PROG): $(COMMON_OBJS)

%: %.o
	$(LINK.c) -o $@ $< $(EXTRA_OBJS) $(LDLIBS)
	$(POST_PROCESS)

%.o: $(SRC)/common/apob/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

install: all $(ROOTCMD)

clean:
	$(RM) $(COMMON_OBJS) $(PROG:%=%.o)

include ../Makefile.targ
