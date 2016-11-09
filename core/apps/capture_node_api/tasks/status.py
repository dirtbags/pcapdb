from celery import shared_task
import time

from apps.capture_node_api.models.status import Status
from apps.capture_node_api.serializers import StatusSerializer

import logging
log = logging.getLogger(__name__)


@shared_task(bind=True)
def get_status(self):
    data = StatusSerializer(Status.load()).data
    log.error('status: {}'.format(data))

    return {'data': {'state': data }}


CAPTURE_STATES = ['start', 'restart', 'stop']


@shared_task(bind=True)
def set_capture_state(self, state):
    TIMEOUT = 2

    status = Status.load()
    before_time = status.capture_state_changed

    # Starting capture will restart it if it is already running.
    if state == 'start':
        status.capture = status.STARTED
    elif state == 'restart':
        status.capture = status.RESTART
    elif state == 'stop':
        status.capture = status.STOPPED
    else:
        log.info('Unknown state value: {}'.format(state))
        return {'danger': 'Unknown state: {}'.format(state)}

    status.save()

    timeout = time.time() + TIMEOUT

    while status.capture_state_changed == before_time:
        if time.time() > timeout:
            return {'danger': 'No attempt to start/stop/restart capture detected. Capture runner '
                              'may not be active.'}
        # Wait a bit
        time.sleep(0.5)
        status.refresh_from_db()

    return {'info': status.running_message}


@shared_task(bind=True)
def set_capture_settings(self, settings):

    status = Status.load()

    mode = settings.get('capture_mode')

    modes = [m[0] for m in status.CAPTURE_MODES]

    if mode not in modes:
        log.info('Invalid capture mode: {}'.format(mode))
        return {'warning': 'Invalid capture mode: {}'.format(mode)}

    status.capture_mode = mode
    status.settings_changed = True
    status.save()

    return {'info': 'Updated capture mode.'}