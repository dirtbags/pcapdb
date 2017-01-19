from django.core.management.base import BaseCommand, CommandError
from django.conf import settings
from django.core.management import call_command

from django.contrib.auth.models import User
from apps.search_head_api.views.users import UserAddView
from apps.search_head_api.models.auth import UserExtraModel

__author__ = 'pflarr'

class Command(BaseCommand):
    help = 'Makes migrations for and migrates each installed app and all databases.'

    def __init__(self, *args, **kwargs):

        super().__init__(*args, **kwargs)

        self.no_color = kwargs.get('no_color', False)

    def add_arguments(self, parser):
        parser.add_argument('username')
        parser.add_argument('first_name')
        parser.add_argument('last_name')
        parser.add_argument('email')

    def handle(self, *args, **options):
        options['user_type'] = UserExtraModel.BASIC
        options['timezone'] = 'UTC'

        ser = UserAddView.PostSerializer(data=options)
        if not ser.is_valid():
            print("Invalid arguments: {}".format(ser.errors))
 
        # See if this user already exists.
        if User.objects.filter(username=ser.validated_data['username']).exists():
            print ('User {} already exists.'.format(username))

        user = User.objects.create_user(username=ser.validated_data['username'],
                                        email=ser.validated_data['email'],
                                        first_name=ser.validated_data['first_name'],
                                        last_name=ser.validated_data['last_name'])
        user.extra = UserExtraModel(type=ser.validated_data['user_type'],
                                    timezone=ser.validated_data['timezone'])
                        
        confirm = UserConfirmation.make_confirm(user)
        info.append(confirm.send_confirmation(request))

        user.save()
        user.extra.save()

        print("New user '{}' created")
