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
  wget locate acl sudo
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

WORKDIR /var/pcapdb/
# Generate an actual usable SSL pair.
RUN openssl req -nodes -x509 -newkey rsa:4096 -days 9999 -keyout /etc/ssl/pcapdb.key -out /etc/ssl/pcapdb.cert \
  -subj '/CN=pcapdb'
# Write out an nginx configuration so the install-search-head won't
# XXX: remove the line with a hostname match, since that will never happen
COPY etc/nginx.conf.tmpl etc/nginx.conf.tmpl
RUN sed 's,DESTDIR,/var/pcapdb,g; s,HOSTNAME,pcapdb,g' etc/nginx.conf.tmpl > /etc/nginx/conf.d/pcapdb.conf
# Stub out `service` so install-search-head won't try (and fail) to start a daemon
COPY docker/fake-service.sh /usr/local/bin/service
# Stub out openssl so install-search-head won't generate a cert and block the build waiting for keyboard input
COPY docker/fake-service.sh /usr/local/bin/openssl

# XXX: Combine these into `COPY . /var/pcapdb` for production
COPY Makefile requirements.txt /var/pcapdb/
COPY core/ /var/pcapdb/core/
COPY etc/ /var/pcapdb/etc/
COPY indexer/ /var/pcapdb/indexer/
COPY system/ /var/pcapdb/system/

## Install
# XXX: Combine these into `RUN make install-common install-search-head install...` for production
RUN make install-common
RUN make install-search-head
RUN make install-capture-node
RUN make install-monolithic

## Post-install

# Remove the fake commands
RUN rm /usr/local/bin/service
RUN rm /usr/local/bin/openssl

# XXX: If possible we should avoid running systemd in a container. IIRC PcapDB already installs a service manager, maybe we could use that?
# XXX: As much as possible, things need to log to stderr

CMD [ "/bin/bash" ]
