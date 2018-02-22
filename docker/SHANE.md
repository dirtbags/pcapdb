Getting things going
====================

First you need to set up your loopback devices.
Be sure you're in a directory where you want them to live, NOT the docker directory,
then run:

    for i in $(seq 1 8); do truncate -s 8g loop$i.img; sudo losetup /dev/loop$i loop$i.img; done

Docker build:

    docker build --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --tag=pcapdb -f Dockerfile ../

Error:

      Sending build context to Docker daemon  34.43GB
      Step 1/30 : FROM ubuntu:16.04
      Get https://registry-1.docker.io/v2/: net/http: request canceled while
      waiting for connection (Client.Timeout exceeded while awaiting headers)

From https://stackoverflow.com/questions/26550360/docker-ubuntu-behind-proxy

    sudo mkdir /etc/systemd/system/docker.service.d
    shannon@violet:~/code/pcapdb/docker$ sudo vim /etc/systemd/system/docker.service.d/http-proxy.conf
    shannon@violet:~/code/pcapdb/docker$ sudo systemctl daemon-reload
    shannon@violet:~/code/pcapdb/docker$ sudo systemctl restart docker
    cashannon@violet:~/code/pcapdb/docker$ cat /etc/systemd/system/docker.service.d/http-proxy.conf
    [Service]
    Environment="HTTP_PROXY=http://proxyout.lanl.gov:8080/"
    Environment="HTTPS_PROXY=http://proxyout.lanl.gov:8080/"
    Environment="NO_PROXY=localhost,127.0.0.1,localaddress"


Now you can start the container:

  ```docker run --name=pcapdb -p 22080:80 -p 22443:443 -e PCAPDB_HOSTNAME=refried.cfl.lanl.gov -e PCAPDB_MAILHOST=mail.lanl.gov --device=/dev/loop1:/dev/loop1 --device=/dev/loop2:/dev/loop2 --device=/dev/loop3:/dev/loop3 --device=/dev/loop4:/dev/loop4 --privileged=true -d pcapdb
```

  docker run --name=pcapdb -p 22080:80 -p 22443:443 -e PCAPDB_HOSTNAME=refried.cfl.lanl.gov -e PCAPDB_MAILHOST=mail.lanl.gov --device=/dev/loop3:/dev/loop3 --device=/dev/loop4:/dev/loop4 --device=/dev/loop5:/dev/loop5 --device=/dev/loop6:/dev/loop6 --privileged=true -d pcapdb
