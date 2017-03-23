# This script watches for whether capture should be running and makes sure it's running when
# appropriate.
# Separating out this functionality into and independent script allows us to keep this script,
# and therefore capture, running with supervisord. It also makes it vastly easier to start the
# capture process as an daemon independent of the celery server

import os
import sys
import time

# This file sites in the project in core/bin/. We need to add
# the path to core/ to the python path
sys.path.append(os.path.dirname(os.path.dirname(__file__)))

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "settings.settings")
import django
django.setup()

from apps.capture_node_api.models.status import Status

import logging
log = logging.getLogger('capture_runner')

REFRESH_RATE = 1

os.umask(0o002)

def main():
    status = Status.load()
    while True:
        # Exits when killed
        # Only do this check every five seconds.
        time.sleep(REFRESH_RATE)
        
        try:
            status.refresh_from_db()
        except status.DoesNotExist:
            # We've never created a status object. Just sleep until we do.
            continue

        if status.capture == status.RESTART:
            # It doesn't matter if we're running or not, restart/start capture.
            log.info("Restarting capture.")
            status.start_capture()

        elif status.capture == status.STOPPED:
            if status.pid is not None:
                # Capture is still running somewhere.
                # Shut it down.
                # Shut it down forever.
                log.info("Shutting down capture.")
                status.stop_capture()

        elif status.capture == status.STARTED:
            cap_status = status.capture_status
            if cap_status[0] == status.NOT_OK:
                # Capture should be started, but isn't
                log.info("Capture was supposed to be running, but wasn't: {}".format(cap_status[1]))
                status.start_capture()
        else:
            log.error("Invalid capture mode: {}".format(status.capture))

if __name__ == '__main__':
    main()

