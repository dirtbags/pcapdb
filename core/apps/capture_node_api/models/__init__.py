import socket

from django.core.files.storage import FileSystemStorage
from django.core.urlresolvers import reverse

from apps.capture_node_api.models.capture import Disk
from .capture import *
from .interfaces import *
from .status import *

storage_location = str(settings.SITE_ROOT/'media')
fs = FileSystemStorage(location=storage_location)


class ResultFile(models.Model):
    """Used to keep track of task results on disk."""

    TASK_RESULT_TYPES = {'S_FLOW': 'The results of a search within just the flow data.',
                         'S_PCAP': 'The results of a search that should return PCAP.'}
    type = models.CharField(max_length=max(map(len, TASK_RESULT_TYPES.keys())),
                            choices=TASK_RESULT_TYPES.items())
    file = models.FileField(storage=fs, null=True)
    task = models.CharField(max_length=255, unique=True)
    when = models.DateTimeField(auto_now_add=True)

    def uri(self):
        """Return an absolute uri to this file."""
        path = reverse('capture_node:result', kwargs={'file_id': self.id})
        file_url = 'http://{}:{}{}'.format(settings.UI_HOST, settings.HTTP_PORT, path)

        return file_url

