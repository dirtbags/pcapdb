#!/bin/bash

INFILE=/data/data.pcap
OUTFILE=/data/random.pcap
IFACE=eth10

#modprobe dummy
#ip link set name eth10 dev dummy0

SEED=9813
while true; do
    SEED=$(expr ${SEED} + 1)
    tcprewrite -i $INFILE -o $OUTFILE -s ${SEED}
    tcpreplay -x 100.0 -i ${IFACE} ${OUTFILE} 
done
