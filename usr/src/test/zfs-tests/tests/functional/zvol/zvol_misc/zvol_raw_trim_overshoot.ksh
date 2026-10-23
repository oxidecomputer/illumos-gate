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

. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# Verify that DKIOCFREE of an extent that runs past the end of a raw zvol
# succeeds and trims up to the end of the volume.
#
# The raw zvol is in a pool on a regular zvol, so a TRIM of the raw zvol
# frees the corresponding part of the regular zvol, which then reads back
# as zeros.  The pool on the regular zvol is created with cachefile=none,
# so that it is not opened at boot if the test panics the system.
#
# STRATEGY:
# 1. Create a pool on a zvol, and a raw zvol in that pool.  Wait for the
#    raw zvol's initialization to complete.
# 2. Fill the raw zvol with random data.
# 3. Issue DKIOCFREE for extents that start inside the raw zvol and end
#    past it: one that ends at twice the volume size, and one whose end
#    overflows a 64-bit offset.  Verify that each succeeds.
# 4. Verify that the trimmed range reads back as zeros up to the end of
#    the volume, and the rest of the data is unchanged.
# 5. Verify that DKIOCFREE of an extent past the end of the raw zvol
#    succeeds and changes nothing.
#

verify_runnable "global"

BASEVOL=$TESTPOOL/trimover_basevol
BASEDEV=$ZVOL_DEVDIR/$BASEVOL
VOLPOOL=trimoverpool
RAWVOL=$VOLPOOL/rawvol
RAWDEV=$ZVOL_RDEVDIR/$RAWVOL
DATA=$TEST_BASE_DIR/trimover.data
OUT=$TEST_BASE_DIR/trimover.out
EXPECT=$TEST_BASE_DIR/trimover.expect
VOL_MB=64
MB=1048576
VOLSIZE=$((VOL_MB * MB))

typeset rawvol_open=false

function cleanup
{
	if $rawvol_open; then
		exec 5<&-
		rawvol_open=false
	fi
	destroy_raw_zvol $RAWVOL
	poolexists $VOLPOOL && destroy_pool $VOLPOOL
	datasetexists $BASEVOL && destroy_dataset $BASEVOL
	rm -f $DATA $OUT $EXPECT
}

# Verify that MB [first, last) of the raw zvol match the data written to it.
function check_data # <first MB> <last MB>
{
	typeset -i count=$(($2 - $1))
	log_must dd if=$RAWDEV of=$OUT bs=1024k iseek=$1 count=$count
	log_must dd if=$DATA of=$EXPECT bs=1024k iseek=$1 count=$count
	log_must cmp $EXPECT $OUT
}

# Verify that MB [first, last) of the raw zvol are zeros.
function check_zeros # <first MB> <last MB>
{
	typeset -i count=$(($2 - $1))
	log_must dd if=$RAWDEV of=$OUT bs=1024k iseek=$1 count=$count
	log_must dd if=/dev/zero of=$EXPECT bs=1024k count=$count
	log_must cmp $EXPECT $OUT
}

log_assert "DKIOCFREE past the end of a raw zvol trims up to the end"
log_onexit cleanup

log_must zfs create -V 1g $BASEVOL
log_must zpool create -o cachefile=none $VOLPOOL $BASEDEV
log_must zfs create -V ${VOL_MB}m -o rawvol=on $RAWVOL

# Keep the raw zvol open, so that rawvol_wait_init's status checks are not
# first opens, each of which would start initialization over.
exec 5<$RAWDEV
rawvol_open=true
rawvol_wait_init $RAWDEV

log_must dd if=/dev/urandom of=$DATA bs=1024k count=$VOL_MB
log_must dd if=$DATA of=$RAWDEV bs=1024k count=$VOL_MB
check_data 0 $VOL_MB

# Ends at twice the volume size.
log_must dkiocfree $RAWDEV $((VOLSIZE - 4 * MB)) $VOLSIZE
check_data 0 $((VOL_MB - 4))
check_zeros $((VOL_MB - 4)) $VOL_MB

# start + length overflows.
log_must dkiocfree $RAWDEV $((VOLSIZE - 8 * MB)) 18446744073709551615
check_data 0 $((VOL_MB - 8))
check_zeros $((VOL_MB - 8)) $VOL_MB

# Entirely past the end.
log_must dkiocfree $RAWDEV $VOLSIZE $VOLSIZE
log_must dkiocfree $RAWDEV $((2 * VOLSIZE)) 18446744073709551615
check_data 0 $((VOL_MB - 8))

log_must test "$(zpool list -Ho health $VOLPOOL)" = "ONLINE"
log_pass "DKIOCFREE past the end of a raw zvol trims up to the end"
