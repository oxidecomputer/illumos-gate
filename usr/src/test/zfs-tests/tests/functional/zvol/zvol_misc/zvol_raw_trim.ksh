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
# Verify that raw zvol trim functionality works correctly
#
# STRATEGY:
# 1. Create a pool (volpool) on a non-raw zvol ($TESTPOOL/basevol)
# 2. Create a raw zvol (volpool/rawvol) in volpool
# 3. Create a new pool (volrawvolpool) on the raw zvol
# 4. Create a filesystem and write data to it
# 5. Remove the data and trim the pool
# 6. Verify that the underlying non-raw zvol's REFER size decreases
#

verify_runnable "global"

function cleanup
{
	if poolexists volrawvolpool; then
		log_must zpool destroy volrawvolpool
	fi
	if datasetexists volpool/rawvol; then
		log_must zfs destroy volpool/rawvol
	fi
	if poolexists volpool; then
		log_must zpool destroy volpool
	fi
	if datasetexists $TESTPOOL/basevol; then
		log_must zfs destroy $TESTPOOL/basevol
	fi
}

log_assert "Verify that raw zvol trim functionality works correctly"
log_onexit cleanup

# Create base zvol for volpool
log_must zfs create -V 1g $TESTPOOL/basevol

# Create volpool on the zvol
log_must zpool create volpool /dev/zvol/dsk/$TESTPOOL/basevol

# Create raw zvol in volpool
log_must zfs create -V 500m -o rawvol=on volpool/rawvol

# Create pool on the raw zvol, retrying while the raw zvol is still
# being initialized
while true; do
	output=$(zpool create volrawvolpool /dev/zvol/dsk/volpool/rawvol 2>&1)
	if [ $? -eq 0 ]; then
		break
	fi
	if echo "$output" | \
	    grep -q "one or more devices is currently unavailable"; then
		log_note "Retrying zpool create: $output"
		sleep 1
	else
		log_fail "zpool create failed: $output"
	fi
done

# Create filesystem
log_must zfs create volrawvolpool/fs

# Get initial REFER size of $TESTPOOL/basevol
log_must zpool sync volrawvolpool
log_must zpool sync $TESTPOOL
initial_refer=$(zfs get -Hp -o value refer $TESTPOOL/basevol)
log_note "Initial REFER size of $TESTPOOL/basevol: $initial_refer"

# Write data to the filesystem
log_must dd if=/dev/urandom of=/volrawvolpool/fs/testfile bs=1M count=100

# Get REFER size after writing
log_must zpool sync volrawvolpool
log_must zpool sync $TESTPOOL
after_write_refer=$(zfs get -Hp -o value refer $TESTPOOL/basevol)
log_note "REFER size after writing: $after_write_refer"

# Remove the file
log_must rm /volrawvolpool/fs/testfile

# Get REFER size after removing file
log_must zpool sync volrawvolpool
log_must zpool sync $TESTPOOL
after_remove_refer=$(zfs get -Hp -o value refer $TESTPOOL/basevol)
log_note "REFER size after removing file: $after_remove_refer"

# Trim the pool
log_must zpool trim volrawvolpool

# Wait a bit for trim to complete
sleep 5

# note the output of `zpool status`
log_note "zpool status after trim:"
zpool status volrawvolpool

# Get final REFER size
log_must zpool sync volrawvolpool
log_must zpool sync $TESTPOOL
final_refer=$(zfs get -Hp -o value refer $TESTPOOL/basevol)
log_note "Final REFER size after trim: $final_refer"

# Verify that REFER size decreased after trim
if [ "$final_refer" -ge "$after_write_refer" ]; then
	log_fail "REFER size did not decrease after trim: $after_write_refer -> $final_refer"
fi

log_pass "Raw zvol trim functionality works correctly"