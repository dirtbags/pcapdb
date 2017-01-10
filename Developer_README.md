#### Project Layout 
 indexer - Is a cmake base project that contains all the portions of the project written in C
 core - All of the python/Django based code. 

PcapDB consists of multiple host nodes. There are two types of nodes:
  Capture Node - Capture, index, and save incoming packet streams.
  Search Head - Provides the interface for the capture system. 

A node can be both a capture node and the search head, but there can only be one search head.

#### Requirements 
PcapDB is designed to work on Linux servers only. It was developed on both Redhat Enterprise and Debian systems, but its primary testbed has so far been Redhat based.

sys_requirements.txt contains a list of the packages required to run and build pcapdb

requirements.txt contains python/pip requirements. They can will be installed via 
'make install'.

#### Deployment 
For development, you can simply work with pcapdb mostly in place. To set up its
directory structure, run 'make install-monolithic DESTDIR=$(pwd)'.

For actual deployment, simply run 'make install-monolithic' (See README).

#### Interface 
See [core/README.md](core/README.md) for more information about the organization and setup of the interface.
