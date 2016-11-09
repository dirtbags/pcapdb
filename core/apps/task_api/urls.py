from django.conf.urls import url

from apps.task_api.views import TaskTrackDTView, MyTasks

urlpatterns = [
    url(r'^dt_tasks$', TaskTrackDTView.as_view(), name='dt_tasks'),
    url(r'^mytasks$', MyTasks.as_view(), name='mytasks'),
]
