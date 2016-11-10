# Overview #
PcapDB is a distributed, search-optimized open source packet capture system. It was designed to
replace expensive, commercial appliances with off-the-shelf hardware and a free, easy to manage 
software system. Captured packets are reorganized during capture by flow (an indefinite length
sequence of packets with the same src/dst ips/ports and transport proto), indexed by flow, and
searched (again) by flow. The indexes for the captured packets are relatively tiny (typically less
than 1% the size of the captured data). 

For hardware requirements, see HARDWARE.md.

## Architectural Overview ##
A PcapDB installation consists of a Search Head and one or more Capture Nodes. The Search Head can
also be a Capture Node, or it can be a VM somewhere else. Wherever it is, the Search Head must be
accessible by the Capture Nodes, but there's no need for the Capture Nodes to be visible to the 
Search Head.

# Requirements #
PcapDB is designed to work on Linux servers only. It was developed on both Redhat Enterprise and Debian systems, but its primary testbed has so far been Redhat based.

`sys_requirements.txt` contains a list of the packages required to run and build pcapdb.

requirements.txt contains python/pip requirements. They will be installed via 'make install'.

# Installing #
To build and install everything in /var/pcapdb/, run one of:
```
make install-search-head
make install-capture-node
make install-monolithic
```
 - Like with most Makefiles, you can set the DESTDIR environment variable to specify where to
   install the system. `make install-search-head DESTDIR=/var/mypcaplocation`
 - This includes installing in place: `make install-capture-node DESTDIR=$(pwd)`. In this case, PcapDB 
   won't install system scripts for various needed components, and it will run as the installing user.

To make your life easier, however, you should work make sure the indexing code builds cleanly by running 'make' in the 'indexer/' directory.

Postgresql may install in a strange location, as noted in the 'indexer/README'. This can cause build
failures in certain pip installed packages. Add `PATH=$PATH:<pgsql_bin_path>` to the end of your
'make install' command to fix this. For me, it is: `make install PATH=$PATH:/usr/pgsql-9.4/bin`.

# Setup #
After running 'make install', there are a few more steps to perform. 

'sudo make rabbitmq' will setup rabbitmq for use with pcapdb, create a password for the pcapdb account, and automatically set that password in the the pcapdb config file.

## DESTDIR/etc/pcapdb.cfg ##
This is the main Pcapdb config file. You must set certain values before PcapDB will run at all.

## Database Setup ##
Both capture nodes and the search head require a PostgreSQL server instance.
 - Capture Nodes expect a database named `capture_node`.
 - The search head expects to have a database named `pcapdb`.
 - Currently (and this is likely to change soon) PcapDB expects all databases to be owned by the
   same role. The username/password for that role need to be set in pcapdb.cfg.
 - Permissions in postgresql's `pg_hba.conf` file will need to be set to allow that user to log in.
   All nodes should be able to log into the search head's database. The capture node databases should
   only be accessible through localhost (via MD5/password) and unix socket (ident). The capture
   program uses the unix socket method to connect by default.
 - To populate the databases, you'll need to run:
   - DESTDIR/bin/python core/manage.py makemigrations
   - DESTDIR/bin/python core/manage.py migrate

## Web server setup ## 
The search head uses nginx and uwsgi to serve interface. A standard install of nginx is all that's
required, the PcapDB nginx config is installed automatically.
 - The PcapDB nginx config expects you to have a server certificate and key file for SSL:
   - /etc/ssl/HOSTNAME.pem
   - /etc/ssl/HOSTNAME.key

## Firewall Notes ## 
 - The Capture Nodes don't open any incoming ports for PcapDB, all communication is out to the Search
Head.
 - Using IPtables or other system firewalls on the Capture Nodes is discouraged. Instead
   put them on a normally inaccessible network.
 - The Search Head needs to be accessible by the Capture Nodes on ports 443, 5432 (postgres), and
   25672 (rabbitmq). 
   - It's ok to us IP tables on the search head (this should be automatically set up)

# Running the system # 
If you installed anywhere except 'in place', the system should attempt to run itself via
supervisord.
 - The `core/runserver` and `core/runcelery` scripts will be helpful when not running the system in
   production.
 - To run capture, use the capture_runner.py script: 
   `DESTDIR/bin/python DESTDIR/core/bin/capture_runner.py`
