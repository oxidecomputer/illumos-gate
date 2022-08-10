#! /bin/bash
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
# Copyright 2022 Oxide Computer Co.
#

set -eu
set -o pipefail

rev="$(git rev-parse --short=12 HEAD)"
tmpdir="$(mktemp -td platform.XXXXXX)"
outdir="${tmpdir}/platform-${rev}"
outfile="${outdir}/phase2.tar"

if [ -z "${ROOT:+x}" ]; then
	printf 'error: ROOT not set.  Are you in bldenv?\n' >&2
	exit 1
fi

mkdir -p "${outdir}"
(cd ${ROOT} && tar cf ${outfile} \
	kernel/drv \
	kernel/fs \
	kernel/misc \
	kernel/strmod \
	lib/64 \
	lib/amd64/lib* \
	lib/lib* \
	usr/bin \
	usr/lib/64 \
	usr/lib/amd64/lib* \
	usr/lib/cfgadm \
	usr/lib/devfsadm \
	usr/lib/fm \
	usr/lib/lib* \
	usr/lib/security \
	usr/sbin \
	usr/xpg6/bin \
	sbin/zfs \
	sbin/zpool)

if [ $? != 0 ]; then
	printf 'tar failed; run it manually to debug\n' >&2
	exit 1
else
	printf '%s\n' "${outfile}"
	exit 0
fi

