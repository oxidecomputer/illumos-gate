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

SIZE=81920
USR_SIZE=49152

rev="$(git rev-parse --short=12 HEAD)"
base="$(cd "$(dirname "$0")" && pwd)"
tmpdir="$(mktemp -td platform.XXXXXX)"
manifest="${base}/ramdisk.manifest"

(cd "${base}/tools/builder" && make >&2)

"${base}/tools/build_ramdisk" -m "${manifest}" -o "${tmpdir}" \
	-s "${SIZE}" -u "${USR_SIZE}" "${base}/extras" "${ROOT}" \
	"${ADJUNCT_PROTO:-/}" / >&2

if [ $? != 0 ]; then
	printf 'build_ramdisk failed; run it manually to debug\n' >&2
	exit 1
else
	printf '%s\n' "${tmpdir}/platform-${rev}/ramdisk.ufs"
	exit 0
fi
