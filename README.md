#### Requirements ####
PcapDB is designed to work on Linux servers only. It was developed on both Redhat Enterprise and Debian systems, but its primary testbed has so far been Redhat based.

'sys_requirements.txt' contains a list of the packages required to run and build pcapdb.

requirements.txt contains python/pip requirements. They will be installed via 'make install'.

#### Installing ####
Running 'make install' should build and install everything in /var/capture/. 

To make your life easier, however, you should work make sure the indexing code builds cleanly by running 'make' in the 'indexer/' directory.

Postgresql may install in a strange location, as noted in the 'indexer/README'. This can cause build failures in certain pip installed packages. Add 'PATH=$PATH:<pgsql_bin_path>' to the end of your 'make install' command to fix this. For me, it is: 'make install PATH=$PATH:/usr/pgsql-9.4/bin'.

#### Setup ####
After running 'make install', there are a few more steps to perform. 

'sudo make rabbitmq' will setup rabbitmq for use with pcapdb, create a password for the pcapdb account, and automatically set that password in the the pcapdb config file.

You will also need to create the appropriate postgres databases and roles. 
 - All nodes will expect a database role with SELECT/INSERT/UPDATE/DELETE rights on all the tables in the following databases. The role will need a password set. 
 - Permissions in postgresql's pg_hba.conf file will need to be set to allow that user to log in. All nodes should be able to log into the search head`s database. The indexer databases should only be accessible through localhost (via MD5/password) and unix socket (ident). The indexer C code uses the unix socket method to connect by default.
 - TODO: Currently, the username and password must be the same for all nodes in the system. This 
         should obviously change in the future. 
 - Indexer nodes will expect a database named indexer. 
 - The search head will expect a database named pcapdb
 - Once the databases are created, you`ll need to run '../bin/python manage.py migrate_all' from core/ to create all the relevant tables in each database.

