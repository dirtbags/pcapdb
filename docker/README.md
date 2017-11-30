This will build a docker image for you.

I run it like so:

    docker build --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --tag=pcapdb -f Dockerfile ../

That last argument is the path to the top-level source directory,
which,
if you are in this directory when you run docker,
is the parent directory.
