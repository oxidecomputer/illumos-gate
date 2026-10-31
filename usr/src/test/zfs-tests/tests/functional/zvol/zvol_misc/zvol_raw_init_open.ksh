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
# Opening a raw zvol while it is being initialized must not deadlock with
# the txg sync of the pool that contains it.
#
# The zero thread used to hold the zvol's state lock while waiting for a
# txg.  An open of the raw zvol holds zfsdev_state_lock while waiting for
# that state lock, and when the containing pool is itself on a zvol, its
# txg sync needs zfsdev_state_lock to flush that vdev.
#
# STRATEGY:
# 1. Create a pool (volpool) on a non-raw zvol ($TESTPOOL/basevol)
# 2. Delay the I/O of $TESTPOOL so volpool's txgs take a long time to sync
# 3. Create a raw zvol (volpool/rawvol), which starts its initialization
# 4. Repeatedly open it with "zpool create" until that succeeds
# 5. Verify that this finishes rather than hanging, and that some attempts
#    were made while the raw zvol was being initialized
#
# If the deadlock is present, the system is left hung and needs a reboot
# without syncing (reboot -n).
#

verify_runnable "global"

typeset DONE=$TEST_BASE_DIR/zvol_raw_init_open.done
typeset STOP=$TEST_BASE_DIR/zvol_raw_init_open.stop
typeset TRIES=$TEST_BASE_DIR/zvol_raw_init_open.tries
typeset -i TIMEOUT_SECS=600
typeset create_pid=""

function cleanup
{
	typeset -i i

	#
	# Stop the opener before destroying anything.  It checks $STOP
	# between attempts, so it doesn't leave a "zpool create" running.
	#
	if [[ -n $create_pid ]]; then
		touch $STOP
		for (( i = 0; i < 60; i++ )); do
			kill -0 $create_pid 2>/dev/null || break
			sleep 1
		done
		kill $create_pid 2>/dev/null
		wait $create_pid
	fi
	zinject -c all >/dev/null
	poolexists volrawvolpool && destroy_pool volrawvolpool
	destroy_raw_zvol volpool/rawvol
	poolexists volpool && destroy_pool volpool
	if datasetexists $TESTPOOL/basevol; then
		log_must zfs destroy $TESTPOOL/basevol
	fi
	rm -f $DONE $STOP $TRIES
}

log_assert "Opening a raw zvol during initialization does not deadlock"
log_onexit cleanup

rm -f $DONE $STOP $TRIES
log_must zfs create -V 1g $TESTPOOL/basevol
log_must zpool create volpool /dev/zvol/dsk/$TESTPOOL/basevol
log_must zinject -d $DISK -D 100:1 $TESTPOOL
#
# Keep the raw zvol small: with $TESTPOOL's I/O delayed, initializing it
# takes about a minute.
#
log_must zfs create -V 128m -o rawvol=on volpool/rawvol

#
# "zpool create" fails with EINPROGRESS ("currently unavailable") until the
# raw zvol has been preallocated; each attempt opens the zvol again.  No
# log_* functions here: they would run the test's cleanup in this subshell.
#
(
	typeset -i n=0
	while [[ ! -f $STOP ]]; do
		echo $(( ++n )) > $TRIES
		if zpool create volrawvolpool /dev/zvol/dsk/volpool/rawvol \
		    >/dev/null 2>&1; then
			touch $DONE
			break
		fi
		sleep 0.2
	done
) &
create_pid=$!

typeset -i start=$SECONDS
while [[ ! -f $DONE ]] && (( SECONDS - start < TIMEOUT_SECS )); do
	sleep 1
done
if [[ ! -f $DONE ]]; then
	log_raw_zvol_dbgmsg
	log_fail "zpool create on an initializing raw zvol did not succeed" \
	    "in ${TIMEOUT_SECS}s"
fi
typeset -i tries=$(<$TRIES) elapsed=$((SECONDS - start))
log_note "zpool create succeeded after ${elapsed}s and $tries attempts"
(( tries > 1 )) || \
    log_fail "the raw zvol was not opened again during its initialization"
log_must poolexists volrawvolpool

log_pass "Opening a raw zvol during initialization does not deadlock"
