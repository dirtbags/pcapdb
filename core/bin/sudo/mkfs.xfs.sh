#!/usr/bin/env bash

# Build an XFS filesystem on the given device
# Args
# $1 The device to format
# $2 (optional) The label for the device.

MKXFS_PATH=/sbin/mkfs.xfs

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

LABEL=
if [ $# -eq 2 ] && is_label $2; then
    LABEL="-L $2"
fi

if check_arg "$1" "$DEVICE_RE"; then
    is_mounted "$1" && exit 1

    ${MKXFS_PATH} -f -q ${LABEL} "$1"
    exit $?
fi

/sbin/udevadm trigger

exit 1
