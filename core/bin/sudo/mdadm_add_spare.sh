#!/usr/bin/env bash

# Add a spare disk to the given md device
# Args
# $1 The MD device to add the spare to
# $2 The spare disk

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

if check_arg "$1" "$MD_DEVICE_RE" && check_arg "$2" "$DEVICE_RE"; then
    ${MDADMIN_PATH} $1 -a $2
    exit $?
fi
exit 2
