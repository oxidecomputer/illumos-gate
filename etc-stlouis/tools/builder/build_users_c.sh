#!/usr/bin/bash
#
# Copyright (c) 2010 Joyent Inc., All rights reserved.
# Copyright 2022 Oxide Computer Co.
#

ROOT=$1

if [[ ! -f ${ROOT}/etc/passwd ]]; then
    echo "FATAL: missing ${ROOT}/etc/paswd" >&2
    exit 1
fi
if [[ ! -f ${ROOT}/etc/group ]]; then
    echo "FATAL: missing ${ROOT}/etc/group" >&2
    exit 1
fi

cat <<EOF
/*
 * This is an autogenerated file; do not edit.  See build_users_c.sh.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

gid_t
gid_from_name(const char *group)
{
	gid_t gid = (gid_t)-1;
EOF

cat ${ROOT}/etc/group | awk -F':' 'NR>1{ printf "else " };{ print "if (strcmp(\"" $1 "\", group) == 0) gid = (gid_t) " $3 ";" }' | sed -e "s/^/	/"
cat <<EOF

	return(gid);
};

EOF

cat <<EOF
uid_t
uid_from_name(const char *user)
{
	uid_t uid = (uid_t)-1;

EOF
cat ${ROOT}/etc/passwd | awk -F':' 'NR>1{ printf "else " };{ print "if (strcmp(\"" $1 "\", user) == 0) uid = (uid_t) " $3 ";" }' | sed -e "s/^/	/"
cat <<EOF

	return (uid);
};
EOF
