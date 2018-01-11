This will build a docker image for you.

I build it like so:

    docker build --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --tag=pcapdb -f Dockerfile ../

That last argument is the path to the top-level source directory,
which,
if you are in this directory when you run docker,
is the parent directory.


Running It
----------

This assumes you want to bind-mount `/srv/pcapdb`

    PCAPDB_BASE=/srv/pcapdb
    PCAPDB_HOSTNAME=$(hostname -f)
    PCAPDB_MAILHOST=mail.example.com
    docker run -d --name pcapdb --network=host -e PCAPDB_HOSTNAME=$PCAPDB_HOSTNAME -e PCAPDB_MAILHOST=$PCAPDB_MAILHOST -v $PCAPDB_BASE/etc:/var/pcapdb/etc -v $PCAPDB_BASE/postgresql:/var/lib/postgresql/data pcapdb

It will start up on port 443 of the host network interface.

Next you will need to create your administrative account:

    email=you@example.com
    docker exec pcapdb sudo -u capture bin/python core/manage.py add_user admin Adam Inistrator $email

Click on the link in your email to set a password and finish account creation.
The account name will be `admin`.


Setup
-----

You can now follow the [main README](../README.md),
picking up at the section entitled "Using PcapDB".

When you set up a capture node,
you will need to know the hostname of the container.
For some reason, `localhost` is not accepted.
To get the container's hostname:

    docker exec pcapdb hostname


TODO
----

* Do we need `--network=host` to capture packets? If not, remove, so the ops person can decide how they want to deploy.
* Add to "running it" how to bind-mount whatever needs bind-mounting
* Does docker run need a capability?
