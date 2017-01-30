from django.contrib.auth.models import User
from djcelery.models import TaskMeta
from rest_framework import serializers

from apps.task_api.models import TaskTrack

__author__ = 'pflarr'


class ResultField(serializers.DictField):
    def to_representation(self, value):
        if issubclass(type(value), str):
            return {'msg': value}
        elif issubclass(type(value), dict):
            return value
        elif issubclass(type(value), BaseException):
            return {'msg': 'Task Error'}
        else:
            return {'wormy': 'squirmy'}


class TaskMetaSerializer(serializers.ModelSerializer):
    class Meta:
        model = TaskMeta
        fields = '__all__'

    meta = serializers.DictField()
    result = ResultField()
    date_done = serializers.DateTimeField(format="%Y-%m-%d %H:%M:%S")


class UserSerializer(serializers.ModelSerializer):
    class Meta:
        model = User
        fields = ('username', 'first_name', 'last_name')


class TaskTrackSerializer(serializers.ModelSerializer):
    class Meta:
        model = TaskTrack
        fields = '__all__'

    task_id = serializers.CharField()
    task = TaskMetaSerializer()
    user = UserSerializer()
    started = serializers.DateTimeField(format="%Y-%m-%d %H:%M:%S")
    status = serializers.ListField(child=serializers.CharField())
