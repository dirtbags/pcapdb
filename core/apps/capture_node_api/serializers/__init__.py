from rest_framework import serializers
from apps.capture_node_api.models.capture import Disk, Index, Stats
from apps.capture_node_api.models.interfaces import Interface
from apps.capture_node_api.models.status import Status

class StatusSerializer(serializers.ModelSerializer):
    class Meta:
        model = Status
        fields = '__all__'

    is_capture_node = serializers.BooleanField()
    capture_status = serializers.ListField(
        child=serializers.CharField()
    )
    index_disk_status = serializers.ListField(
        child=serializers.CharField()
    )
    capture_disk_status = serializers.ListField(
        child=serializers.CharField()
    )
    CAPTURE_MODES = serializers.ListField(
        child=serializers.ListField(
            child=serializers.CharField()
        )
    )

class IndexSerializer(serializers.ModelSerializer):
    class Meta:
        model = Index
        fields = ('start_ts', 'end_ts', 'ready')


class StatsSerializer(serializers.ModelSerializer):
    class Meta:
        # class attributes
        model = Stats
        fields = '__all__'

    index = IndexSerializer()


class DiskSerializer(serializers.ModelSerializer):
    class Meta:
        model = Disk
        fields = '__all__'


class InterfaceSerializer(serializers.ModelSerializer):
    class Meta:
        model = Interface
        fields = '__all__'

    mac = serializers.CharField()
    addresses = serializers.DictField()
    dev_id = serializers.CharField()
    dev_port = serializers.CharField()
    duplex = serializers.CharField()
    flags = serializers.CharField()
    iflink = serializers.CharField()
    mtu = serializers.IntegerField()
    speed = serializers.IntegerField()
    speed_hr = serializers.CharField()

    rx_bytes = serializers.IntegerField()
    rx_packets = serializers.IntegerField()
    rx_dropped = serializers.IntegerField()
    rx_errors = serializers.IntegerField()
    tx_bytes = serializers.IntegerField()
    tx_packets = serializers.IntegerField()
    tx_dropped = serializers.IntegerField()
    tx_errors = serializers.IntegerField()

    queues = serializers.IntegerField()
    max_queues = serializers.IntegerField()
    driver = serializers.CharField()
