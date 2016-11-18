from django.conf import settings
from django.utils import timezone
from django.core.urlresolvers import reverse
from apps.capture_node_api.tasks.interface import gather_interface_stats
from celery import shared_task
from apps.search_head_api.models import CaptureNode
from apps.search_head_api.models.search import SearchInfo, SearchResult

import os
import shutil
import subprocess
import uuid

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'

@shared_task(bind=True)
def idx_interface_gather():
    """Tell each capture node to record its interface statistics."""
    for idx in CaptureNode.objects.all():
        gather_interface_stats.apply_async(queue=idx.hostname)


@shared_task(bind=True)
def search_merge(self, node_search_results, search_id):
    """Combine the result from multiple capture nodes for a flow search.
    :param [{}] node_search_results: The results from the nodes that tried to run the search.
    :param int search_id: The id of the search we need to merge results for.
    :rtype: dict
    """

    log.info('search id: {}, *args: {}'.format(search_id, node_search_results))

    try:
        search = SearchInfo.objects.get(id=search_id)
    except SearchInfo.DoesNotExist:
        raise ValueError("Search {} does not exist.", search_id)

    msgs = set([])
    ret = {}

    node_res_paths = []

    for node_search in search.node_results.all():
        if node_search.file.name == '':
            msgs.add('Search did not complete on capture node: {}'
                     .format(node_search.capture_node.hostname))
            msgs.add('Search Results are incomplete.')
        else:
            if node_search.file.size > 0:
                node_res_paths.append(node_search.file.path)

    if not node_res_paths:
        msgs.add("Search results were empty.")
    else:
        search_res = SearchResult(search=search)
        search_res.file.name = uuid.uuid4()

        if len(node_res_paths) == 1:
            # Don't try to merge a single file, just move it.
            try:
                # Try to just move the file.
                os.rename(node_res_paths[0], search_res.file.path)
            except OSError:
                # This happens if the src and dst aren't on the same filesystem.
                # We'll have to copy the file instead.
                shutil.copyfile(node_res_paths[0], search_res.file.path)
        else:
            if search.type == search.T_FLOW:
                # Do a flow merge if we're dealing with flows
                cmd = [settings.SITE_ROOT/'bin'/'merge',
                       search_res.path,
                       '-p', settings.SITE_ROOT]
                for node_res in node_res_paths:
                    cmd.extend(['-f', node_res])

            elif search.type == search.T_PCAP:
                # Do a mergecap if we're working in PCAP
                cmd = [settings.MERGECAP_PATH,
                       '-F', 'libpcap',  # Output plain pcap.
                       '-w', search_res.file.path]
                cmd.extend(node_res_paths)
            else:
                raise RuntimeError("")

            # Run the command (whatever it is...)
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
            proc.wait()

        result_url = reverse('search_head_api:result', kwargs={'search_id': search.id})
        search_res.save()

        if search.type == search.T_FLOW:
            msgs.add("Flow search successful")
            ret['link'] = reverse('search_head_gui:search',
                                  kwargs={'search_id': search.id})
            ret['raw_result'] = result_url
        else:
            msgs.add("Pcap fetch successful")
            ret['link'] = result_url

        # Delete all the intermediate results.
        for node_result in search.node_results.all():
            log.info("Result file: {}, {}".format(os.path.exists(node_result.file.path),
                                                  node_result.file.path))
            node_result.file.delete()
            node_result.delete()

    search.completed = timezone.datetime.now()
    search.save()

    ret['msg'] = '\n'.join(sorted(list(msgs)))
    return ret
