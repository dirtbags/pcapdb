import datetime

from django.utils import timezone
from rest_framework.response import Response
from rest_framework.views import APIView

from apps.task_api.models import TaskTrack
from apps.task_api.serializers import TaskTrackSerializer

__author__ = 'pflarr'


class TaskTrackDTView(APIView):
    def get(self, request):
        """Get all of the tasks for the current user.
        :param request:
        :return:
        """

        user = request.user

        tasks = TaskTrack.objects.filter(user=user)

        stasks = TaskTrackSerializer(tasks, many=True).data
        for t in stasks:
            print(t)

        return Response(data={'data': stasks})


class MyTasks(APIView):
    # The limit to how long we'll show a task.
    _OLDEST = datetime.timedelta(days=7)

    def get(self, request):
        """
        :param rest_framework.request.Request request:
        :return rest_framework.response.Response:
        """

        user = request.user
        if not request.user.is_authenticated():
            return Response(data={}, status=200)

        oldest = timezone.now() - self._OLDEST

        user_tasks = TaskTrack.objects.filter(user=user, cleared=False, started__gt=oldest)
        user_tasks = user_tasks.order_by('-started')
        user_tasks = user_tasks.prefetch_related()

        mytasks = [TaskTrackSerializer(task).data for task in user_tasks]

        return Response(data=mytasks, status=200)

    def post(self, request):
        """
        :param Request request:
        :return Response:
        """

        task_ids = request.data.getlist('task')
        if not task_ids:
            return Response(data={}, status=404)

        tasks = TaskTrack.objects.filter(user=request.user, task_id__in=task_ids)

        for task in tasks:
            task.cleared = True
            task.save()

        return Response(data={}, status=200)
