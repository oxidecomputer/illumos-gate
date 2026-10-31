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
# Opening a raw zvol while it is being initialized must not stall opens of
# other zvols.
#
# The zero thread used to hold the raw zvol's state lock while waiting for
# a txg and while building the block -> DVA mapping.  An open of the raw
# zvol holds zfsdev_state_lock while waiting for that state lock, so every
# other zvol open, close and ioctl waited too.
#
# STRATEGY:
# 1. Create and open a regular zvol ($TESTPOOL/othervol)
# 2. Delay the I/O of $TESTPOOL so its txgs take a long time to sync
# 3. Create a raw zvol, which starts its initialization
# 4. Hold both zvols open, and repeatedly open the raw zvol again until a
#    read from it succeeds
# 5. Meanwhile, repeatedly open and read othervol, timing each attempt
# 6. Verify that no attempt took more than MAX_OPEN_SECS
#
# Holding both zvols open keeps the timed opens and closes from being first
# opens or last closes: zvol_last_close() waits for a txg while holding
# zfsdev_state_lock, which also stalls other zvols but isn't specific to raw
# zvols.
#

verify_runnable "global"

typeset DONE=$TEST_BASE_DIR/zvol_raw_init_stall.done
typeset HELD=$TEST_BASE_DIR/zvol_raw_init_stall.held
typeset MEASURED=$TEST_BASE_DIR/zvol_raw_init_stall.measured
typeset reader_pid=""
typeset OTHER=/dev/zvol/rdsk/$TESTPOOL/othervol
typeset RAW=/dev/zvol/rdsk/$TESTPOOL/rawvol
typeset -i MAX_OPEN_SECS=2
typeset -i TIMEOUT_SECS=600

function cleanup
{
	touch $MEASURED
	if [[ -n $reader_pid ]]; then
		kill $reader_pid 2>/dev/null
	fi
	wait	# for the background jobs to close the zvols
	destroy_raw_zvol $TESTPOOL/rawvol
	if datasetexists $TESTPOOL/othervol; then
		log_must zfs destroy $TESTPOOL/othervol
	fi
	rm -f $DONE $HELD $MEASURED
}

log_assert "Opening a raw zvol during initialization does not stall" \
    "other zvols"
log_onexit cleanup

rm -f $DONE $HELD $MEASURED
log_must zfs create -V 100m $TESTPOOL/othervol
log_must dd if=$OTHER of=/dev/null bs=512 count=1

log_must zinject -d $DISK -D 200:1 $TESTPOOL
log_must zfs create -V 64m -o rawvol=on $TESTPOOL/rawvol

# Hold both zvols open until we are done measuring.
(
	exec 3<$RAW 4<$OTHER
	touch $HELD
	while [[ ! -f $MEASURED ]]; do
		sleep 0.5
	done
) &
for (( i = 0; i < 30; i++ )); do
	[[ -f $HELD ]] && break
	sleep 1
done
[[ -f $HELD ]] || log_fail "could not open $RAW and $OTHER"

#
# Reads fail with EINPROGRESS until the raw zvol has been preallocated, and
# then wait for its initialization to finish; each attempt opens it again.
# Raw zvol I/O goes straight to the disks, so it must be a multiple of the
# pool's sector size.
#
(
	while ! dd if=$RAW of=/dev/null bs=128k count=1 >/dev/null 2>&1; do
		sleep 0.1
	done
	touch $DONE
) &
reader_pid=$!

typeset -F3 start elapsed longest=0
typeset -i tries=0
while [[ ! -f $DONE ]] && (( SECONDS < TIMEOUT_SECS )); do
	start=$SECONDS
	dd if=$OTHER of=/dev/null bs=512 count=1 >/dev/null 2>&1 ||
	    log_fail "read from $OTHER failed"
	elapsed=$((SECONDS - start))
	(( elapsed > longest )) && longest=$elapsed
	(( tries++ ))
	sleep 0.1
done

touch $MEASURED
if [[ ! -f $DONE ]]; then
	log_raw_zvol_dbgmsg
	log_fail "raw zvol initialization did not finish"
fi
log_note "longest of $tries opens of othervol: ${longest}s"
(( longest <= MAX_OPEN_SECS )) || \
    log_fail "opening othervol took ${longest}s (max ${MAX_OPEN_SECS}s)"

log_pass "Opening a raw zvol during initialization does not stall" \
    "other zvols"
