#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#

#
# Copyright 2011, Richard Lowe
# Copyright (c) 2012, Igor Kozhukhov <ikozhukhov@gmail.com>
# Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
# Copyright 2024 Oxide Computer Company
#

SUBDIRS=	man1		\
		man1b		\
		man1c		\
		man1has		\
		man1s		\
		man2		\
		man3		\
		man3avl		\
		man3bsm		\
		man3c		\
		man3c_db	\
		man3cfgadm	\
		man3commputil	\
		man3contract	\
		man3cpc		\
		man3curses	\
		man3dat		\
		man3devid	\
		man3devinfo	\
		man3dlpi	\
		man3dns_sd	\
		man3elf		\
		man3exacct	\
		man3ext		\
		man3fcoe	\
		man3fstyp	\
		man3gen		\
		man3gss		\
		man3head	\
		man3ipcc	\
		man3iscsit	\
		man3kstat	\
		man3kvm		\
		man3jedec	\
		man3ldap	\
		man3lgrp	\
		man3lib		\
		man3m		\
		man3mail	\
		man3malloc	\
		man3mp		\
		man3mpapi	\
		man3mvec	\
		man3nsl		\
		man3nvpair	\
		man3pam		\
		man3ofmt	\
		man3papi	\
		man3perl	\
		man3picl	\
		man3picltree	\
		man3pool	\
		man3proc	\
		man3project	\
		man3resolv	\
		man3rpc		\
		man3rsm		\
		man3sasl	\
		man3scf		\
		man3sec		\
		man3secdb	\
		man3sip		\
		man3slp		\
		man3socket	\
		man3stmf	\
		man3sysevent	\
		man3tecla	\
		man3tsol	\
		man3utempter	\
		man3uuid	\
		man3volmgt	\
		man3xcurses	\
		man3xnet	\
		man4		\
		man4d		\
		man4fs		\
		man4i		\
		man4ipp		\
		man4m		\
		man4p		\
		man5		\
		man7		\
		man8		\
		man9		\
		man9e		\
		man9f		\
		man9p		\
		man9s

.PARALLEL: $(SUBDIRS)

all		:= TARGET = all
clean		:= TARGET = clean
clobber		:= TARGET = clobber
install		:= TARGET = install
check		:= TARGET = check

all check clean clobber install: $(SUBDIRS)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
