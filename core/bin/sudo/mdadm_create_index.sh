#!/usr/bin/env bash

# Create a new MD device for an index. It's a 1 disk Raid 1, which will be expanded later.
# Args
# $1 The name the MD device should have
# $2 The initial disk for the RAID

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

for arg in ${@:4}; do
    if ! check_arg "$arg" "$DEVICE_RE"; then
        exit 1;
    fi
done

if check_arg "$1" "$MD_DEVICE_RE" && check_arg "$2" "$DEVICE_RE"; then
    ${MDADMIN_PATH} --create "$1" -v --raid-devices=2 --level=1 $2 missing
    exit $?
fi

/sbin/udevadm trigger

exit 2
