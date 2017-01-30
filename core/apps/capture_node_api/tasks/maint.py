from datetime import datetime
import os
import requests
import socket
import subprocess as sp
import tempfile
import time
import uuid

from celery import shared_task
from django.conf import settings
from django.core.files import File
from django.core.urlresolvers import reverse

from apps.capture_node_api.models.capture import Index
from apps.stats_api.models import Stats

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'


@shared_task(bind=True)
def clean_indexes(self):

    # Delete all the indexes that we've already marked as expired.
    for idx in Index.objects.filter(expired=True):
        # This deletes both the index entry and all of its files.
        idx.delete()

    # Delete any stats that predate any currently existing indexes
    oldest_index = Index.objects.order_by('start_ts').first()
    if oldest_index is not None:
        stats = Stats.objects.filter(capture_node__hostname=settings.NODE_NAME,
                                     minute__lt=oldest_index.start_ts)
        stats.delete()

    # Figure out how many indexes we'll need to expire next time.
    try:
        stat = os.statvfs(settings.INDEX_PATH)
    except FileNotFoundError:
        log.critical("Could not stat the index filesystem. It will eventually "
                     "run out of space.")
        raise

    # The limit on how full we'll let the index disk get.
    free_limit = settings.INDEX_DISK_RESERVED*stat.f_blocks*stat.f_bsize
    free = stat.f_bfree*stat.f_bsize

    indexes = Index.objects.order_by("start_ts")
    expirable = list(indexes.filter(capture_slot=None))

    while free < free_limit and expirable:
        idx = expirable.pop()
        idx.expired = True
        size = idx.size()
        idx.save()

        # Note that what we needed to free was in rough blocks, while
        # the size method returns the exact file size total. This will
        # result in more being freed than necessary due to file slack space.
        free += size

    # Note that we may not have enough indexes to delete to free things up for next time.
    # This should only ever be a problem if there's insufficient index space
    # relative to capture disk.

    return {}
