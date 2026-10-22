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
# Verify that device errors under a raw zvol are returned to the caller
# of read(2), write(2) and DKIOCFREE.
#
# Raw zvol I/O goes straight to the leaf vdev's device, bypassing the zio
# pipeline, so zinject cannot inject errors into it.  Instead, the pool
# holding the raw zvol is built on a regular zvol, and errors are injected
# by changing kernel state with mdb:
# - zvol_strategy() fails any I/O at or past zv_volsize with EIO, so
#   setting the base zvol's zv_volsize to 0 makes the device fail all
#   reads and writes.
# - vdev_disk_rawio() fails raw I/O and TRIM with ENXIO while the leaf
#   vdev's vd_ldi_offline is set, as it would be after an LDI offline
#   event.
# The pool must not issue I/O of its own while errors are injected, or it
# would fault the device and suspend the pool.  So the pool is synced first,
# and zfs_txg_timeout is raised so that no txg is synced on its own.  The
# pool is created with cachefile=none, so that it is not opened at boot if
# the test panics the system.
#
# STRATEGY:
# 1. Create a pool on a zvol, and a raw zvol in that pool.  Hold the raw
#    zvol open and wait for its initialization to complete.
# 2. Write data to the raw zvol and read it back.
# 3. Make the base zvol fail all I/O.  Verify that reads and writes of
#    the raw zvol fail.
# 4. Stop the errors.  Verify that the raw zvol still holds the data.
# 5. Verify that DKIOCFREE on the raw zvol succeeds, then mark the leaf
#    vdev offline and verify that it fails.
#

verify_runnable "global"

BASEVOL=$TESTPOOL/rawerr_basevol
BASEDEV=$ZVOL_DEVDIR/$BASEVOL
VOLPOOL=rawerrpool
RAWVOL=$VOLPOOL/rawvol
RAWDEV=$ZVOL_RDEVDIR/$RAWVOL
DATA=$TEST_BASE_DIR/rawerr.data
OUT=$TEST_BASE_DIR/rawerr.out
DATA_MB=4

typeset volsize_addr=""
typeset volsize_saved=""
typeset offline_addr=""
typeset rawvol_open=false
typeset txg_timeout=$(get_tunable zfs_txg_timeout)

function cleanup
{
	log_must set_tunable32 zfs_txg_timeout $txg_timeout
	[[ -n $volsize_addr ]] && restore_volsize
	[[ -n $offline_addr ]] && set_offline 0
	if $rawvol_open; then
		exec 5<&-
		rawvol_open=false
	fi

	destroy_raw_zvol $RAWVOL
	poolexists $VOLPOOL && destroy_pool $VOLPOOL
	datasetexists $BASEVOL && destroy_dataset $BASEVOL
	rm -f $DATA $OUT
}

# Print the address of a field of the zvol_state_t of zvol $1.
function zvol_state_field_addr # <zvol> <field>
{
	typeset minor
	minor=$(ls -lL $ZVOL_RDEVDIR/$1 | awk '{print $6}')
	[[ -n $minor ]] || return 1
	echo "*zfsdev_state::softstate 0t$minor | " \
	    "::print zfs_soft_state_t zss_data | ::print -a zvol_state_t $2" | \
	    mdb -k | awk '{print $1}'
}

# Print the address of a field of the vdev_disk_t of the leaf vdev $2 of
# pool $1.
function vdev_disk_field_addr # <pool> <leaf path> <field>
{
	typeset spa vd
	spa=$(echo "::spa" | mdb -k | awk -v p=$1 '$3 == p {print $1}')
	[[ -n $spa ]] || return 1
	vd=$(echo "$spa::spa -v" | mdb -k | awk -v d=$2 '$4 == d {print $1}')
	[[ -n $vd ]] || return 1
	echo "$vd::print vdev_t vdev_tsd | ::print -a vdev_disk_t $3" | \
	    mdb -k | awk '{print $1}'
}

function fail_basevol_io
{
	volsize_addr=$(zvol_state_field_addr $BASEVOL zv_volsize)
	[[ -n $volsize_addr ]] || log_fail "no zv_volsize for $BASEVOL"
	volsize_saved=$(echo "$volsize_addr/J" | mdb -k | awk '{print $2}')
	log_note "$BASEVOL zv_volsize at $volsize_addr = 0x$volsize_saved"
	typeset -li cur=16#$volsize_saved
	typeset -li want=$(get_prop volsize $BASEVOL)
	(( cur == want )) || \
	    log_fail "unexpected zv_volsize 0x$volsize_saved"
	log_must eval "echo '$volsize_addr/Z 0' | mdb -kw >/dev/null"
}

function restore_volsize
{
	log_must eval "echo '$volsize_addr/Z 0x$volsize_saved' | \
	    mdb -kw >/dev/null"
	volsize_addr=""
}

function set_offline # <0|1>
{
	log_must eval "echo '$offline_addr/W $1' | mdb -kw >/dev/null"
	[[ $1 == 0 ]] && offline_addr=""
}

log_assert "Device errors under a raw zvol are returned to the caller"
log_onexit cleanup

log_must zfs create -V 1g $BASEVOL
log_must zpool create -o cachefile=none $VOLPOOL $BASEDEV
log_must zfs create -V 64m -o rawvol=on $RAWVOL

# Keep the raw zvol open, so that no first open rebuilds its DVA map (which
# reads the pool) while errors are injected.
exec 5<$RAWDEV
rawvol_open=true

rawvol_wait_init $RAWDEV

log_must dd if=/dev/urandom of=$DATA bs=1024k count=$DATA_MB
log_must dd if=$DATA of=$RAWDEV bs=1024k count=$DATA_MB
log_must dd if=$RAWDEV of=$OUT bs=1024k count=$DATA_MB
log_must cmp $DATA $OUT

log_must set_tunable32 zfs_txg_timeout 3600
log_must zpool sync $VOLPOOL
fail_basevol_io
log_mustnot dd if=$RAWDEV of=$OUT bs=1024k count=$DATA_MB
log_mustnot dd if=$RAWDEV of=$OUT bs=8k count=1
log_mustnot dd if=$DATA of=$RAWDEV bs=1024k count=$DATA_MB
log_mustnot dd if=$DATA of=$RAWDEV bs=8k count=1
restore_volsize

log_must dd if=$RAWDEV of=$OUT bs=1024k count=$DATA_MB
log_must cmp $DATA $OUT

typeset -li free_off=$((DATA_MB * 1048576))
log_must dkiocfree $RAWDEV $free_off 1048576
log_must zpool sync $VOLPOOL
offline_addr=$(vdev_disk_field_addr $VOLPOOL $BASEDEV vd_ldi_offline)
[[ -n $offline_addr ]] || log_fail "no vd_ldi_offline for $BASEDEV"
set_offline 1
log_mustnot dkiocfree $RAWDEV $free_off 1048576
log_mustnot dkiocfree $RAWDEV $((free_off + 1048576)) 4096
set_offline 0
log_must dkiocfree $RAWDEV $free_off 1048576

log_must test "$(zpool list -Ho health $VOLPOOL)" = "ONLINE"
log_pass "Device errors under a raw zvol are returned to the caller"
