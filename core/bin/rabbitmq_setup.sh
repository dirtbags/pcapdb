#!/bin/bash

echo This sets up the rabbitmq server for the pcapdb search head.
echo It typically only needs to be run once.
echo The password will be set in the pcapdb.conf file automatically, but will 
echo need to be distributed to the capture nodes and set in their rabbitmq.conf files manually.

RABBITMQCTL=$(which rabbitmqctl)
DD=$(which dd)
HASHER=$(which sha1sum)
CONFIG_PATH=$(dirname $0)
if echo $CONFIG_PATH | grep -v '^/'; then 
    CONFIG_PATH=$(pwd)/$CONFIG_PATH
fi
CONFIG_PATH=$(dirname $CONFIG_PATH)
CONFIG_PATH=$(dirname $CONFIG_PATH)/etc/pcapdb.cfg
echo $CONFIG_PATH


# Setup rabbitmq for use with PcapDB
# This only needs to run on the search head.
${RABBITMQCTL} delete_user guest
# This generates our rabbitmq password
${DD} if=/dev/urandom count=1 bs=512 | ${HASHER} - | head -c 16 > /tmp/.rabbitmq_pass
${RABBITMQCTL} delete_user pcapdb
${RABBITMQCTL} add_user pcapdb $(cat /tmp/.rabbitmq_pass)
${RABBITMQCTL} set_permissions -p / pcapdb '.*' '.*' '.*'
echo "Setting the rabbitmq/amqp password in etc/pcapdb.cfg"
sed -i "s/^#\? *amqp_password *=.*$/amqp_password=$(cat /tmp/.rabbitmq_pass)/" ${CONFIG_PATH}
shred /tmp/.rabbitmq_pass
