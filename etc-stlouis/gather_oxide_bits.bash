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

if [ $# -lt 1 ]; then
	echo "Usage: $(basename $0) <outdir>"
	exit 1
fi

base="$(cd "$(dirname "$0")" && pwd)"
outdir="$1"

if [ ! -d ${outdir} ]; then
	mkdir -p ${outdir}
fi

cp $ROOT/platform/oxide/kernel/amd64/unix ${outdir}/unix
mv $(${base}/mkrd.bash) ${outdir}/ramdisk
mv $(${base}/mkphase2.bash) ${outdir}/phase2.tar
