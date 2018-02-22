Getting things going
====================

First you need to set up your loopback devices.
Be sure you're in a directory where you want them to live,
then run:

    for i in /dev/md*; do mdadm --stop $i; mdadm --remove $i; done
    losetup -D
    for i in $(seq 1 8); do rm -f loop$i.img; truncate -s 8g loop$i.img; sudo losetup /dev/loop$i loop$i.img; done
    

Now you can start the container:

    docker run --name=pcapdb -p 22080:80 -p 22443:443 -e PCAPDB_HOSTNAME=refried.cfl.lanl.gov -e PCAPDB_MAILHOST=mail.lanl.gov --device=/dev/loop1:/dev/loop1 --device=/dev/loop2:/dev/loop2 --device=/dev/loop3:/dev/loop3 --device=/dev/loop4:/dev/loop4 --privileged=true -d pcapdb

Once disks are set up, load in some pcap:

    docker exec -it pcapdb 'bin/capture -m 80 -r -i /src/indexer/tests/data/many_sessions.pcap'
