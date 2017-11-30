FROM ubuntu:16.04

ARG http_proxy
ARG https_proxy

ENV http_proxy=${http_proxy} https_proxy=${https_proxy}

# Wireshark hangs waiting for you to type "no" unless you provide the answer beforehand
RUN echo 'wireshark-common wireshark-common/install-setuid boolean false' | debconf-set-selections

# Install things we need
RUN apt-get -y update && \
  apt-get -y install postgresql postgresql-server-dev-all python3 python3-pip python3-dev virtualenv uwsgi-plugin-python3 libsasl2-dev libldap2-dev supervisor \
  rabbitmq-server nginx \
  librabbitmq4 ethtool xfsprogs mdadm \
  cmake libnuma-dev libssl-dev libhugetlbfs-dev libpcap-dev \
  runit \
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

# XXX: Combine these into `COPY . /var/pcapdb` for production; we're omitting the docker directory so builds don't have to replay every step when we change this file (the one you're reading right now)
COPY Makefile requirements.txt /var/pcapdb/
COPY core/ /var/pcapdb/core/
COPY etc/ /var/pcapdb/etc/
COPY indexer/ /var/pcapdb/indexer/
COPY system/ /var/pcapdb/system/

WORKDIR /var/pcapdb/
# Generate our own SSL keypair
RUN openssl req -nodes -x509 -newkey rsa:4096 -days 9999 -keyout /etc/ssl/pcapdb.key -out /etc/ssl/pcapdb.cert \
  -subj '/CN=pcapdb'
# Write out an nginx configuration so the install-search-head won't
RUN sed 's,DESTDIR,/var/pcapdb,g; s,server_name HOSTNAME,server_name _,; s,HOSTNAME,pcapdb,g' etc/nginx.conf.tmpl > /etc/nginx/conf.d/pcapdb.conf
# Stub out `service` so install-search-head won't try (and fail) to start daemons
COPY docker/fake-service.sh /usr/local/bin/service
# Stub out openssl so install-search-head won't generate a cert and block the build waiting for keyboard input
COPY docker/fake-service.sh /usr/local/bin/openssl

RUN useradd --system capture

## Install
# XXX: Combine these into `RUN make install-common install-search-head install...` for production
RUN make install-common
RUN make install-search-head
RUN make install-capture-node
RUN make install-monolithic

## Post-install

# Remove the fake commands
RUN rm /usr/local/bin/service /usr/local/bin/openssl

COPY docker/pcapdb-omnibus-init.sh /usr/local/sbin/

# XXX: As much as possible, things need to log to stderr

CMD [ "/usr/local/sbin/pcapdb-omnibus-init.sh" ]
