#!/usr/bin/env bash

# Remove a spare disk from the given md device
# Args
# $1 The MD device to remove the spare from
# $2 The spare disk

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

if check_arg "$1" "$MD_DEVICE_RE" && check_arg "$2" "$DEVICE_RE"; then
    ${MDADMIN_PATH} $1 -remove $2
    exit $?
fi
exit 2
