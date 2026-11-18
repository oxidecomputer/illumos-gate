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
# A raw zvol whose initialization was stopped partway, and which no longer
# has enough space for the rest of the volume, must still open.  If what was
# written includes gang blocks, initialization must start over at a smaller
# block size; otherwise it must fail with ENOSPC.
# Once initialization has failed, reopening the volume, even after its minor
# is recreated, must report the failure instead of starting over.
#
# Gang blocks use more space than the data they hold, so they can use up the
# space meant for the rest of the volume before initialization finds them.
# Instead of relying on that, the test stops initialization partway and then
# sets the parent's quota to a little less than the volume uses plus what
# the rest of it needs.  The volume is sparse, so only the quota limits it.
#
# STRATEGY:
# 1. Create a sparse raw zvol in a filesystem without a quota.  Limit dirty
#    data so that initialization takes many txgs, open the volume, and stop
#    initialization once a quarter of the volume has been written.
# 2. Set the quota above and open the volume again.  Verify that the open
#    succeeds, that initialization fails with ENOSPC, and that reopening
#    it, also after recreating its minor, reports ENOSPC without starting
#    over.
# 3. Repeat with gang blocks forced for some allocations larger than 64K.
#    Verify that the open succeeds and that initialization starts over at a
#    smaller block size (retrying up to 20 times if no allocation happened
#    to gang).  Verify that the pass that started over succeeds or, if it
#    runs out of space, that reopening the volume reports that without
#    starting over.
#

verify_runnable "global"

typeset PARENT=$TESTPOOL/rawresume
typeset RAWVOL=$PARENT/rawvol
typeset RAWDEV=$ZVOL_RDEVDIR/$RAWVOL
typeset -i VOLSIZE=$((512 * 1024 * 1024))
typeset -i SHORT=$((1024 * 1024))
typeset orig_force_ganging=$(get_tunable metaslab_force_ganging)
typeset orig_dirty_max=$(get_tunable zfs_dirty_data_max)

function cleanup
{
	log_must set_tunable64 metaslab_force_ganging $orig_force_ganging
	log_must set_tunable64 zfs_dirty_data_max $orig_dirty_max
	rawvol_release
	datasetexists $PARENT && log_must zfs set quota=none $PARENT
	destroy_raw_zvol $RAWVOL
	destroy_raw_zvol $RAWVOL-renamed
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

#
# Print the zero thread's first message about running out of space that was
# logged after mark $1, or nothing.  Only the first new message tells whether
# the pass found gang blocks: a pass that starts over can run out of space at
# once and log another.
#
function next_backoff # <mark>
{
	dbgmsg_since $1 | grep "out of space, blocksize" | head -1
}

#
# Create the raw zvol, start its initialization, and stop it once a quarter
# of the volume has been written, leaving the volume closed.  Then set the
# parent's quota to $SHORT less than the volume needs to resume.  Returns 1
# if initialization got too far before it stopped.
#
function stop_partway
{
	typeset -i zoff=0 tries=0
	typeset used

	log_must zfs create -s -V $VOLSIZE -o rawvol=on $RAWVOL
	rawvol_hold $RAWDEV || log_fail "could not open $RAWDEV"
	while ((zoff < VOLSIZE / 4)); do
		if ((tries++ >= 600)); then
			log_raw_zvol_dbgmsg
			log_fail "initialization of $RAWVOL made no progress"
		fi
		sleep 0.1
		zoff=$(rawvol_ioctl zoff $RAWDEV)
	done
	rawvol_stop_init $RAWDEV
	zoff=$(rawvol_ioctl zoff $RAWDEV)
	RAWVOL_STATUS=$(rawvol_ioctl status $RAWDEV)
	rawvol_release
	log_note "initialization stopped at $zoff: $RAWVOL_STATUS"
	# A pass that wrote the whole volume can also have started over.
	[[ $RAWVOL_STATUS == EINTR ]] && ((zoff >= VOLSIZE / 8)) && \
	    ((zoff <= VOLSIZE - 2 * SHORT)) || return 1

	used=$(get_prop used $PARENT) || \
	    log_fail "could not get used of $PARENT"
	log_must zfs set quota=$((used + VOLSIZE - zoff - SHORT)) $PARENT
	return 0
}

#
# Wait for initialization to finish, and set RAWVOL_STATUS.
#
function wait_init
{
	rawvol_wait_status $RAWDEV
	log_raw_zvol_dbgmsg
}

#
# Close and reopen the volume, whose initialization failed with status $1,
# and verify that the open reports that status instead of starting over,
# also after the volume's minor is recreated.  The zero thread keeps the
# volume open briefly after its status is final, and only a first open would
# start over, so reopen until a first open logs that it isn't restarting.
# Leaves the volume closed.
#
function verify_no_restart # <status>
{
	typeset since
	typeset -i tries=0

	since=$(dbgmsg_mark)
	while true; do
		rawvol_release
		rawvol_hold $RAWDEV || log_fail "could not reopen $RAWDEV"
		dbgmsg_since $since | grep -q "not restarting" && break
		if ((tries++ >= 10)); then
			log_raw_zvol_dbgmsg
			log_fail "reopening $RAWVOL did not report its failure"
		fi
		sleep 1
	done
	dbgmsg_since $since | grep -q "initializing from offset" && \
	    log_fail "reopening $RAWVOL started initialization over"
	RAWVOL_STATUS=$(rawvol_ioctl status $RAWDEV)
	[[ $RAWVOL_STATUS == $1 ]] || \
	    log_fail "expected $1 after reopening, got $RAWVOL_STATUS"
	rawvol_release
	rawvol_verify_failure_kept $RAWVOL $1
}

function start_over
{
	rawvol_release
	log_must zfs set quota=none $PARENT
	destroy_raw_zvol $RAWVOL
}

log_assert "A raw zvol whose initialization was stopped partway opens when" \
    "there is too little space to resume"
log_onexit cleanup

log_must zfs create $PARENT
log_must set_tunable64 zfs_dirty_data_max $((16 * 1024 * 1024))

# Without gang blocks, initialization gives up with ENOSPC, which is final.
for ((i = 0; i < 5; i++)); do
	stop_partway && break
	log_note "initialization got too far; trying again"
	start_over
done
((i < 5)) || log_fail "could not stop initialization partway"
rawvol_hold $RAWDEV || \
    log_fail "could not open $RAWDEV with too little space"
wait_init
[[ $RAWVOL_STATUS == ENOSPC ]] || \
    log_fail "expected ENOSPC, got $RAWVOL_STATUS"
verify_no_restart ENOSPC
start_over

# With gang blocks, initialization starts over at a smaller block size.
log_must set_tunable64 metaslab_force_ganging $((64 * 1024 + 1))
typeset since backoff
for ((i = 0; i < 20; i++)); do
	if ! stop_partway; then
		log_note "initialization got too far; trying again"
		start_over
		continue
	fi
	since=$(dbgmsg_mark)
	rawvol_hold $RAWDEV || \
	    log_fail "could not open $RAWDEV with too little space"
	typeset -i tries=0
	while backoff=$(next_backoff $since) && [[ -z $backoff ]]; do
		if ((tries++ >= 60)); then
			log_raw_zvol_dbgmsg
			log_fail "initialization did not run out of space"
		fi
		sleep 1
	done
	log_note "$backoff"
	[[ $backoff == *"retry 1" ]] && break
	log_note "no allocation was forced to gang; trying again"
	start_over
done
((i < 20)) || log_fail "initialization never started over"

# After freeing what was written, the quota leaves about the volume size, so
# the pass that started over may run out of space.
log_must set_tunable64 metaslab_force_ganging $orig_force_ganging
log_must set_tunable64 zfs_dirty_data_max $orig_dirty_max
wait_init
log_note "initialization after starting over: $RAWVOL_STATUS"
case $RAWVOL_STATUS in
0)		;;
ENOSPC|EDQUOT)	verify_no_restart $RAWVOL_STATUS ;;
*)		log_fail "initialization failed: $RAWVOL_STATUS" ;;
esac
rawvol_release

log_pass "A raw zvol whose initialization was stopped partway opens when" \
    "there is too little space to resume"
