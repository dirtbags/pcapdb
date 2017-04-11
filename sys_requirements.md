# System package requirements
These requirements include those packages that are needed beyond those available on 
a general purpose Redhat EL (RHEL) 7 or Ubuntu 16.04 install. Requirements for other distributions may vary.

__The version numbers listed are the versions that the system was tested on.__

# The mostly short version

## On Debian/Ubuntu
On all nodes:
 - `apt install -y postgresql postgresql-server-dev-all tshark python3 python3-pip python3-dev virtualenv uwsgi-plugin-python3 libsasl2-dev libldap2-dev supervisor dkms`

On the search head:
 - `apt install -y rabbitmq-server nginx`

On the capture nodes:
 - `apt install -y librabbitmq4 ethtool`

If you're building from source:
 - `apt install -y cmake libnuma-dev libssl-dev libhugetlbfs-dev libpcap-dev`

You'll also need the pfring packages from http://packages.ntop.org
 - x86\_64/PF\_RING/pfring 
 - all/pfring-dkms 

# Detailed General Requirements
## PostGres
Postgres provides package servers for each of their versions most major linux distributions.
 - postgresql-server >= 9.2 (Earlier versions may work)
 - postgresql-server-devel  (For building the python postgresql packages.

## TShark
You'll need tshark (or wireshark), specifically the mergecap command. We'll probably, eventually, write our own to drop this dependency.
 - tshark or wireshark package.

## Python3
Python forms the basis of the interface and task management code.
 - python3.4 or greater
 - python3-devel
 - python3-pip
 - python3-virtualenv

*You'll need to following in the above python version
Note that the Makefile assumes that you'll have a python3 executable somewhere on your path, 
  and most packages make a softlink from python3 to python3.x by default. Some don't.
The softlink is usually at /usr/bin/python3*

### Some python libs
Needed by the python ldap package

#### (On Debian/Ubuntu)
 - libsasl2-dev
 - libldap2-dev

#### (On Redhat)
 - openldap-devel

## Redhat
  __ius.io is a good source for python3 RPMs__

### Needed on Debian based systems
We'll be using uwsgi to server the interface, but we'll install it through pip.
 - uwsgi-plugin-python3

## mlocate
The makefile uses mlocate to check where certain things are. 
 - mlocate (Installed by default on most system, but not universally)

## supervisor
Python's supervisord provides a system agnostic way to ensure processes are always running.
I should probably replace this with systemd.

# Search Head Node Only Requirements

## RabbitMQ
Used for managing task messages between the capture nodes and search head.
__On search head node only__
 - rabbitmq-server>=3.1 

### Redhat
 - rabbitmq is available via EPEL.

## nginx
We use nginx to server the interface.

# Capture Node only requirements
We need to talk to the rabbitmq server, and probe network interfaces.
 - librabbitmq
 - ethtool

## PFRING
For pfring, ntop.org provides RPM's and DEB's at http://packages.ntop.org/
 - pfring  >= 6.2
 - pfring-dkms (also provided via ntop)
 - dkms (available via apt)

# Build Requirements
These are only required when building from source.
 - cmake >= 2.8
 - gcc
 - gcc-c++  # The project doesn't contain any C++, but cmake insists
 - numactl-devel
 - openssl-devel
 - libhugetlbfs-devel
 - libpcap-devel

## postgresql-dev
This is covered above, but there may be hiccups in building the indexer code if things
aren't installed in exactly the expected place. See the note in indexer/README-building

# Building PF-ring ZC Drivers
As mentioned, you can get pfring packages straight from ntop via their mirrors for certain systems/kernels. This is generally preferable for the main components of the package. To use PFRING in ZC mode, which is greatly preferred, you'll need drivers specific to your network card and kernel.

To build the drivers you'll need the following:
bison 
flex 
kernel-headers
kernel-devel

Within the pfring source, run make and make install in:
 - `drivers/ZC/intel/<your_card's_driver>`

You should also ensure your card's driver isn't overwritten by another package automatically. RHEL has `kmod-<driver>` packages that should be removed, for instance.
