#! /bin/sh

set -e

log () {
  echo "[Omnibus Init] $*" 1>&2
}

start () {
  log "Starting $1..."
}

# XXX: Tech Debt: this is not a great way to start these services. Maybe that doesn't matter, though?

start PostgreSQL
pg_ctlcluster 9.5 main start

start RabbitMQ
rabbitmq-server &

start Supervisor
supervisord -c /etc/supervisor/supervisord.conf

if [ ! -f /var/pcapdb/initialized ]; then
  log "Running initial setup..."
  /var/pcapdb/core/bin/post-install.sh -s -c 127.0.0.1
  touch /var/pcapdb/initialized
fi
