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

#
# DESCRIPTION:
# Verify that a raw zvol whose first open fails (because there is not
# enough space to preallocate it) can still be opened later and destroyed.
#
# STRATEGY:
# 1. Create a raw zvol under a filesystem. Raw zvol creation clears the
#    refreservation, so no space is reserved for it yet.
# 2. Set a quota on the parent filesystem that is smaller than the volume.
# 3. Verify that opening the volume fails, twice.
# 4. Verify that the volume can be destroyed.
# 5. Recreate the volume, fail its first open again, remove the quota,
#    and verify that the volume can now be opened and read once its
#    initialization completes. Then destroy it.
#

verify_runnable "global"

PARENT=$TESTPOOL/rawparent
RAWVOL=$PARENT/rawvol
RAWDEV=$ZVOL_RDEVDIR/$RAWVOL

function cleanup
{
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

function open_rawvol
{
	dd if=$RAWDEV of=/dev/null bs=128k count=1
}

log_assert "A raw zvol can be destroyed after its first open fails"
log_onexit cleanup

log_must zfs create $PARENT
log_must zfs create -V 256m -o rawvol=on $RAWVOL
log_must zfs set quota=64m $PARENT

log_mustnot open_rawvol
log_mustnot open_rawvol
log_must zfs destroy $RAWVOL
log_mustnot datasetexists $RAWVOL

log_must zfs set quota=none $PARENT
log_must zfs create -V 256m -o rawvol=on $RAWVOL
log_must zfs set quota=64m $PARENT
log_mustnot open_rawvol
log_must zfs set quota=none $PARENT

# Reads fail while the raw zvol is still being initialized.
typeset -i tries=0
until open_rawvol; do
	((tries++ < 60)) || log_fail "raw zvol could not be read after open"
	sleep 1
done

# The initialization thread holds the volume open until it exits.
tries=0
until zfs destroy $RAWVOL; do
	((tries++ < 60)) || log_fail "raw zvol could not be destroyed"
	sleep 1
done
log_mustnot datasetexists $RAWVOL

log_pass "A raw zvol can be destroyed after its first open fails"
