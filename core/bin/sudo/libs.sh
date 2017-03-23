#!/usr/bin/env bash

# Only allow md and sd devices
DEVICE_RE='/dev/(md[0-9]+|(sd|xvd)[a-z]+[0-9]*)'
MD_DEVICE_RE='/dev/md[0-9]+'

function check_arg {
    echo "$1" | /bin/grep -E "^$2$" > /dev/null
}

function is_int {
    check_arg "$1" "[0-9]+"
}

function is_label {
    check_arg "$1" "[0-9a-zA-Z_]+"
}

function is_mounted {
    # This checks for the device and any partitions on the device.
    /bin/grep -E "^$1[^a-z]*" /proc/mounts
}
