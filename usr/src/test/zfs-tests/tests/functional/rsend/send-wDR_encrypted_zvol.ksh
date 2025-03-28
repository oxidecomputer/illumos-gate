#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Datto Inc. All rights reserved.
# Copyright 2020 Joyent, Inc.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verify that zvols with dedup=on and encryption=on can be sent and received
# with a deduplicated raw send stream.
#
# STRATEGY:
# 1. Create a zvol with dedup and encryption on and put a filesystem on it
# 2. Copy a file into the zvol a few times and take a snapshot
# 3. Repeat step 2 a few times to create more snapshots
# 4. Send all snapshots in a recursive, raw, deduplicated send stream
# 5. Mount the received zvol and verify that all of the data there is correct
#

verify_runnable "both"

function cleanup
{
	if is_linux; then
		ismounted $recvmnt ext4 && log_must umount $recvmnt
		ismounted $mntpnt ext4 && log_must umount $mntpnt
	else
		ismounted $recvmnt ufs && log_must umount $recvmnt
		ismounted $mntpnt ufs && log_must umount $mntpnt
	fi
	[[ -d $recvmnt ]] && log_must rm -rf $keyfile
	[[ -d $mntpnt ]] && log_must rm -rf $keyfile
	destroy_dataset $TESTPOOL/recv "-r"
	destroy_dataset $TESTPOOL/$TESTVOL "-r"
	[[ -f $keyfile ]] && log_must rm $keyfile
	[[ -f $sendfile ]] && log_must rm $sendfile
}
log_onexit cleanup

log_assert "Verify zfs can receive raw, recursive, and deduplicated send streams"

typeset keyfile=/$TESTPOOL/pkey
typeset snap_count=5
typeset zdev=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
typeset mntpnt=$TESTDIR/$TESTVOL
typeset recvdev=$ZVOL_DEVDIR/$TESTPOOL/recv
typeset recvmnt=$TESTDIR/recvmnt
typeset sendfile=$TESTDIR/sendfile

log_must eval "echo 'password' > $keyfile"

log_must zfs create -o dedup=on -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile -V 128M $TESTPOOL/$TESTVOL
log_must block_device_wait

if is_linux; then
	log_must eval "new_fs -t ext4 -v $zdev"
else
	log_must eval "new_fs $zdev"
fi
log_must mkdir -p $mntpnt
log_must mkdir -p $recvmnt
log_must mount $zdev $mntpnt

for ((i = 1; i <= snap_count; i++)); do
	log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=1
	for ((j = 0; j < 10; j++)); do
		log_must cp $mntpnt/file $mntpnt/file$j
	done

	log_must sync
	log_must zfs snap $TESTPOOL/$TESTVOL@snap$i
done

log_must eval "zfs send -wDR $TESTPOOL/$TESTVOL@snap$snap_count > $sendfile"
log_must eval "zfs recv $TESTPOOL/recv < $sendfile"
log_must zfs load-key $TESTPOOL/recv
log_must block_device_wait

log_must mount $recvdev $recvmnt

md5_1=$(cat $mntpnt/* | digest -a md5)
md5_2=$(cat $recvmnt/* | digest -a md5)
[[ "$md5_1" == "$md5_2" ]] || log_fail "md5 mismatch: $md5_1 != $md5_2"

log_pass "zfs can receive raw, recursive, and deduplicated send streams"
