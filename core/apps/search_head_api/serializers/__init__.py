import logging
import socket

import re
from celery.exceptions import TimeoutError
from django.contrib.auth.models import User, Group
from django.db import transaction
from rest_framework import serializers

from apps.capture_node_api.tasks.status import get_status
from apps.search_head_api.models.auth import UserExtraModel, GroupTypeModel
from apps.search_head_api.models.sites import Site, CaptureNode
log = logging.getLogger(__name__)

USER_GROUP_NAME_RE = r'^[a-z0-9_-]{4,16}$'

class GroupSerializer(serializers.ModelSerializer):
    class Meta:
        model = Group
        fields = '__all__'

    name = serializers.RegexField(USER_GROUP_NAME_RE)

class UserExtraSerializer(serializers.ModelSerializer):
    class Meta:
        model = UserExtraModel
        fields = '__all__'


class UserSerializer(serializers.ModelSerializer):
    class Meta:
        model = User

        fields = ['id',
                  'groups',
                  'extra',
                  'username',
                  'first_name',
                  'last_name',
                  'email',
                  'last_login',
                  'date_joined']

    groups = GroupSerializer(many=True)
    extra = UserExtraSerializer()


class CaptureNodeSerializer(serializers.ModelSerializer):
    class Meta:
        model = CaptureNode
        fields = '__all__'

    site = serializers.SlugRelatedField(slug_field='name', queryset=Site.objects.all())
    addr = serializers.CharField(read_only=True)
    disks_uri = serializers.CharField(read_only=True)
    ifaces_uri = serializers.CharField(read_only=True)

    TIMEOUT = 3

    def validate_hostname(self, hostname):
        """Normalize the hostname to an FQDN."""

        try:
            # Normalize the hostname
            hostname = socket.getfqdn(hostname)
        except socket.gaierror:
            raise serializers.ValidationError("No such host: {}".format(hostname))

        # The normalized hostname should replace whatever the user gave us.
        return hostname

    def validate(self, data):
        """Make sure the indexer exists and connected via celery."""



        task = get_status.apply_async(queue=data['hostname'])
        try:
            task_data = task.get(timeout=self.TIMEOUT).get('data')
        except TimeoutError:
            raise serializers.ValidationError("Capture Node not connected.")

        if not (hasattr(task_data, 'get') and 'state' in task_data):
            raise serializers.ValidationError("Capture Node not responding correctly.")

        return data

    def create(self, val_data):
        node = CaptureNode(site=val_data['site'], hostname=val_data['hostname'])

        node.save()

        return node


class SiteSerializer(serializers.Serializer):

    name = serializers.SlugField(max_length=15)
    group = serializers.RegexField(USER_GROUP_NAME_RE)
    admin_group = serializers.RegexField(USER_GROUP_NAME_RE)
    capture_nodes = CaptureNodeSerializer(many=True)
    # capture_nodes added below

    def group_validate(self, group):
        group = group.strip()

        if re.match(USER_GROUP_NAME_RE, group) is None:
            raise serializers.ValidationError("Group Names must be alphanumeric.")

        if Group.objects.filter(name=group).exists():
            raise serializers.ValidationError("Group {} already exists.".format(group))

        return group

    admin_group_validate = group_validate

    @transaction.atomic
    def create(self, val_data):
        """Create a new site from validated data."""

        group = Group(name=val_data['group'])
        group.save()
        group_type = GroupTypeModel(group=group, type=GroupTypeModel.SITE)
        group_type.save()

        admin_group = Group(name=val_data['admin_group'])
        admin_group.save()
        adm_group_type = GroupTypeModel(group=admin_group, type=GroupTypeModel.SITE_ADMIN)
        adm_group_type.save()

        return Site.objects.create(name=val_data['name'], group=group, admin_group=admin_group)

    addr = serializers.CharField(read_only=True)
    disks_uri = serializers.CharField(read_only=True)
    ifaces_uri = serializers.CharField(read_only=True)

