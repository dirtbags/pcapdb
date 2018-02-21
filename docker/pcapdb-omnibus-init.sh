#! /bin/sh

set -e

log () {
  echo "[Omnibus Init] $*" 1>&2
}


log "Writing configuration file"
cfgset () {
  config_item=$1
  value=$2
  variable_name=${3:-MISSING_VARIABLE_NAME}
  if [ -z "$value" ]; then
    echo "ERROR: \$$variable_name must be set!" 1>&2
    exit 1
  fi
  
  sed -i "s/^[# ]*\($config_item\) *=.*/\1 = $value/" /var/pcapdb/etc/pcapdb.cfg
}
cfgset allowed_hosts "$PCAPDB_HOSTNAME" PCAPDB_HOSTNAME
cfgset search_head_host "$PCAPDB_HOSTNAME" PCAPDB_HOSTNAME
cfgset search_head_ui_host "${PCAPDB_HOSTNAME}${PCAPDB_PORT:+:}${PCAPDB_PORT}" PCAPDB_HOSTNAME
cfgset host "$PCAPDB_MAILHOST" PCAPDB_MAILHOST
cfgset capture_node true
cfgset search_head true


log "Setting up directories and permissions"
: >> /var/pcapdb/log/django.log
chown capture /var/pcapdb/log/django.log

# XXX: Tech Debt: this is not a great way to start these services. Maybe that doesn't matter, though?


if [ ! -f /etc/ssl/pcapdb.pem ]; then
  log "Generating /etc/ssl/pcapdb.pem"
  openssl req -nodes -x509 -newkey rsa:4096 -days 9999 -keyout /etc/ssl/pcapdb.key -out /etc/ssl/pcapdb.pem \
  -subj '/CN=pcapdb'
fi


log "Starting NGINX"
nginx

PGBIN=/usr/lib/postgresql/9.5/bin
if [ ! -f $PGDATA/PG_VERSION ]; then
  log "Setting up PostgreSQL"
  sudo -u postgres $PGBIN/pg_ctl -D $PGDATA init
fi

log "Starting PostgreSQL"
sudo -u postgres $PGBIN/pg_ctl -D $PGDATA start


log "Starting RabbitMQ"
rabbitmq-server &


# XXX: what is a more appropriate directory for `initialized`?
if [ ! -f /var/pcapdb/initialized ]; then
  log "Running initial setup..."
  yes yes | /var/pcapdb/core/bin/post-install.sh -s -c 127.0.0.1
  touch /var/pcapdb/initialized
fi


log "Starting Supervisor"
supervisord -c /etc/supervisor/supervisord.conf

busybox syslogd -n -O /dev/stdout
