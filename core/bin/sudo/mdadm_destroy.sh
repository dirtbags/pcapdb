#!/usr/bin/env bash

# Destroy the given RAID component. This just uses Mdadm to zero the device's superblock.
# Args
# $1... The raid components to destroy

MDADMIN_PATH=/sbin/mdadm

PATH="$( dirname "${BASH_SOURCE[0]}" )"
source ${PATH}/libs.sh

for arg in ${@:3}; do
    if ! check_arg "$arg" "$DEVICE_RE"; then
        exit 1;
    fi
    if is_mounted "$1"; then
        exit 1;
    fi
done

${MDADMIN_PATH} --zero-superblock $@
exit $?
