#!/usr/bin/env bash

# Stop the given MD device.
# Args
# $1 The MD device to stop

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

if check_arg "$1" "$MD_DEVICE_RE"; then
    ${MDADMIN_PATH} --stop "$1"
    exit $?
fi
exit 1
