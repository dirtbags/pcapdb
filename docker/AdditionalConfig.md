Building
========

This is stuff you only need to do once.

Set up Docker for proxy
-----------------------

From https://stackoverflow.com/questions/26550360/docker-ubuntu-behind-proxy

    sudo mkdir /etc/systemd/system/docker.service.d
    $:~/code/pcapdb/docker$ sudo vim /etc/systemd/system/docker.service.d/http-proxy.conf
    $:~/code/pcapdb/docker$ sudo systemctl daemon-reload
    $:~/code/pcapdb/docker$ sudo systemctl restart docker
    ca$:~/code/pcapdb/docker$ cat /etc/systemd/system/docker.service.d/http-proxy.conf
    [Service]
    Environment="HTTP_PROXY=http://yourproxy:8080/"
    Environment="HTTPS_PROXY=http://yourproxy:8080/"
    Environment="NO_PROXY=localhost,127.0.0.1"

Docker build
------------

    docker build --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --tag=pcapdb -f Dockerfile ../


Running
=======

This is stuff you need to do every time.


Set up loopback devices
-----------------------

Be sure you're in a directory where you want them to live, NOT the docker directory since the Docker daemon will include them, then run:

    for i in /dev/md*; do sudo mdadm --stop $i; sudo mdadm --remove $i; done
    sudo losetup -D
    for i in $(seq 1 4); do rm -f loop$i.img; truncate -s 8g loop$i.img; sudo losetup /dev/loop$i loop$i.img; done



Start container
----------
Only need to include as many devices that you plan to use for Search Head and Capture Disks.  You can bind to 80 and 443 directly if you do not have other services running.

    docker run --name=pcapdb -p 22080:80 -p 22443:443 \
      -e PCAPDB_HOSTNAME=localhost \
      -e PCAPDB_MAILHOST=mail.server.com \
      --device=/dev/loop1:/dev/loop1 \
      --device=/dev/loop2:/dev/loop2 \
      --device=/dev/loop3:/dev/loop3 \
      --device=/dev/loop4:/dev/loop4 \
      --privileged=true \
      -d pcapdb

Initialize pcapdb
------------------
Go to ```docker_install_setup.md``` to complete disk and user setup!

Load sample capture
-------------------
    sudo docker exec -it pcapdb apt-get -y install curl
    sudo docker exec -it pcapdb curl -o /tmp/http.pcap "https://wiki.wireshark.org/SampleCaptures?action=AttachFile&do=get&target=http.cap"

Load downloaded pcap file into pcapdb

    sudo docker exec -it pcapdb bin/capture -m 80 -r -i /tmp/http.pcap


Fix permissions
---------------

XXX: You may have to do one search first to create directory / files for `chown`'ing

    sudo docker exec -it pcapdb chown -R capture:capture /var/pcapdb/capture/index/


Search!
-------


Start date 2003-whatever, search for "port 80"

One packet in this capture:

    2004-05-13 10:17:37.704928 IP 65.208.228.223.80 > 145.254.160.237.3372: Flags [.], ack 481, win 6432, length 0
