from celery import shared_task
import socket
import time
import uuid

from django.conf import settings
from django.core.urlresolvers import reverse
from django.core.files import File

from apps.capture_node_api.models import ResultFile

__author__ = 'pflarr'

IF_SYS_PATH = '/sys/class/net/'

MAX_INPUT_ARGUMENTS = 500


@shared_task(bind=True)
def debug_task(self):
    print('Request: {0!r}'.format(self.request))


@shared_task(bind=True)
def sleepy_task(self, timeout):
    start_timeout = timeout
    print('Sleepy time for {}: {}.'.format(self.request.id, timeout))
    while timeout > 0:
        progress = 100*(start_timeout - timeout)/start_timeout
        self.update_state(state='WORKING',
                          # This becomes the 'result' value in the TaskMeta table,
                          # not the 'meta' value like one might guess.
                          meta={'progress': progress})
        print('updated state')
        time.sleep(min(timeout, 1))
        timeout -= 1
    print('Good morning {}.'.format(self.request.id))

    return {'msg': 'Slept for {} seconds.'.format(start_timeout)}


@shared_task(bind=True)
def splayed_task(self, message):

    print("Splaying message {}".format(message))

    result = ResultFile(type='S_PCAP',
                        task=self.request.id)
    # Make a random file name.
    result.file.name = str(uuid.uuid4())

    with open(result.file.path, 'w') as result_file:
        f = File(result_file)
        f.write('{} got message: {}'.format(settings.UI_HOST, message))
        f.close()
    result.save()

    path = reverse('capture_node:result', kwargs={'file_id': result.id})
    file_url = 'http://{}:{}{}'.format(settings.UI_HOST, settings.HTTP_PORT, path)

    return {'link': file_url, 'msg': 'ok'}
