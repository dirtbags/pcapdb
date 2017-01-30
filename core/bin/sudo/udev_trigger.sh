#!/usr/bin/env bash

# Tell udev to refresh its info.

UDEVADMIN_PATH=/sbin/udevadm

$UDEVADMIN_PATH trigger
