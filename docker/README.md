This will build a docker image for you.

I build it like so:

    docker build --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --tag=pcapdb -f Dockerfile ../

That last argument is the path to the top-level source directory,
which,
if you are in this directory when you run docker,
is the parent directory.


Running It
----------

    docker run --rm -it --network=host -e PCAPDB_HOSTNAME=$(hostname -f) pcapdb

It will start up on port 443 of the host network interface.

TODO
----

* Add to "running it" how to bind-mount whatever needs bind-mounting
* Does docker run need a capability?
