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
# Verify that a raw zvol without enough space to initialize can still be
# opened, that its initialization fails with ENOSPC, that the failure is
# final, and that the volume can be destroyed.
#
# STRATEGY:
# 1. Create a raw zvol under a filesystem. Raw zvol creation clears the
#    refreservation, so no space is reserved for it yet.
# 2. Set a quota on the parent filesystem that is smaller than the volume.
# 3. Verify that opening the volume succeeds, that its initialization
#    fails with ENOSPC, and that reads fail.
# 4. Remove the quota.  Verify that reopening the volume, also after its
#    minor is recreated, still reports ENOSPC.
# 5. Verify that the volume can be destroyed.
#

verify_runnable "global"

PARENT=$TESTPOOL/rawparent
RAWVOL=$PARENT/rawvol
RAWDEV=$ZVOL_RDEVDIR/$RAWVOL
typeset -i VOLSIZE=$((256 * 1024 * 1024))

function cleanup
{
	rawvol_release
	destroy_raw_zvol $RAWVOL
	destroy_raw_zvol $RAWVOL-renamed
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

log_assert "A raw zvol without enough space to initialize opens, reports" \
    "ENOSPC, and can be destroyed"
log_onexit cleanup

log_must zfs create $PARENT
log_must zfs create -V $VOLSIZE -o rawvol=on $RAWVOL
log_must zfs set quota=64m $PARENT

rawvol_hold $RAWDEV || log_fail "could not open $RAWDEV"
rawvol_wait_status $RAWDEV
[[ $RAWVOL_STATUS == ENOSPC ]] || \
    log_fail "expected ENOSPC, got $RAWVOL_STATUS"
log_mustnot dd if=$RAWDEV of=/dev/null bs=128k count=1
rawvol_release

# Giving up is final, even once there is space.
log_must zfs set quota=none $PARENT
rawvol_verify_failure_kept $RAWVOL ENOSPC

destroy_raw_zvol $RAWVOL
log_mustnot datasetexists $RAWVOL

log_pass "A raw zvol without enough space to initialize opens, reports" \
    "ENOSPC, and can be destroyed"
