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

from apps.capture_node_api.lib.search import parse
from apps.capture_node_api.models import ResultFile
from apps.capture_node_api.models.capture import Index

import iso8601

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'

IF_SYS_PATH = '/sys/class/net/'


@shared_task(bind=True)
def search_node(self, search_txt, start, end, proto, result_url, packets=False):
    """Perform a search across the given time-span.
    :param self: This is run as a method.
    :param str search_txt: The search to perform.
    :param str start_dt: The start timestamp as an ISO8601 UTC string
    :param str end_dt: The end timestamp as an ISO8601 UTC string
    :param int proto: The transport protocol ('all', 'tcp', 'udp')
    :param str result_url: The url to use to PUT the result on the search head.
    :param bool packets: If true, return packets rather than a flow result.
    :return: A dict containing a 'file_id' and 'msg'. The file_id is the id
             of the result file we sent to the search head.
    :rtype: dict
    """

    # Parse the search to make sure it is sane.
    tree = parse.parse(search_txt)
    tree = tree.normalize()
    tree.prune()

    start_dt = iso8601.parse_date(start)
    end_dt = iso8601.parse_date(end)

    # Get all of the indexes that match our time range.
    indexes = Index.objects.filter(start_ts__lt=end_dt,
                                   end_ts__gt=start_dt,
                                   ready=True).order_by('id')

    if not indexes:
        return {'msg': 'No data to search in time-span ({}).'.format(settings.UI_HOST)}

    # Create our search description and save it to a temp file.
    search_descr = tempfile.NamedTemporaryFile('w', delete=False)
    search_descr.write(tree.make_search_description(indexes, start_dt, end_dt, proto))
    search_descr.close()

    cmd_path = settings.SITE_ROOT/'bin'
    search_cmd = [cmd_path/'search', search_descr.name]

    if settings.SITE_ROOT != '/var/pcapdb':
        search_cmd.extend(['-p', settings.SITE_ROOT])

    if packets:
        # Fetch the packets, not the flows.
        search_cmd.append('-P')

    index_count = len(indexes)
    progress_denom = index_count*1.1

    log.info("Running search command: {}".format(' '.join(search_cmd)))
    proc = sp.Popen(search_cmd, stderr=sp.PIPE, stdout=sp.PIPE, stdin=sp.PIPE)
    while proc.poll() is None:
        try:
            stdout, stderr = proc.communicate(None, 1)
        except sp.TimeoutExpired:
            continue

        updates = stdout.split()
        if updates and updates[-1].endswith(b'.'):
            # Update our progress based on the index completion data from the
            # search process.
            try:
                indexes_done = int(updates[-1][:-1])
            except ValueError:
                # Bad update data
                continue

            self.update_state(state="WORKING", meta={'progress': indexes_done/progress_denom})

    if proc.poll() != 0:
        raise RuntimeError("Search command failed with: {}".format(proc.poll()))

    final_result = os.path.join('/tmp', str(uuid.uuid4()))

    full_result_fn, partial_result_fn = tree.filtered_output_filenames(start_dt, end_dt, proto)
    merge_idxs = list(indexes)
    temp_files = []
    finished_files = []
    while merge_idxs or temp_files:
        batch = merge_idxs[:settings.MAX_SEARCH_BATCH]
        merge_idxs = merge_idxs[settings.MAX_SEARCH_BATCH:]

        if merge_idxs:
            output_fn = os.path.join('/tmp', str(uuid.uuid4()))
            # The merge command takes absolute paths or index id's
            temp_files.append(output_fn)
        else:
            output_fn = final_result

        if packets:
            merge_cmd = [settings.MERGECAP_PATH, '-F', 'libpcap', '-w', output_fn]
        else:
            merge_cmd = [cmd_path/'merge', output_fn,
                         '-r', full_result_fn]

        if packets:
            name_fmt = '{}.pcap'
        else:
            name_fmt = '{}.flows'
        for idx in batch:
            if idx.start_ts < start_dt or idx.end_ts > end_dt:
                if not packets:
                    merge_cmd.append('-f')
                merge_cmd.append(os.path.join(idx.path, name_fmt.format(partial_result_fn)))
            elif packets:
                merge_cmd.append(os.path.join(idx.path, name_fmt.format(full_result_fn)))
            else:
                merge_cmd.append(str(idx.id))

        if not batch or len(batch) + len(temp_files) < settings.MAX_SEARCH_BATCH:
            for tmp in temp_files:
                if not packets:
                    merge_cmd.append('-f')
                merge_cmd.append(tmp)
                finished_files.append(tmp)
            temp_files = []

        log.info("Running search merge command: {}".format(' '.join(merge_cmd)))
        proc = sp.Popen(merge_cmd, stdout=sp.PIPE, stdin=sp.PIPE, stderr=sp.PIPE)
        # Wait for the process to finish.
        stdout, stderr = proc.communicate()
        if proc.poll() != 0:
            raise RuntimeError("Command failed with: {}".format(proc.poll()))

    # Delete the search description
    os.unlink(search_descr.name)

    # Clean up our temporary files.
    for fn in finished_files:
        os.unlink(fn)


    res = requests.put(result_url, open(final_result, 'rb'),
                       headers={'Content-Disposition': 'attachment; filename=data',
                                'Content-Type': 'application/octet-stream'},
                       # TODO: This should be True. Disabled for demo environment for now.
                       verify=False,
                       # Disable any proxies we might have configured.
                       proxies={'https': None, 'http': None})
    res.close()

    os.unlink(final_result)

    return {}


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

    # TODO: This is going to be replaced entirely.
    path = reverse('capture_node:result', kwargs={'file_id': result.id})
    file_url = 'http://{}:{}{}'.format(settings.UI_HOST, settings.HTTP_PORT, path)

    return {'link': file_url, 'msg': 'ok'}
