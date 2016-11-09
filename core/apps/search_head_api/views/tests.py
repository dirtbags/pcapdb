from apps.task_api.models import TaskTrack
from celery import chord
from rest_framework.response import Response
from rest_framework.views import APIView

from apps.capture_node_api.tasks.test import sleepy_task
from apps.search_head_api.models import CaptureNode

__author__ = 'pflarr'


class SleepyTest(APIView):
    def post(self, request):

        timeout = int(request.data['sleepy_timeout'])

        capture_nodes = [capnode.hostname for capnode in CaptureNode.objects.all()]
        if not capture_nodes:
            raise ValueError("No capture_nodes exist.")
        capnode = capture_nodes[0]
        result = sleepy_task.apply_async([timeout], queue=capnode)
        TaskTrack.track(result, "Sleeping for a bit on {}.".format(capnode), request)

        return Response({'info': 'Sleepy task started as task {}'.format(result.task_id)})



class DistStatusTest(APIView):
    def post(self, request):
        timeout = int(request.data['timeout'])

        capture_nodes = [capnode.hostname for capnode in CaptureNode.objects.all()]

        if len(capture_nodes) == 0:
            raise ValueError("No capture_nodes on which to splay")

        # Start each of the splayed tasks,
        task_group = chord((sleepy_task.subtask([timeout], options={'queue': capnode})
                            for capnode in capture_nodes),
                           sleepy_task.subtask([timeout], immutable=True,
                                               options={'queue': 'search_head'}))

        result = task_group.apply_async(queue='search_head')

        TaskTrack.track(result, "Distributing sleep for {}".format(timeout), request)

        return Response({'info', 'Started dist. sleepy task.'})
