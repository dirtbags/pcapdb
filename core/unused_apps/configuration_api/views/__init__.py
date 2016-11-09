from rest_framework.views import APIView
from rest_framework.response import Response

from libs.permissions import APIRequiredGroupsPermission

# Serializers
from ..serializers import UpdateConfigSerializer
from ..serializers import DeleteConfigSerializer
from ..serializers import NewConfigSerializer
# Models
from apps.search_head_gui.models.models import AppConfig, AppConfigHistory
#
import json


class BaseConfigAPIView(APIView):
    permission_classes = (APIRequiredGroupsPermission,)
    required_groups = {
        'ALL': ['cpatreportpriv'],      # replace or add your desired ADMIN LDAP auth group here
        }

class GetConfigurationHistory(BaseConfigAPIView):
    def get(self, request):
        data = []
        q = AppConfigHistory.objects.all().select_related('current_config').order_by('-timestamp')[:10]

        for history in q:
            data.append({'key': history.current_config.key,
                         'current_value': history.current_config.value,
                         'previous_value': history.old_value,
                         'timestamp': history.timestamp,
                         'current_modified_by': history.current_config.modified_by,
                         'previous_modified_by': history.modified_by})

        return Response(data)


class GetConfigurationData(BaseConfigAPIView):
    def get(self, request):
        data = []
        categories = [a.category for a in AppConfig.objects.all().distinct('category').only('category')]

        for cat in categories:
            vals = AppConfig.objects.filter(category=cat).values()
            if cat == '':
                cat = "uncategorized"
            data.append({cat: vals})

        return Response(data)


class UpdateConfig(BaseConfigAPIView):
    serializer_class = UpdateConfigSerializer

    def post(self, request):
        update_data = self.serializer_class(data=request.DATA)
        if not update_data.is_valid():
            return Response({'error': 'Invalid configuration update.'})
        config_item = AppConfig.objects.filter(key=update_data.data['key'])[:1]
        if not config_item.exists():
            return Response({'error': 'That configuration does not exist.'})

        config_item = config_item[0]
        # Set the history
        history = AppConfigHistory()
        history.current_config = config_item
        history.old_value = config_item.value
        history.modified_by = config_item.modified_by
        history.save()

        config_item.value = json.loads(update_data.data['value'])
        config_item.modified_by = request.user.username
        config_item.save()
        return Response({'status': 'OK'})


class AddConfig(BaseConfigAPIView):
    serializer_class = NewConfigSerializer

    def post(self, request):
        new_data = self.serializer_class(data=request.DATA)
        if not new_data.is_valid():
            return Response({'error': 'Invalid configuration.'})

        new_config = AppConfig(category=new_data.data['category'],
                               key=new_data.data['key'],
                               value=json.loads(new_data.data['value']),
                               modified_by=request.user.username)
        new_config.save()
        return Response({'status': 'OK'})


class DeleteConfig(BaseConfigAPIView):
    serializer_class = DeleteConfigSerializer

    def post(self, request):
        del_data = self.serializer_class(data=request.DATA)
        if not del_data.is_valid():
            return Response({'error': 'Invalid configuration.'})

        config_item = AppConfig.objects.filter(key=del_data.data['key'])[:1]
        if not config_item.exists():
            return Response({'error': 'That configuration does not exist.'})

        config_item = config_item[0]
        config_item.delete()
        return Response({'status': 'OK'})