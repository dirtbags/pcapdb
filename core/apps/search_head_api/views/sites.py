# Rest Framework imports
from rest_framework.response import Response
from rest_framework import serializers

from django.db.utils import IntegrityError
from django.db import transaction

from apps.search_head_api.views.base import SearchHeadAPIView
from apps.search_head_api.models.sites import Site
from apps.search_head_api import serializers as api_serializers

from libs.view_helpers import format_errors

import logging
log = logging.getLogger(__name__)

class SiteListView(SearchHeadAPIView):
    def get(self, request):
        ser = api_serializers.SiteSerializer(Site.objects.all(), many=True)
        log.error('{}'.format(ser.data))
        return Response(data={'data': ser.data})


class SiteAddView(SearchHeadAPIView):

    PostSerializer = api_serializers.SiteSerializer

    def post(self, request):

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        try:
            ser.save()
        except IntegrityError as err:
            return Response({'warning': 'Site {} already exists.'.format(err)})

        return Response({'success': 'Site {} created.'.format(ser.data['name'])})


class SiteDeleteView(SearchHeadAPIView):

    class PostSerializer(serializers.Serializer):
        name = serializers.CharField(max_length=15)

    @transaction.atomic
    def post(self, request):

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': ['{}'.format(request.data), format_errors(
                ser.errors)]})

        try:
            site = Site.objects.get(name=ser.data['name'])
            # We need to delete both the associated groups and the site itself.
            site.group.delete()
            site.admin_group.delete()
            site.delete()
        except Site.DoesNotExist:
            return Response({'warning': 'No such site to delete: {}'.format(ser.data['name'])})

        return Response(data={'success': 'Site deleted.'})
