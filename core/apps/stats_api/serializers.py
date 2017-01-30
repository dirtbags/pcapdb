from rest_framework import serializers
from apps.search_head_api.serializers import CaptureNodeSerializer

from . models import Stats


class StatsSerializer(serializers.ModelSerializer):
    class Meta:
        model = Stats
        fields = '__all__'

    capture_node = CaptureNodeSerializer()
    interface = serializers.CharField()
    minute = serializers.DateTimeField()
    capture_size = serializers.IntegerField()
    received = serializers.IntegerField()
    dropped = serializers.IntegerField()
    ipv4 = serializers.IntegerField()
    ipv6 = serializers.IntegerField()
    network_other = serializers.IntegerField()
    tcp = serializers.IntegerField()
    udp = serializers.IntegerField()
    transport_other = serializers.IntegerField()
