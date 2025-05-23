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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2019 Joyent, Inc.
#

CRTI = crti.o
CRTN = crtn.o
CRT1 = crt1.o
VALUES = values-Xa.o values-Xc.o values-Xs.o values-Xt.o \
		values-xpg4.o values-xpg6.o
COMMON_CRT = common-crt.o
MACH_CRT = mach-crt1.o

# include library definitions
include ../../Makefile.lib

SRCS = $(VALUES:%.o=../common/%.c) $(COMMON_CRT:%.o=../common/%.c)

POST_PROCESS_O = $(PROCESS_COMMENT) $@ ; $(STRIP) -x $@

OBJECTS = $(VALUES) $(CRT1) $(CRTI) $(CRTN)

ROOTLIB=	$(ROOT)/usr/lib
ROOTLIB64=	$(ROOTLIB)/$(MACH64)
ROOTOBJECTS=	$(OBJECTS:%=$(ROOTLIB)/%)
ROOTOBJECTS64=	$(OBJECTS:%=$(ROOTLIB64)/%)

$(INTEL_BLD)ROOTOBJECTS += $(ROOTLIB)/gcrt1.o
$(INTEL_BLD)ROOTOBJECTS64 += $(ROOTLIB64)/gcrt1.o

ASFLAGS +=	-D_ASM -DPIC $(AS_PICFLAGS)
ASFLAGS64 +=	-D_ASM -DPIC $(AS_PICFLAGS)

values-xpg6.o := CPPFLAGS += -I$(SRC)/lib/libc/inc
$(COMMON_CRT) $(VALUES) := CFLAGS += $(C_PICFLAGS)
$(COMMON_CRT) $(VALUES) := CFLAGS64 += $(C_PICFLAGS64)

# intentional double exit
SMOFF += unreachable

COMPATLINKS=	usr/ccs/lib/values-Xa.o \
		usr/ccs/lib/values-Xc.o \
		usr/ccs/lib/values-Xs.o \
		usr/ccs/lib/values-Xt.o \
		usr/ccs/lib/values-xpg4.o \
		usr/ccs/lib/values-xpg6.o

COMPATLINKS64=	usr/ccs/lib/$(MACH64)/values-Xa.o \
		usr/ccs/lib/$(MACH64)/values-Xc.o \
		usr/ccs/lib/$(MACH64)/values-Xs.o \
		usr/ccs/lib/$(MACH64)/values-Xt.o \
		usr/ccs/lib/$(MACH64)/values-xpg4.o \
		usr/ccs/lib/$(MACH64)/values-xpg6.o

$(ROOT)/usr/ccs/lib/values-Xa.o:=	COMPATLINKTARGET=../../lib/values-Xa.o
$(ROOT)/usr/ccs/lib/values-Xc.o:=	COMPATLINKTARGET=../../lib/values-Xc.o
$(ROOT)/usr/ccs/lib/values-Xs.o:=	COMPATLINKTARGET=../../lib/values-Xs.o
$(ROOT)/usr/ccs/lib/values-Xt.o:=	COMPATLINKTARGET=../../lib/values-Xt.o
$(ROOT)/usr/ccs/lib/values-xpg4.o:=	COMPATLINKTARGET=../../lib/values-xpg4.o
$(ROOT)/usr/ccs/lib/values-xpg6.o:=	COMPATLINKTARGET=../../lib/values-xpg6.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-Xa.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-Xa.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-Xc.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-Xc.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-Xs.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-Xs.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-Xt.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-Xt.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-xpg4.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-xpg4.o
$(ROOT)/usr/ccs/lib/$(MACH64)/values-xpg6.o:= \
	COMPATLINKTARGET=../../../lib/$(MACH64)/values-xpg6.o

.KEEP_STATE:

all:	$(OBJECTS)

clean clobber:
	$(RM) $(OBJECTS) $(MACH_CRT) $(COMMON_CRT)


$(CRT1): $(COMMON_CRT) $(MACH_CRT)
	$(LD) -r $(MACH_CRT) $(COMMON_CRT) -o $(CRT1)

%.o:	../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

%.o:	%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)

%.o:	../$(MACH)/%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)

# install rule for ROOTOBJECTS and ROOTOBJECTS64
$(ROOTLIB)/%.o: %.o
	$(INS.file)

$(ROOTLIB)/gcrt1.o:
	$(RM) $(ROOTLIB)/gcrt1.o; $(SYMLINK) crt1.o $(ROOTLIB)/gcrt1.o

$(ROOTLIB64)/%.o: %.o
	$(INS.file)

$(ROOTLIB64)/gcrt1.o:
	$(RM) $(ROOTLIB64)/gcrt1.o; $(SYMLINK) crt1.o $(ROOTLIB64)/gcrt1.o

$(ROOTCOMPATLINKS) $(ROOTCOMPATLINKS64):
	$(RM) $@; $(SYMLINK) $(COMPATLINKTARGET) $@
