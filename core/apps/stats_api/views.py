
from collections import defaultdict
from _datetime import datetime, timedelta
import pytz
from django.db.models import Sum
from rest_framework import serializers
from rest_framework.response import Response

from apps.search_head_api.models import CaptureNode, Site
from apps.search_head_api.views import SearchHeadAPIView
from apps.stats_api.models import Stats, StatsInterface
from libs.view_helpers import format_errors

import logging
log = logging.getLogger(__name__)

__author__ = 'shannon'


class StatsByGroup(SearchHeadAPIView):

    class GetSerializer(serializers.Serializer):
        site = serializers.IntegerField(required=False)
        capture_node = serializers.IntegerField(required=False)
        interface = serializers.CharField(max_length=200, required=False)
        start = serializers.DateTimeField(required=False)
        end = serializers.DateTimeField(required=False)

        def validate_capture_node(self, cn):
            if cn is not None:
                try:
                    CaptureNode.objects.get(id=cn)
                except CaptureNode.DoesNotExist:
                    raise serializers.ValidationError("No such capture node id: {}".format(cn))

    def get(self, request):
        """Fetch stats for the given time period.
        :param request:
        :return:
        """

        log.info("stats start: %s", request.data.get('start'))

        # Validate GET variables
        ser = self.GetSerializer(data=request.GET)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        log.error('request: {}'.format(request.GET))

        site_id = ser.validated_data.get('site', None)
        capnode_id = ser.validated_data.get('capture_node', None)
        iface_id = ser.validated_data.get('interface', None)
        end = ser.validated_data.get('end', pytz.UTC.localize(datetime.utcnow()))
        start = ser.validated_data.get('start', end - timedelta(hours=24))

        query = Stats.objects.filter(minute__gt=start, minute__lt=end)

        # The fields/values we include will determine the 'group by' (how the results are
        # aggregated).
        values = ['minute']
        title = []
        if site_id is not None:
            site = Site.objects.get(id=site_id)
            query = query.filter(capture_node__site_id=site_id)
            values.append('capture_node__site_id')
            title.append(site.name)

        if capnode_id is not None:
            try:
                capnode = CaptureNode.objects.get(id=capnode_id)
                query = query.filter(capture_node_id=capnode_id)
                values.append('capture_node_id')
                title.append(capnode.hostname)
            except CaptureNode.DoesNotExist:
                log.error("Capture node does not exist. {}".format(capnode_id))

        if iface_id is not None:
            try:
                iface = StatsInterface.objects.get(id=iface_id)
                query = query.filter(interface_id=iface_id)
                values.append('interface_id')
                title.append(iface.name)
            except StatsInterface.DoesNotExist:
                log.error("Interface does not exist.  {}".format(iface_id))

        query = query.order_by('minute')

        query = query.values(*values).distinct()

        query = query.annotate(
                    capture_size=Sum('capture_size'),
                    received=Sum('received'),
                    dropped=Sum('dropped'),
                    ipv4=Sum('ipv4'),
                    ipv6=Sum('ipv6'),
                    network_other=Sum('network_other'),
                    tcp=Sum('tcp'),
                    udp=Sum('udp'),
                    transport_other=Sum('transport_other')
                )

        if not title:
            title = ['all capture nodes']

        title = 'Capture stats for ' + ', '.join(title)

        return Response(data={'data': {'chart_data': query,
                                       'title': title,
                                       'start': start,
                                       'end': end}})


class GroupsByStat(SearchHeadAPIView):
    class GetSerializer(serializers.Serializer):
        GROUPINGS = {'capture_node': 'Capture Node',
                     'site': 'Site'}

        STAT_TYPES = {'capture_size': 'Capture Size',
                      'received': 'Packets Received',
                      'dropped': 'Packets Dropped',
                      'ipv4': 'IPv4 Packets',
                      'ipv6': 'IPv6 Packets',
                      'network_other': 'Net Layer Other',
                      'tcp': 'TCP Packets',
                      'udp': 'UDP Packets',
                      'transport_other': 'Transport Layer Other'}
        grouping = serializers.ChoiceField(choices=sorted(GROUPINGS.items()),
                                           required=False,
                                           default='capture_node')
        stat_type = serializers.ChoiceField(choices=sorted(STAT_TYPES.items()),
                                            required=False,
                                            default='capture_size')
        start = serializers.DateTimeField(required=False)
        end = serializers.DateTimeField(required=False)

    def get(self, request):
        """Fetch stats for the given time period.
        :param request:
        :return:
        """

        log.info('req data: {}'.format(request.GET))

        # Validate GET variables
        ser = self.GetSerializer(data=request.GET)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        grouping = ser.validated_data.get('grouping')
        stat_type = ser.validated_data.get('stat_type')
        end = ser.validated_data.get('end', pytz.UTC.localize(datetime.utcnow()))
        start = ser.validated_data.get('start', end - timedelta(hours=24))

        log.error('end time: {}'.format(end))
        log.info('grouping {}, stat_type {}'.format(grouping, stat_type))

        query = Stats.objects.filter(minute__gt=start, minute__lt=end)

        values = ['minute']
        grouping_key = None
        if grouping == 'capture_node':
            grouping_key = 'capture_node__hostname'
            values.append(grouping_key)
        elif grouping == 'site':
            grouping_key = 'capture_node__site__name'
            values.append(grouping_key)
        else:
            raise RuntimeError("Invalid grouping: {}".format(grouping_key))

        values.append(stat_type)

        query = query.order_by('minute')

        query = query.values(*values).distinct()

        query = query.annotate(
            data=Sum(stat_type)
        )

        # There is a way, using the postgresql 'crosstab' function, to do this in postgresql
        # directly. It is very complicated, and not very dynamic. Instead we'll just build the
        # result set the hard way in python.

        # dict[minutes] -> dict[host/site name] -> count
        by_minute = defaultdict(lambda : {})
        for res in query:
            by_minute[res['minute']][res[grouping_key]] = res[stat_type]

        chart_data = []
        for minute in sorted(by_minute.keys()):
            data = {'minute': minute}
            data.update(by_minute[minute])
            chart_data.append(data)

        title = '{} by {}'.format(self.GetSerializer.STAT_TYPES[stat_type],
                                  self.GetSerializer.GROUPINGS[grouping])

        return Response(data={'data': {'chart_data': chart_data,
                                       'title': title,
                                       'start': start,
                                       'end': end}})
