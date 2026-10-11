#!/usr/bin/ksh -p
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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# Raw zvols cannot be encrypted. Creating one with encryption on, or under
# an encrypted parent, must fail.
#
# STRATEGY:
# 1. Create an encrypted filesystem.
# 2. Verify that creating a raw zvol under it fails.
# 3. Verify that creating a raw zvol with encryption=on fails.
# 4. Verify that a raw zvol with encryption=off under the encrypted
#    filesystem can be created.
#

verify_runnable "global"

typeset PASSPHRASE="password"
typeset CRYPT=$TESTPOOL/crypt
typeset VOLSIZE_RAW=64m

function cleanup
{
	datasetexists $CRYPT && destroy_dataset $CRYPT -r
}

log_assert "Raw zvols cannot be encrypted"
log_onexit cleanup

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $CRYPT"

# Inherited encryption
log_mustnot zfs create -V $VOLSIZE_RAW -o rawvol=on $CRYPT/vol
log_mustnot datasetexists $CRYPT/vol

# Explicit encryption, inheriting the parent's key
log_mustnot zfs create -V $VOLSIZE_RAW -o rawvol=on -o encryption=on \
	$CRYPT/vol
log_mustnot datasetexists $CRYPT/vol

# Explicit encryption, new encryption root
log_mustnot eval "echo $PASSPHRASE | zfs create -V $VOLSIZE_RAW" \
	"-o rawvol=on -o encryption=on -o keyformat=passphrase" \
	"$TESTPOOL/vol"
log_mustnot datasetexists $TESTPOOL/vol

# An unencrypted raw zvol under an encrypted parent is fine
log_must zfs create -V $VOLSIZE_RAW -o rawvol=on -o encryption=off \
	$CRYPT/plainvol
log_must destroy_dataset $CRYPT/plainvol

log_pass "Raw zvols cannot be encrypted"
