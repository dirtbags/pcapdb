#!/bin/bash

# Used to setup postgresql on the in an initial PcapDB installation.
# If run on the search head, will automatically populate the config with the database password.
# The password will have to be manually configured in remote capture nodes.

# Get path to pcapdb from the location of this script.
PCAPDB_PATH=$(dirname $0)
if echo $PCAPDB_PATH | grep -v '^/'; then 
    PCAPDB_PATH=$(pwd)/$PCAPDB_PATH
fi
PCAPDB_PATH=$(dirname $PCAPDB_PATH)
CONFIG_PATH=$(dirname $PCAPDB_PATH)/etc/pcapdb.cfg

# Create the basic pcapdb postgres user. This will prompt you to create a password.
sudo -u postgres createuser capture -l -P
if grep -E 'search_head\s+=\s+True' $CONFIG_PATH; then 
    sudo -u postgres createdb -O capture pcapdb "PcapDB Search Head Database"

fi


