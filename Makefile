PY_VERS=3.4

# Running pip below without pointing to the postgres bin path will fail.
PGSQL_BINPATH="$(shell dirname $$(locate -r 'pg_config$$' | sort | tail -1))"
PATH_EXPORT=export PATH=$$PATH:${PGSQL_BINPATH}:/usr/local/bin;

# The proxy info for you site
PROXY=
HTTP_PROXY=
ifneq "${PROXY}" ""
	export http_proxy="http://${PROXY}"
	export https_proxy="http://${PROXY}"
endif

DESTDIR=/var/pcapdb
RABBITMQCTL=/usr/sbin/rabbitmqctl
DD=/bin/dd
# This is used to create a password, not a password hash. As such, it's ok to use
# SHA rather a suitable password hasher like crypt.
HASHER=/usr/bin/sha512sum
SHRED=/usr/bin/shred

RSYSLOGD=/etc/rsyslog.d
NGINXD=/etc/nginx/conf.d
LOGROTATED=/etc/logrotate.d
SUDOERSD=/etc/sudoers.d

ifeq "${DESTDIR}" "$(shell pwd)"
  CAPTURE_USER="$(shell whoami)"
  CAPTURE_GROUP=users
  INSTALL_PERMS=
else
  CAPTURE_USER="capture"
  CAPTURE_GROUP="capture"
  INSTALL_PERMS=--owner=${CAPTURE_USER} --group=${CAPTURE_GROUP} 
endif

# Build and install the capture system.
install-common: setup_dirs ${DESTDIR}/bin/python ${DESTDIR}/lib/packages_installed indexer_install core

install-capture-node: install-common capture-node-configs common-configs
install-search-head: install-common search-head-configs common-configs
install-monolithic: install-common capture-node-configs search-head-configs common-configs

# Create the python ${DESTDIR}/bin/python that will run all our python code
${DESTDIR}/bin/python:
	${PATH_EXPORT} /usr/local/bin/virtualenv -p /usr/local/bin/python${PY_VERS} ${DESTDIR}

# Setup rabbitmq for use with PcapDB
# This only needs to run on the search head.
rabbitmq:
	-${RABBITMQCTL} delete_user guest
	# This generates our rabbitmq password
	${DD} if=/dev/urandom count=1 bs=512 | ${HASHER} - | head -c 16 > /tmp/.rabbitmq_pass
	-${RABBITMQCTL} delete_user pcapdb
	${RABBITMQCTL} add_user pcapdb $$(cat /tmp/.rabbitmq_pass)
	${RABBITMQCTL} set_permissions -p / pcapdb '.*' '.*' '.*'
	echo "Setting the rabbitmq/amqp password in etc/pcapdb.cfg"
	sed -i "s/^#\? *amqp_password *=.*$$/amqp_password=$$(cat /tmp/.rabbitmq_pass)/" ${DESTDIR}/etc/pcapdb.cfg
	shred /tmp/.rabbitmq_pass

SYSTEM_DIRS=${DESTDIR}/capture ${DESTDIR}/capture/index ${DESTDIR}/log ${DESTDIR}/static ${DESTDIR}/etc ${DESTDIR}/media

# Setup all the directories needed for the system to run.
setup_dirs: setup_user
ifneq "${DESTDIR}" "$(shell pwd)"
	mkdir -p ${DESTDIR}
	chown ${CAPTURE_USER}:${CAPTURE_GROUP} ${DESTDIR}
endif
	mkdir -p ${SYSTEM_DIRS}
	chown ${CAPTURE_USER}:${CAPTURE_GROUP} ${SYSTEM_DIRS}
	chmod g+s ${CAPTURE_GROUP} ${DESTDIR}/log

core: setup_dirs 
ifneq "${DESTDIR}" "$(shell pwd)"
	cp -R core ${DESTDIR}
	rm -f ${DESTDIR}/core/settings/settings.py
	chown ${CAPTURE_USER}:${CAPTURE_GROUP} -R ${DESTDIR}/core
	-ln -s ${DESTDIR}/core/settings/prod.py ${DESTDIR}/core/settings/settings.py
else
	-ln -s ${DESTDIR}/core/settings/devel.py ${DESTDIR}/core/settings/settings.py
endif
	if [ ! -e ${DESTDIR}/etc/pcapdb.cfg ]; then install -b ${INSTALL_PERMS} etc/pcapdb.cfg.example ${DESTDIR}/etc/pcapdb.cfg; fi

common-configs: ${DESTDIR}/etc/syslog.conf ${DESTDIR}/etc/logrotate.conf ${DESTDIR}/etc/sudoers ${DESTDIR}/etc/supervisord_common.conf
	-ln -s ${DESTDIR}/etc/syslog.conf ${RSYSLOGD}/pcapdb.conf
	service rsyslog restart
	-ln -s ${DESTDIR}/etc/logrotate.conf ${LOGROTATED}/pcapdb
	install -g root -o root -m 0440 ${DESTDIR}/etc/sudoers /etc/sudoers.d/pcapdb.sudoers
	# Tell supervisord to include our supervisord conf
	if ! grep -E "^files = ${DESTDIR}/etc/supervisord\*.conf" /etc/supervisord.conf; then \
		echo "[include]"								>> /etc/supervisord.conf; \
		echo "files = ${DESTDIR}/etc/supervisord*.conf"	>> /etc/supervisord.conf; \
	fi
	service supervisord restart

search-head-configs: ${DESTDIR}/etc/nginx.conf ${DESTDIR}/etc/supervisord_sh.conf
	-ln -s ${DESTDIR}/etc/nginx.conf ${NGINXD}/etc/nginx/conf.d/pcapdb.conf
	service nginx reload

capture-node-configs: ${DESTDIR}/etc/supervisord_cn.conf


# We just make this file in place, since it depends more on the make 
# variables than anything else.
${DESTDIR}/etc/syslog.conf:
	echo "# Pcapdb logs to local5 at all levels" > $@
	echo "local5.*	${DESTDIR}/etc/syslog.conf" >> $@

# Escape the forward slashes in the DESTDIR, so we can use it in a sed script
DESTDIR_ESCAPED=$(shell echo ${DESTDIR} | sed 's/\//\\\//g')
HOSTNAME=$(shell hostname -f)
${DESTDIR}/etc/nginx.conf: etc/nginx.conf.tmpl
	sed 's/DESTDIR/${DESTDIR_ESCAPED}/g;s/HOSTNAME/${HOSTNAME}/g' etc/nginx.conf.tmpl > $@

${DESTDIR}/etc/logrotate.conf:
	echo "${DESTDIR}/log/* {" > $@
	echo "  daily"		>> $@
	echo "  missingok"	>> $@
	echo "  compress"	>> $@
	echo "  rotate 7"	>> $@
	echo "}"			>> $@

# Most of the commands that need to be run as root are wrapped in shell
# scripts to severely limit their arguments. 
${DESTDIR}/etc/sudoers:
	echo "capture	ALL=NOPASSWD:${DESTDIR}/core/bin/sudo/*"		>  $@
	echo "# Note that the * in the arguments below is usually "		>> $@ 
	echo "# dangerous. We're relying on the fact that readlink "	>> $@
	echo "# takes only a single filename argument."					>> $@
	echo "capture	ALL=NOPASSWD:/bin/readlink -f /proc/[0-9]*/exe" >> $@
	echo "capture	ALL=NOPASSWD:/bin/umount"						>> $@

${DESTDIR}/etc/supervisord_common.conf: etc/supervisord_common.conf.tmpl
	sed 's/DESTDIR/${DESTDIR_ESCAPED}/g' etc/supervisord_common.conf.tmpl > $@	

${DESTDIR}/etc/supervisord_sh.conf: etc/supervisord_sh.conf.tmpl
	sed 's/DESTDIR/${DESTDIR_ESCAPED}/g' etc/supervisord_sh.conf.tmpl > $@	

${DESTDIR}/etc/supervisord_cn.conf: etc/supervisord_cn.conf.tmpl
	sed 's/DESTDIR/${DESTDIR_ESCAPED}/g' etc/supervisord_cn.conf.tmpl > $@	

${DESTDIR}/etc/uwsgi.ini: etc/uwsgi.ini.tmpl
	sed 's/DESTDIR/${DESTDIR_ESCAPED}/g' etc/uwsgi.ini.tmpl > $@	

ifeq "${DESTDIR}" "$(shell pwd)"
setup_user:
	# Do nothing when installing in place.
else
setup_user:
	# Create the capture user and group
	groupadd -f -r ${CAPTURE_GROUP}
	if ! id ${CAPTURE_USER} > /dev/null; then useradd -d ${DESTDIR} -c "PcapDB User" -g ${CAPTURE_GROUP} -M -r ${CAPTURE_USER}; fi
endif 

${DESTDIR}/lib/packages_installed: ${DESTDIR}/bin/python requirements.txt
	http_proxy=${http_proxy} export https_proxy=${http_proxy} ${PATH_EXPORT} ${DESTDIR}/bin/pip install -r requirements.txt 
	echo "Warning: if you have multiple postgres versions installed and/or psycopg2 fails to work,"
	echo "then it's possibly because the wrong postgres bin path was used."
	echo "Use pip to uninstall psycopg2, and reinstall with the correct path."
	touch ${DESTDIR}/lib/packages_installed

indexer:
	make -C indexer

indexer_install: setup_dirs
	make -C indexer install DESTDIR=${DESTDIR}

