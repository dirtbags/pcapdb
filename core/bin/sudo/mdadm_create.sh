#!/usr/bin/env bash

# Create a new MD device
# Args
# $1 The name the MD device should have
# $2 The number of disks in the RAID
# $3 The level of the RAID
# $4... The disks that will comprise this RAID

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

for arg in ${@:4}; do
    if ! check_arg "$arg" "$DEVICE_RE"; then
        exit 1;
    fi
done

if check_arg "$1" "$MD_DEVICE_RE" && is_int "$2" && is_int "$3"; then
    ${MDADMIN_PATH} --create "$1" -v --raid-devices=${2} --level=${3} ${@:4}
    exit $?
fi
exit 2
