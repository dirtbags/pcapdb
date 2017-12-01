#! /bin/sh

set -e

log () {
  echo "[Omnibus Init] $*" 1>&2
}

start () {
  log "Starting $1..."
}

log "Setting up directories and permissions"
: >> /var/pcapdb/log/django.log
chown capture /var/pcapdb/log/django.log

# XXX: Tech Debt: this is not a great way to start these services. Maybe that doesn't matter, though?

if [ ! -f /etc/ssl/pcapdb.pem ]; then
  log "Generating /etc/ssl/pcapdb.pem"
  openssl req -nodes -x509 -newkey rsa:4096 -days 9999 -keyout /etc/ssl/pcapdb.key -out /etc/ssl/pcapdb.pem \
  -subj '/CN=pcapdb'
fi

start NGINX
nginx

start PostgreSQL
pg_ctlcluster 9.5 main start

start RabbitMQ
rabbitmq-server &

# XXX: what is a more appropriate place for initialized?
if [ ! -f /var/pcapdb/initialized ]; then
  log "Running initial setup..."
  yes yes | /var/pcapdb/core/bin/post-install.sh -s -c 127.0.0.1
  touch /var/pcapdb/initialized
fi

log "Writing configuration file"
[ -n "$PCAPDB_HOSTNAME" ] && sed -i 's/.*allowed_hosts =.*/allowed_hosts = '"$PCAPDB_HOSTNAME"'/' /var/pcapdb/etc/pcapdb.cfg

start Supervisor
supervisord -n -c /etc/supervisor/supervisord.conf
