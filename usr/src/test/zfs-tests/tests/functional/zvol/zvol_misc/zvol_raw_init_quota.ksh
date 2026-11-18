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
# A raw zvol whose initialization runs out of space because of gang blocks
# must retry at a smaller block size.  If it still fails at its minimum block
# size, the failure must be final.
#
# Gang blocks use more space than the data they hold.  The parent filesystem
# has a quota (and reservation) of the volume size plus 4 MiB per GiB, which
# is how Oxide's sled-agent sets up a local storage disk.  That is enough for
# the volume's indirect blocks, but not for many gang blocks with 4K sectors
# (each costs two copies of a 4K gang header, plus rounding of its children),
# so the test needs a pool with an ashift of at least 12.
# metaslab_force_ganging makes about one in four large allocations gang (it
# depends on the clock tick), so passes with blocks larger than 64K almost
# always find gang blocks or run out of space.
#
# STRATEGY:
# 1. Force gang blocks for some allocations larger than 64K.
# 2. Create a raw zvol in a filesystem with the quota above, and open it.
#    Verify that its initialization succeeds, and that it can be read.
# 3. Create a raw zvol with a minimum block size of 128K in the same way.
#    Verify that its initialization runs out of space (retrying a few times
#    if no allocation happened to gang), that opening it again reports the
#    failure, even without the quota and after its minor is recreated, and
#    that it can be destroyed.
#

verify_runnable "global"

typeset PARENT=$TESTPOOL/rawquota
typeset RAWVOL=$PARENT/rawvol
typeset RAWDEV=$ZVOL_RDEVDIR/$RAWVOL
typeset -i VOLSIZE=$((256 * 1024 * 1024))
typeset -i QUOTA=$((VOLSIZE + VOLSIZE / 1024 * 4))
typeset orig_force_ganging=$(get_tunable metaslab_force_ganging)

function cleanup
{
	log_must set_tunable64 metaslab_force_ganging $orig_force_ganging
	rawvol_release
	datasetexists $PARENT && \
	    log_must zfs set quota=none reservation=none $PARENT
	destroy_raw_zvol $RAWVOL
	destroy_raw_zvol $RAWVOL-renamed
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

function read_rawvol
{
	dd if=$RAWDEV of=/dev/null bs=128k count=1 >/dev/null 2>&1
}

#
# Create the raw zvol with minimum block size $1, start its initialization,
# and wait for it to finish, setting RAWVOL_STATUS.
#
# The first open starts the initialization.  The volume is left open (call
# rawvol_release), so that the status checks aren't first opens.
#
function init_rawvol # <volblocksize>
{
	log_must zfs create -V $VOLSIZE -o rawvol=on -o volblocksize=$1 \
	    $RAWVOL
	log_must zfs set quota=$QUOTA reservation=$QUOTA $PARENT

	rawvol_hold $RAWDEV || log_fail "could not open $RAWDEV"
	rawvol_wait_status $RAWDEV
	log_raw_zvol_dbgmsg
}

log_assert "Raw zvol initialization that runs out of space retries at a" \
    "smaller block size, and a failure at its minimum block size is final"
log_onexit cleanup

typeset -i ashift
ashift=$(zdb -C $TESTPOOL | awk '$1 == "ashift:" { print $2; exit }')
((ashift >= 12)) || log_unsupported "needs ashift >= 12 (pool has $ashift)"

log_must zfs create $PARENT
log_must set_tunable64 metaslab_force_ganging $((64 * 1024 + 1))

init_rawvol 8k
[[ $RAWVOL_STATUS == 0 ]] || log_fail "initialization failed: $RAWVOL_STATUS"
log_must read_rawvol
rawvol_release
destroy_raw_zvol $RAWVOL

# Every block size it can try is large enough to be forced to gang.
for ((i = 0; i < 5; i++)); do
	init_rawvol 128k
	rawvol_release
	[[ $RAWVOL_STATUS != 0 ]] && break
	log_note "no allocation was forced to gang; trying again"
	destroy_raw_zvol $RAWVOL
done
# If few allocations gang, the pass fits in the quota and finds the gang
# blocks at the end: EFRAGS, which Python calls EBADR.
case $RAWVOL_STATUS in
EDQUOT|ENOSPC|EBADR)	;;
*)	log_fail "initialization with 128K blocks: expected EDQUOT, ENOSPC" \
	    "or EFRAGS, got $RAWVOL_STATUS" ;;
esac

# Opening the volume again reports the failure instead of starting over.
log_must set_tunable64 metaslab_force_ganging $orig_force_ganging
typeset failed=$RAWVOL_STATUS
RAWVOL_STATUS=$(rawvol_ioctl status $RAWDEV) || log_fail "could not reopen"
[[ $RAWVOL_STATUS == $failed ]] || \
    log_fail "expected $failed after reopening, got $RAWVOL_STATUS"

# Giving up is final, even once there is space.
log_must zfs set quota=none reservation=none $PARENT
rawvol_verify_failure_kept $RAWVOL $failed
destroy_raw_zvol $RAWVOL
log_mustnot datasetexists $RAWVOL

log_pass "Raw zvol initialization that runs out of space retries at a" \
    "smaller block size, and a failure at its minimum block size is final"
