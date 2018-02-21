FROM ubuntu:16.04

ARG http_proxy
ARG https_proxy

ENV http_proxy=${http_proxy} https_proxy=${https_proxy}

# Wireshark hangs waiting for you to type "no" unless you provide the answer beforehand
RUN echo 'wireshark-common wireshark-common/install-setuid boolean false' | debconf-set-selections

RUN apt-get -y update
RUN apt-get -y install postgresql-common

# Please don't set up a database for me
RUN sed -i 's/#\?\(create_main_cluster\).*/\1 = false/' /etc/postgresql-common/createcluster.conf

# Install things we need
RUN apt-get -y install postgresql postgresql-server-dev-all python3 python3-pip python3-dev virtualenv uwsgi-plugin-python3 libsasl2-dev libldap2-dev supervisor \
  rabbitmq-server nginx \
  librabbitmq4 ethtool xfsprogs mdadm \
  tshark cmake libnuma-dev libssl-dev libhugetlbfs-dev libpcap-dev \
  busybox \
  wget locate acl sudo iputils-ping
# XXX: Last line above is undocumented dependencies

### Install PFRing
# XXX: We need to make sure we have legal permission to redistribute NTOP code
WORKDIR /tmp
RUN wget http://apt-stable.ntop.org/16.04/all/apt-ntop-stable.deb && \
  dpkg -i apt-ntop-stable.deb && \
  rm apt-ntop-stable.deb && \
  apt-get -y update && \
  apt-get -y install pfring


###
### Install
###

## Pre-install

# XXX: Combine these into `COPY . /src` for production; we're omitting the docker directory so builds don't have to replay every step when we change this file (the one you're reading right now)
COPY Makefile requirements.txt /src/
COPY core/ /src/core/
COPY etc/ /src/etc/
COPY indexer/ /src/indexer/
COPY system/ /src/system/

RUN useradd --system capture

WORKDIR /src/
# Write out an nginx configuration so the install-search-head won't
RUN sed 's,DESTDIR,/var/pcapdb,g; s,server_name HOSTNAME,server_name _,; s,HOSTNAME,pcapdb,g' etc/nginx.conf.tmpl > /etc/nginx/conf.d/pcapdb.conf
# Stub out `service` so install-search-head won't try (and fail) to start daemons
COPY docker/fake-service.sh /usr/local/bin/service
# Stub out openssl so install-search-head won't generate a cert and block the build waiting for keyboard input
COPY docker/fake-service.sh /usr/local/bin/openssl

## Install
# XXX: Combine these into `RUN make install-common install-search-head install...` for production
RUN make install-common
RUN make install-search-head
RUN make install-capture-node
RUN make install-monolithic

## Post-install

# Remove the source tree and fake commands
#RUN rm -rf /src
RUN rm -rf /usr/local/bin/service /usr/local/bin/openssl

COPY docker/pcapdb-omnibus-init.sh /usr/local/sbin/

# XXX: As much as possible, things need to log to stdout

ENV PGDATA /var/lib/postgresql/data
WORKDIR /var/pcapdb
CMD [ "/usr/local/sbin/pcapdb-omnibus-init.sh" ]
