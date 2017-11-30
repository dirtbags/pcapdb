Things that need fixin'
=======================

- `/var/pcapdb/etc/supervisord_common.conf` specifies a `capture` user that doesn't exist.

pfring
------

`/etc/init.d/pf_ring` wants to load a kernel module.
It calls dkms, which promptly explodes,
since debian doesn't have kernel headers for a coreos kernel.
We are going to have to figure this out.


Startup services
----------------

PcapDB expects systemd to start the following:

- nginx
- supervisord
- uwsgi
- rabbitmq
- postgres
