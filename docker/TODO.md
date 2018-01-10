Things that need fixin'
=======================

- Are we going to need cron?
- Registration emails come from docker container hostname, how can we override that with $PCAPDB_HOSTNAME?


pfring
------

`/etc/init.d/pf_ring` wants to load a kernel module.
It calls dkms, which promptly explodes,
since debian doesn't have kernel headers for a coreos kernel.
We are going to have to figure this out.

Mount Points
------------

The following directories are thins you would want to persist across deploys

- `/var/lib/postgres`
-
