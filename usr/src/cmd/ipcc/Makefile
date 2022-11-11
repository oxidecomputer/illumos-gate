#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2022 Oxide Computer Company
#

# XXX - this is a temporary service to set up the DPIO that IPCC uses to
#       receive SP interrupts.

PROG=
MANIFEST= ipcc.xml
SVCMETHOD= svc-ipcc

include $(SRC)/cmd/Makefile.cmd

ROOTMANIFESTDIR = $(ROOTSVCSYSTEM)

.KEEP_STATE:

all:

install: $(DIRS) $(ROOTMANIFEST) $(ROOTSVCMETHOD)

check:	$(CHKMANIFEST)

clean:

include $(SRC)/cmd/Makefile.targ
