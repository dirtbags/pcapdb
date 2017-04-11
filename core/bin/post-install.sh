#!/bin/bash

# Used to setup postgresql on the in an initial PcapDB installation.
# If run on the search head, will automatically populate the config with the database password.
# The password will have to be manually configured in remote capture nodes.

# Keeping in mind that 1 is False, 0 is True
IS_CAPTURE_NODE=1
IS_SEARCH_HEAD=1
PING_SEARCH_HEAD=0
for var in "$@"; do 
    if [ "$var" = "-s" ]; then 
      IS_SEARCH_HEAD=0
    elif [ "$var" = "-c" ]; then
      IS_CAPTURE_NODE=0
    elif [ "$var" = "-P" ]; then
      PING_SEARCH_HEAD=1
    fi
done

echo -e "\033[1;32m"
echo "Configuring PCAPDB"
echo "------------------"
echo -e "\033[0m"

if [ ${IS_CAPTURE_NODE} -eq 1 -a ${IS_SEARCH_HEAD} -eq 1 ]; then
    echo "Usage: (from the pcapdb installed directory, as root)"
    echo "./core/bin/postgres_setup.sh [-s] [-c] <search_head_ip>\n"
    echo "  -c  Setup postgres for this host as a capture node"
    echo "  -s  Setup postgres for this host as a search head"
    echo "  -P  Don't test the existance of the search head by pinging it."
    echo "  <search_head_ip> The IP address of the search head."
    echo " Both -s and -c may be specified (monolithic mode)"
    exit 1
fi

if ! [ "$(id -un)" = "root" ]; then 
    echo "You must be root to run this command."
    exit 1
fi

for last; do true; done
SEARCH_HEAD_IP=$last
if ! echo -n $SEARCH_HEAD_IP | egrep -q '^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$'; then
    echo "You must provide the ip of the search head."
    exit 1;
fi

if [ $PING_SEARCH_HEAD ]; then 
    if ! ping -c 1 $SEARCH_HEAD_IP >/dev/null 2>/dev/null; then 
        echo "You must provide the ip of the search head." 
        echo "Note, we partly test this by pinging it. If you know you can't ping the search head right"
        echo "now, you can disable this check with -P."
        exit 1
    fi
fi

# We're going to use slocate to find some things, so we should do it with an up-to-date db
updatedb
# Use one of our more uniquely named executables to find the pcapdb path.
PCAPDB_PATH="$(dirname $(dirname $(locate --regex 'bin/subindex_search$')))"

pushd $PCAPDB_PATH
 
PCAPDB_CONFIG=${PCAPDB_PATH}/etc/pcapdb.cfg

PG_CONF_PATH=$(dirname $(locate --regex 'pg_hba.conf$'))
echo 'pg config path' ${PG_CONF_PATH}

PCAPDB_USER=capture
RABBITMQCTL=$(which rabbitmqctl)
DD=$(which dd)
HASHER=$(which sha256sum)
PASSWD=$(${DD} if=/dev/urandom count=1 bs=512 2>/dev/null | ${HASHER} - | head -c 16)
PASSWD_ENC="md5$(echo -n ${PASSWD}${PCAPDB_USER} | md5sum | awk '{ print $1 }')"

ALL_APPS='stats_api login_api core task_api search_head_api login_gui search_head_gui capture_node_api'

echo ${PASSWD} ${PASSWD_ENC}

SUDO_PG='sudo -u postgres'

if ! ${SUDO_PG} psql -tAc "select 1 from pg_roles WHERE rolname='${PCAPDB_USER}'" | grep -q 1; then 
    # Create the basic pcapdb postgres user. If this is purely a capture node, the password will
    # never be used, and isn't recorded.
    if ${SUDO_PG} psql -tAc  "CREATE ROLE ${PCAPDB_USER} PASSWORD '${PASSWD_ENC}' LOGIN"; then 
        echo "${PCAPDB_USER} db role created"
    else
        echo "Failed to create ${PCAPDB_USER} db role"
        exit 1
    fi

    if [ ${IS_SEARCH_HEAD} -eq 0 ]; then 
        # Put our password into the pcapdb config file.
        sed --in-place "s/^db_pass *=.*/db_pass=${PASSWD}/" ${PCAPDB_CONFIG}
        echo "Set db password in ${PCAPDB_CONFIG}"
    fi

fi

echo "Setting search head ip in ${PCAPDB_CONFIG}"
sed --in-place "s/^search_head_host *=.*/search_head_host = ${SEARCH_HEAD_IP}/" ${PCAPDB_CONFIG}


if [ ${IS_SEARCH_HEAD} -eq 0 ]; then 
    echo "Setting host as a search head in ${PCAPDB_CONFIG}"
    sed --in-place 's/^search_head *=.*/search_head = True/' ${PCAPDB_CONFIG}

    echo "Creating a random session secret in ${PCAPDB_CONFIG}"
    SESSION_SECRET=$(${DD} if=/dev/urandom count=1 bs=512 2>/dev/null | ${HASHER} - | head -c 32)
    echo "Setting the session secret"
    sed --in-place "s/^session_secret.*/session_secret = ${SESSION_SECRET}/" ${PCAPDB_CONFIG}

    if ! ${SUDO_PG} psql -tAc "select 1 from pg_database WHERE datname='pcapdb'" | grep -q 1; then
        ${SUDO_PG} createdb -O capture pcapdb
    fi

    # Allow for external connections on the postgres server, but only for the search head.
    LISTEN_ADDR="listen_addresses = 'localhost,${SEARCH_HEAD_IP}'     # what IP address(es) to listen on;"
    sed --in-place "s/^[# ]*listen_addresses *=.*/${LISTEN_ADDR}/" ${PG_CONF_PATH}/postgresql.conf
    echo "Tried to allow postgres to listen on the search head ip address"

    CONN_LINE="host    pcapdb       all             ${SEARCH_HEAD_IP}/32            md5"
    if ! grep "${CONN_LINE}" ${PG_CONF_PATH}/pg_hba.conf; then
        echo $CONN_LINE >> ${PG_CONF_PATH}/pg_hba.conf
        echo "Allowing password connections to postgres via the search IP"
    fi

    # Restarting Postgres
    service postgresql restart

    echo "Setting up RabbitMQ, and setting password in ${PCAPDB_CONF}"
    # Setup rabbitmq for use with PcapDB
    ${RABBITMQCTL} delete_user guest
    # This generates our rabbitmq password
    RABBIT_PASS=$(${DD} if=/dev/urandom count=1 bs=512 2>/dev/null | ${HASHER} - | head -c 16)
    ${RABBITMQCTL} delete_user pcapdb
    ${RABBITMQCTL} add_user pcapdb ${RABBIT_PASS}
    ${RABBITMQCTL} set_permissions -p / pcapdb '.*' '.*' '.*'
    echo "Setting the rabbitmq/amqp password in etc/pcapdb.cfg"
    sed -i "s/^#\? *amqp_password *=.*$/amqp_password=${RABBIT_PASS}/" ${PCAPDB_CONFIG}

fi

if [ ${IS_CAPTURE_NODE} -eq 0 ]; then
    echo "Setting host as a capture_node in ${PCAPDB_CONFIG}"
    sed --in-place 's/^capture_node *=.*/capture_node = True/' ${PCAPDB_CONFIG}

    if ! ${SUDO_PG} psql -tAc "select 1 from pg_database WHERE datname='capture_node'" | grep -q 1; then
    ${SUDO_PG} createdb -O capture capture_node
    fi

    if ! ${SUDO_PG} psql -tAc "select 1 from pg_roles WHERE rolname='root'" | grep -q 1; then 
        # Create a 'root' role with the same privileges as the PCAPDB_USER. We just reuse the
        # password, though it won't ever actually get used.
        if ${SUDO_PG} psql -tAc  "CREATE ROLE root PASSWORD '${PASSWD_ENC}' LOGIN"; then 
            echo "'root' db role created"
        else
            echo "Failed to create ${PCAPDB_USER} db role"
            exit 1
        fi
        if ${SUDO_PG} psql -tAc  "GRANT ${PCAPDB_USER} TO root"; then 
            echo "'root' db role given priviledges"
        else
            echo "Failed to give root role access to pcapdb databases."
            exit 1
        fi
    fi
fi

./bin/python core/manage.py makemigrations $ALL_APPS

if [ ${IS_SEARCH_HEAD} -eq 0 ]; then 
    sudo -u ${PCAPDB_USER} ./bin/python core/manage.py migrate
fi

if [ ${IS_CAPTURE_NODE} -eq 0 ]; then
    # The capture node database uses peer authentication, so this has to run as the pcapdb user
    # (capture).
    sudo -u ${PCAPDB_USER} ./bin/python core/manage.py migrate --database=capture_node

    sudo -u ${PCAPDB_USER} ./bin/python core/manage.py collectstatic

    if [ ${IS_SEARCH_HEAD} -eq 1 ]; then 
        echo -e "\033[1;31m"
        echo "You will still have to do the following manually."
        echo " - Add the search head db password to ${PCAPDB_CONFIG}"
        echo " - Add the rabbitmq password to ${PCAPDB_CONFIG}"
        echo " - Restart everything in supervisorctl"
        echo -e "\033[0m"
    fi
    supervisorctl restart capture_runner
fi

supervisorctl restart pcapdb_celery

