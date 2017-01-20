# Rest Framework imports
from rest_framework.response import Response
from rest_framework.views import APIView
from rest_framework.renderers import JSONRenderer, BrowsableAPIRenderer
from rest_framework import serializers
from rest_framework.permissions import AllowAny

from apps.search_head_api.views.base import SearchHeadAPIView
from apps.search_head_api.models.auth import UserConfirmation, UserExtraModel

from django.contrib.auth.models import User, Group
from django.conf import settings
from django.http import Http404

from apps.search_head_api.serializers import UserSerializer

from libs.view_helpers import format_errors

import datetime
import pytz
import random
import time
import zxcvbn

import logging
log = logging.getLogger(__name__)

USER_GROUP_NAME_RE = r'^[a-z0-9_-]{4,16}$'


class UserListView(SearchHeadAPIView):
    def get(self,request):
        return Response(data={'data': UserSerializer(User.objects.all(), many=True).data})


class UserAddView(SearchHeadAPIView):

    class PostSerializer(serializers.Serializer):
        username = serializers.RegexField(USER_GROUP_NAME_RE)
        email = serializers.EmailField()
        first_name = serializers.CharField(min_length=1, max_length=20, required=False)
        last_name = serializers.CharField(min_length=1, max_length=20, required=False)
        user_type = serializers.ChoiceField([t.id for t in UserExtraModel.USER_TYPES])
        timezone = serializers.ChoiceField(pytz.common_timezones)

        def validate(self, data):
            if data['user_type'] != UserExtraModel.LDAP and \
                    ('first_name' not in data or 'last_name' not in data):
                raise serializers.ValidationError("First and Last name required for non-LDAP "
                                                  "users.")
            return data

    def post(self, request):
        """Add a new user. The user will have a random password set, and will be emailed an email
        confirmation/password change request.
        Post Data:
          - username: Username of new user
          - email: Email of new user
          - first_name: First name of new user.
          - last_name: Last name of new user.
          - user_type: The type of user to create.
        """

        info = []

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        username = ser.validated_data['username']
        user_type = ser.validated_data['user_type']

        # See if this user already exists.
        if User.objects.filter(username=username).exists():
            return Response(data={'warning': 'User already exists.'})

        if user_type == 'LDAP' and settings.LDAP_GROUPS_ENABLED:
            return Response(data={'warning': 'With LDAP groups enabled, LDAP users are created on '
                                  'login, and their permissions are based on LDAP groups.'})

        user = User.objects.create_user(username, email=ser.validated_data['email'],
                                        first_name=ser.validated_data['first_name'],
                                        last_name=ser.validated_data['last_name'])
        user.extra = UserExtraModel(type=user_type, timezone=ser.validated_data['timezone'])

        if user_type == UserExtraModel.BASIC:
            # Generate a new confirmation object with a random token.
            confirm = UserConfirmation.make_confirm(user)
            info.append(confirm.send_confirmation(request))

        if user_type == 'LDAP':
            # Automatically enroll LDAP users in our local LDAP required group. This provides the
            # 'authorization' needed to identify LDAP users who should be able to log in.
            try:
                ldap_base_group = Group.objects.get(name=settings.LDAP_REQUIRED_GROUP)
            except Group.DoesNotExist:
                # There's a pretty unlikely race condition here where the group is created between
                # where we checked for one and when we save a new one. Eh.
                ldap_base_group = Group(name=settings.LDAP_REQUIRED_GROUP)
                ldap_base_group.save()

            user.groups.add(ldap_base_group)

        user.save()
        user.extra.save()

        return Response({'success': 'User "{}" Created.'.format(username)})


class UserDeleteView(SearchHeadAPIView):

    class PostSerializer(serializers.Serializer):
        username = serializers.RegexField(USER_GROUP_NAME_RE)

    def post(self, request):

        log.debug('keys: {}'.format(request.data.keys()))

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        try:
            user = User.objects.get(username=ser.validated_data['username'])
        except User.DoesNotExist:
            return Response(data={'warning': 'No such user.'})

        warnings = []
        try:
            if user.extra.type == 'LDAP' and settings.LDAP_GROUPS_ENABLED:
                warnings.append('The delete user will be able to log in as long as they are '
                                'in the {} LDAP group.'.format(settings.LDAP_REQUIRED_GROUP))
        except UserExtraModel.DoesNotExist:
            log.error("User object does not have a row in UserExtra. This shouldn't happen.")

        user.delete()

        return Response(data={'warning': warnings,
                              'info': 'User {} deleted.'.format(user.username)})


class UserAddGroupView(SearchHeadAPIView):

    class PostSerializer(serializers.Serializer):
        usernames = serializers.ListField(child=serializers.RegexField(USER_GROUP_NAME_RE),
                                          allow_empty=False)
        group = serializers.RegexField(USER_GROUP_NAME_RE)

    def post(self, request):
        log.info("add group: {}".format(request.data))

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        users = User.objects.filter(username__in=ser.validated_data['usernames'])
        if not users:
            return Response(data={'warning': 'No such users.'})

        try:
            group = Group.objects.get(name=ser.validated_data['group'])
        except Group.DoesNotExist:
            return Response(data={'warning': 'No such group.'})


        info = []
        warning = []
        for user in users:
            if user.extra.type == 'LDAP' and settings.LDAP_GROUPS_ENABLED:
                warning.append('{} uses external LDAP groups.'.format(user.name))
            else:
                user.groups.add(group)
                user.save()
                info.append('{} assigned to group {}'.format(user.username, group.name))

        return Response(data={'info': [], 'warning': warning})


class UserRemoveGroupView(SearchHeadAPIView):
    def post(self, request):
        # Re-using the serializer from group adding.
        ser = UserAddGroupView.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        users = User.objects.filter(username__in=ser.validated_data['usernames'])
        if not users:
            return Response(data={'warning': 'No such users.'})

        try:
            group = Group.objects.get(name=ser.validated_data['group'])
        except Group.DoesNotExist:
            return Response(data={'warning': 'No such group.'})

        for user in users:
            user.groups.remove(group)
            user.save()

        return Response(data={'info': 'Users removed from group {}.'.format(group.name)})


class UserPasswordResetView(APIView):
    """View for resetting a user's password. Note that this view does not require authentication."""

    permission_classes = (AllowAny,)

    class PostSerializer(serializers.Serializer):
        username = serializers.RegexField(USER_GROUP_NAME_RE)
        email = serializers.EmailField()

    def post(self, request):
        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        # Muck with timing attacks.
        time.sleep(random.random()*UserConfirmation.DELAY_BASE)

        try:
            user = User.objects.get(username=ser.validated_data['username'])
            if user.email != ser.validated_data['email']:
                raise User.DoesNotExist
        except User.DoesNotExist:
            return Response(data={'warning': 'Invalid User.'.format()})

        if user.extra.type != user.extra.BASIC:
            return Response(data={'warning': 'You can only reset passwords for basic auth users.'})

        user.password = None

        try:
            confirm = UserConfirmation.objects.get(user=user)
            now = datetime.datetime.now()
            now = pytz.UTC.localize(now)
            confirm.created = now
            confirm.save()
        except UserConfirmation.DoesNotExist:
            confirm = UserConfirmation.make_confirm(user)

        confirm.send_confirmation(request)

        return Response(data={'info': 'User {} password reset and email sent.'
                                      .format(user.username)})


class UserPasswordSetView(APIView):
    permission_classes = (AllowAny,)
    renderer_classes = (JSONRenderer, BrowsableAPIRenderer)

    class PostSerializer(serializers.Serializer):
        password = serializers.RegexField(settings.PASSWORD_RE)
        password2 = serializers.RegexField(settings.PASSWORD_RE)

        def validate(self, data):
            """Make sure the passwords match and are reasonably strong."""

            password = data['password']
            password2 = data['password2']
            if password != password2:
                raise serializers.ValidationError("Passwords do not match.")

            pw_results = zxcvbn.password_strength(password)
            strength = pw_results['score']

            if strength < settings.PASSWORD_STRENGTH_MIN:
                raise serializers.ValidationError("Insufficient password strength. Scored {}/4. "
                                                  "Estimated time to crack: {}"
                                                  .format(strength,
                                                          pw_results['crack_time_display']))

            return data

    def post(self, request, token):

        age_limit = pytz.UTC.localize(datetime.datetime.now()) - UserConfirmation.AGE_LIMIT

        # Grab the confirmation object for this view, if it exists.
        confirm = UserConfirmation.objects.filter(token=token,
                                                  created__gt=age_limit)

        # Sleep randomly to screw with timing attacks.
        time.sleep(random.random()*UserConfirmation.DELAY_BASE)

        if not confirm:
            # No such valid token
            raise Http404

        confirm = confirm.first()

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        confirm.user.set_password(ser.validated_data['password'])
        confirm.user.save()

        confirm.delete()

        return Response(data={'success': 'Password Updated'})
