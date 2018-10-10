<img src="https://cloud.githubusercontent.com/assets/22897558/22356169/8f6e0b16-e3ec-11e6-8695-2273424c2b06.png" width="200" />

# Overview

PcapDB is a distributed, search-optimized open source packet capture system. It was designed to
replace expensive, commercial appliances with off-the-shelf hardware and a free, easy to manage
software system. Captured packets are reorganized during capture by flow (an indefinite length
sequence of packets with the same src/dst ips/ports and transport proto), indexed by flow, and
searched (again) by flow. The indexes for the captured packets are relatively tiny (typically less
than 1% the size of the captured data).

For hardware requirements, see [HARDWARE.md](HARDWARE.md).

[Also see our fact sheet](FactSheet_PcapDBv2.pdf)

## DESTDIR
Many things in this file refer to DESTDIR as a pathname prefix. The default, and that used by future pcapdb packages, is `/var/pcapdb`.

## Architectural Overview
A PcapDB installation consists of a Search Head and one or more Capture Nodes. The Search Head can
also be a Capture Node, or it can be a VM somewhere else. Wherever it is, the Search Head must be
accessible by the Capture Nodes, but there's no need for the Capture Nodes to be visible to the
Search Head.

# 1. Requirements
PcapDB is designed to work on Linux servers only. It was developed on both Redhat Enterprise and
Debian systems, but its primary testbed has so far been Redhat based. While it has been verified to
work (with packages from non-default repositories) on RHEL 6, a more bleeding edge system (like
RHEL/Centos 7, or the latest Debian/Ubuntu LTS) will greatly simplify the process of gathering dependencies.

[sys_requirements.md](sys_requirements.md) contains a list of the packages required to run and build
pcapdb. They are easiest to install on modern Debian based machines.

requirements.txt contains python/pip requirements. They will automatically be installed via 'make install'.

# 2. Installing
To build and install everything in /var/pcapdb/, run one of:
```
make install-search-head
make install-capture-node
make install-monolithic
```
 - Like with most Makefiles, you can set the DESTDIR environment variable to specify where to
   install the system. `make install-search-head DESTDIR=/var/mypcaplocation`
 - This includes installing in place: `make install-capture-node DESTDIR=$(pwd)`. In this case, PcapDB
   won't install system scripts for various needed components. You will have to run it manually, see
   below.
 - If you're behind a proxy, you'll need to specify a proxy connection string using PROXY=host:port
	as part of the make command.
 - There's a bug in some 1.10.* versions of virtualenv that cause the install to fail. Specify the python3 virtualenv executable with using VIRTUALENV=<virtualenv path>

To make your life easier, however, you should work make sure the indexing code builds cleanly by running 'make' in the 'indexer/' directory.

Postgresql may install in a strange location, as noted in the 'indexer/README'. This can cause build
failures in certain pip installed packages. Add `PATH=$PATH:<pgsql_bin_path>` to the end of your
'make install' command to fix this. For me, it is: `make install PATH=$PATH:/usr/pgsql-9.4/bin`.

# 3. Setup
After running 'make install', there are a few more steps to perform.

## 3-0: Setup hugepages (optional)
Pcapdb uses 2MB hugepages to manage memory more efficiently. If a capture node has hugepages available,
they will be automatically consumed by the capture process. 

First, determine how much memory you want to devote to capture. I'd recommend about 70% of available
system memory, which should be a minimum of 16G (64G or more are recommended). Then simply divide
that amount by 2M to get the number of hugepages. 

To enable hugepages, add 'hugepages=<number of pages> hugepagesz=2M' to your /boot/grub/grub.conf
and reboot. You may also want to add it to /etc/default/grub in the GRUB_CMDLINE_LINUX variable,
depending on your OS flavor (debian). 

## 3-1: Post-Install script
The core/bin/post-install.sh script will handle the vast majority of the system setup for you.
 - Only run once. It can cause issues if you run it multiple times. ~~It does so idempotently, so it can be run multiple times without breaking anything.~~ If you run it again, you may have to re-install completely. Moving the old PcapDB install files to another directory can cause problems as well.
 - Run without arguments to get the usage information.
  - Basically, you want to give it arguments based on whether you're setting up a search head (-s),
    a capture node (-c), or monolithic install (-c -s).
  - You'll also have to give it the search head's IP.
```
/var/pcapdb/core/bin/post-install.sh [-c] [-s] <search_head_ip>    
```

This will set up the databases and rabbitmq.

## 3-2 DESTDIR/etc/pcapdb.cfg
This is the main Pcapdb config file. You must set certain values before PcapDB will run at all.
There are a few things you need to set in here manually:
 - __(On capture nodes) The search head db password__
 - __(On capture nodes) The rabbitmq password__
   - Both of the above should be in the search head's pcapdb.cfg file.
 - __(On search head) The local mailserver.__
   - If you don't have one, I'd start with installing Postfix. It even has selectable install
     settings that will configure it as a local mailserver for you.

## 3-3 Add an admin user (Search Head Only)
You'll need to create an admin user.
```
sudo su - capture
./bin/python core/manage.py add_user <username> <first_name> <last_name> <email>
```
 - Usernames must be at least four characters long.
 - This will email you a link to use to set that user's password.
  - (This is why email had to be set up).
  - root@localhost is a reasonable email address, if you need it.
  - *Note that manage.py also has a __createsuperuser__ command, which shouldn't be used.*

## 3-4 Add the capture nodes to the postgres pg\_hba.conf on the search head. 
 This is needed if running a separate search head. See below.

## 3-5 Set up a site. 
 - You should be able to login with your admin account. 
 - Click 'Admin', 'Capture Sites'. 
 - Add a new capture site. The group name can be the same as the site name; the admin group should be different. 
 - This adds a grouping of capture nodes to work with.

## 3-6 Set up a capture node.
 - Click 'Admin', 'Capture Nodes'. 
 - Select your site, and add a new capture by hostname. 
 - The you have to do this even in monolithic mode. 
 - If this fails, check logs/celery.log on the capture node. 
 - The capture node must already be able to connect to rabbitmq/celery on the search head for this to work.

## 3-7 Add capture node permissions
 - To be able to configure the capture node, you must set permissions for it. 
 - Click 'Admin', 'Users'. 
 - Select your user, and 'Add to Group'. Select the admin group for the site you added. 
 - You can now edit the disks and configure capture. 

## 3-8 Configure disks.
Go into the Disks view - Click 'Admin', 'Capture Nodes', and then the 'Disks' button on the node you
want to configure.

### Index disks
 - You'll need one or two equally sized disks dedicated to indexing. Select the disks from the available 'Devices'
   table, and click 'Create Index RAID'. It will take a few minutes.
 - If you choose two disks, it will create a RAID 1 of the two disks.

### Capture disks
 You'll need to set up some groups of disks for capture. 

 - Select some number of equally sized disks from the 'Devices' list, and click 'Create RAID5'. This
   will create a new md device from those disks. (If your disk is an external RAID, you can skip
   this step.)
 - Select your RAID, and click 'Initialize as Capture Disk'. This will format the RAID and add it to
   the database. It should appear in the 'Capture Disks' table at the top of the page. 
 - Repeat these steps to include as many capture disks as you like. PcapDB balances across them
   according to their size, so they don't all have to end up as the same size. 
 - You can also add dynamically re-assigned spares that will be used by any of your RAID's as needed, by
   clicking a disk and selecting 'Add Spare'. 
 - For each of the Capture Disks you added, select and enable them in the 'Capture Disks' table.

Debugging note: Errors from this all go into the logs/celery.log on the capture node. 

## 3-9 Configure Capture
Go into the Capture view - Click 'Admin', 'Capture Nodes', and then the 'Capture' button on the node you
want to configure.

 - Select the interface you'd like to enable for capture, and click the red circle. Capture will be
   enabled on this device on the next capture restart. 
 - On capture settings, you can enable PFRing ZC mode, if you have a license and a compatible
   network interface. All used interfaces must be compatible. 
 - The multi-queue mode has been tested, but not well. The queue slider is only used when in
   PFring-ZC mode.
 - There's a bug in the capture settings; you must put a number (including zero) in the local memory
   box. 

When you're ready, click 'Start'. 

Debugging note: Capture runner errors go into logs/django.log on the capture node. For some reason you may get error messages about the capture runner not trying to do anything. As root, on the capture node, run supervisorctl restart capture_runner. Logs for the actual capture process are supposed to be in /var/pcapdb/log/capture.log, but sometimes show up in /var/log/messages if syslog didn't get configured correctly.



### Things that can, and have, gone wrong
 - If your host doesn't have a host name in DNS, you can set an IP in the 'search\_head\_host' variable
   in the pcapdb.cfg file.

## 3-5 pfring-zc drivers
One more thing. You should install the drivers specific to your capture card for pfring-zc. The
packages from NTOP actually build the drivers for your kernel on the fly when installed, though
you may have to reinstall that package whenever you do a kernel update.  Building and installing
from source is also fairly straightforward.

# Using PcapDB
Now that the system is installed and running, you have to set up a capture site, capture node, and some users.

## Create a Capture Site
Every capture node belongs to a capture site.
 - Each capture site has it's own group, only users in that group can search the site.
 - They also have an admin group. Users must be a member of this to admin (setup disk on, or start capture on) capture nodes in that site.
 - These can be LDAP groups.

## Create a Capture Node
Add a capture node to your site.
 - If the search head is also a capture node, it will have to be added to.
 - If the buttons for disk and capture configuration aren't active, your user isn't an admin for the relevant site.

## Setup disks
PcapDB needs three types of disk:
 - An OS disk, which is barely used.
 - An index disk or disks. It's generally recommended to have two identical disks for this to set up a RAID 1.
 - A bunch of disk for capture storage.

 You've obviously already got an OS at this point. The other two can be any sort of hard disk, SSD, partition, or RAID. As long as the system thinks it's a block device, you should be fine.

#### Note
*The system is pretty dumb about what disks it considers to be block devices. Any /dev/sd or /dev/md device works, for now*

### Add an Index Disk
In the disk management interface, select one or two identical disks to act as the index disk. If more than one disk is selected, they will automatically be grouped together in a RAID 1 configuration.
 - Click, 'create index disk' and the system will RAID, format, and name the disks appropriately.

### Add a Capture Disk
In the disk management interface, you can build RAID 5 arrays, and then assign those (or individual disks) as capture disks.
 - While you can add all your disks individually as capture disks, it's recommended to RAID them for a bit of data safety.  RAID's of 9 disks are fairly reasonable.
 - Like with the index disk, select groups of disks, and click 'Create RAID 5'. Once that's done, you can add the
   resulting disk as a capture disk.
 - Or you can add individual disks (or external RAIDs) as capture disks.
 - Once a capture disk is added, it must be activated above before it can be captured to.
 - You can also set disks as __spares__, which are shared across all RAIDS created by PcapDB.
 - If RAIDS are ever degraded, they should be put in REPAIR mode automatically. They'll still be available to search, but won't be written to until they're fixed.

## Set a capture interface, and go.
In the capture interface, enable the interface or interfaces of your choice.
 - Each will get a separate thread (which will in turn be dedicated to it's own processor. So you shouldn't try to capture on more interfaces than half your CPU's).
 - PFring mode is far less likely to drop packets than libpcap mode.
 - PFringZC mode is far less likely to drop packets than PFring mode, but requires a license from NTOP.

# Details on the various subsystems
PcapDB uses quite a few off-the-shelf open source systems, and it's useful to understand how those
pieces fit into the larger system. What follows is a detailed description of those systems, and how
to set them up manually.

## RabbitMQ
RabbitMQ is a fast and efficient messaging system used to communicate simple messages between a
distributed network of hosts. As with Celery, RabbitMQ is really meant for distributing messages to
the 'first available' worker, but in PcapDB all of our messages are to a specific worker. As such,
the PcapDB rabbitMQ instance automatically creates a specific message queue for each Capture Node,
as well as a queue for the Search Head. The command 'rabbitmqctl' gives visibility into the
currently active queues. For further debugging/introspection, the rabbitmq admin plugin provides a
web interface that can be quite useful.

RabbitMQ server need only be configured on the search head.
```
# Get rid of the guest user
rabbitmqctl delete_user guest
# Create the pcapdb user with a password
rabbitmqctl add_user pcapdb <a strong password>
# Allow the pcapdb user to set/view all queues
rabbitmqctl set_permissions -p / pcapdb '.*' '.*' '.*'
```

## Database Setup
The setup varies significantly between the search head and capture nodes.

### Add the 'capture' Role
On all pcapdb servers, add a 'capture' role with login privileges and a password. As the postgres user:
```
sudo su -postgres
createuser capture -l -P
```

The 'db\_pass' variable in the pcapdb.cfg file should be set to the Search Head's db password on all
pcapdb hosts in the network.

### On the Search Head
Create a database named "pcapdb":
```
createdb -O capture pcapdb
```

Edit the Search Head's "pg\_hba.conf" (location varies) file to allow connections to Search Head DB from localhost. Also add a line allowing each capture node.
```
host    pcapdb          capture         127.0.0.1/32            md5
host    pcapdb          capture         <capture node ip>       md5
```

Edit the Search Head's postgresql.conf file so that it listens on it's own IP:
```
listen_addresses = 'localhost,<search head ip>'
```

### On the Capture Nodes
Create a database named 'capture\_node' on each capture node host:
```
createdb -O capture capture_node
```

Since the capture nodes connect via peer (unix socket) to their own database, no additional setup
should be needed.


### Restart the postgresql server.
```
# On most systems...
service postgresql restart
```

### Install the Database Tables
After restarting the postgres service, we'll need to install the database tables on each
PcapDB host. From the PcapDB installation directory (typically /var/pcapdb):
```
sudo su - root
cd /var/pcapdb
./bin/python core/manage.py makemigrations stats_api login_api core task_api search_head_api
login_gui search_head_gui capture_node_api

# On the search head
./bin/python core/manage.py migrate

# On the capture nodes
./bin/python core/manage.py migrate --database=capture_node
```

## Web server setup
The Makefile will generate a self-signed cert for your server for you if an installed one doesn't
already exist at '/etc/ssl/<HOSTNAME>.pem'. You should probably change that to something that isn't
self-signed.

## Firewall Notes
 - The Capture Nodes don't open any incoming ports for PcapDB, all communication is out to the Search Head.
 - Using IPtables or other system firewalls on the Capture Nodes is discouraged. Instead
   put them on a normally inaccessible network. They shouldn't generally have any ports open other than ssh.
 - The Search Head needs to be accessible by the Capture Nodes on ports 443, 5432 (postgres), and
   25672 (rabbitmq).
   - It's ok to us IP tables on the Search Head (and will eventually be automatic)

## Running the system
If you installed anywhere except 'in place', the system should attempt to run itself via
supervisord. __You'll have to restart some processes, as supervisord will have given up on them.__
 - The `supervisorctl` command can give you the status of the various components of the system. Capture has to be started manually from within the interface, so you shouldn't expect it to be running initially.
 - The `capture_runner` process, however, should be running. From within supervisorctl,

 - The `core/runserver` and `core/runcelery` scripts will be helpful when not running the system in
   production.
 - Similarly, to run capture outside of production, use the capture_runner.py script:
   `DESTDIR/bin/python DESTDIR/core/bin/capture_runner.py`
   - DESTDIR/log/django.log will tell you the exact command used to start capture, if for some reason it's failing to start.
   - DESTDIR/log/capture.log will usually give you some idea why capture is failing to run. If this file doesn't exist, either capture has never successfully ran at all, or rsyslog isn't forwarding the logs to the right place.


### PostgreSQL
While PcapDB is a database of packets, it uses postgres to take care of more mundane database tasks.
The Search Head has a unique database that houses information on users, capture nodes, celery
response data, and aggregate statistics for the entire network of PcapDB capture nodes. All PcapDB
hosts must be able to connect to the Search Head database.

Each Capture Node also has a database that keeps track of the available disk chunks and indexes on
that node. This database is only accessible to the capture node itself.

This has to be set up manually. See above for more information.

### Celery
Celery is a system for distributing and scheduling tasks across a network of workers. PcapDB manages
all of it's communications with the Capture Nodes through Celery tasks, from initiating searches to
managing disk arrays. The tasks are assigned and picked up by the appropriate host via RabbitMQ
messaging queues, and the responses are saved to the search head via the search head's database.
Celery runs on both the Search Head and all Capture Nodes, though each host subscribes to different
task queues (see RabbitMQ below).

Celery is configured automatically on system install, and the process is managed via supervisord.


### uWSGI and Nginx
The web interface for PcapDB is built in the Python Django system, which is served via a unix
socket using uWSGI and persistant Python instances. Nginx handles all of the standard HTTP/HTTPS
portions of the web service, and passes Django requests to uWSGI via it's socket. (This is a pretty
standard way of doing things).

 - uWSGI and Nginx are automatically configured on install.
 - Nginx is managed as a standard system service.
 - uWSGI is managed via supervisord

#### Certificates
The standard configuration for PcapDB and Nginx expects ssl certificates and a private key installed
at:
```
/etc/ssl/<HOSTNAME>.pem
/etc/ssl/<HOSTNAME>.prv
```

If these don't already exist when running make install, self-signed certs will be created
automatically (with your input).


# A Few Other Tasks

## Install static files
You will need to install the static files for pcapdb.

```
sudo su - capture
./bin/python core/manage.py collectstatic
```
# Making sure everything is working.
After installing and setting up the database, there are a few things you can check to make sure
everything is working.

 1. Restart supervisord to reset the uwsgi and celery processes to pick up the new database configs.

 2. Go to your webserver's root directory https://<myserver>/, and you should get to the pcapdb
 login page.
