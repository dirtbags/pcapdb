#### Overview
This directory houses the python side of PcapDB. It is a
Django project that sets up the database used by the capture
system as well as the API and GUI portions of the interface.

##### Directory Structure

 apps/ - Django app modules. Each module has a README detailing what should go in them.
   apps/core - A module containing components shared by both capture nodes and the search head.
   apps/capture_node_api - The API module that runs only on capture nodes.
   apps/search_head_api - Similarly, the API module for the search head node.
   apps/search_head_gui - GUI frontend components for a search head node.

 bin/ - Scripts needed by interface code
 etc/ - Configuration file directory
 settings/ - The Django settings module.
 
 manage.py - The standard django manage.py script.
 runserver - A shortcut for running the test server on a port specified in the pcapdb config.

#### Configuration
Before you can run the PcapDB interface components, you must first configure it. An example configuration file is available in etc/.
