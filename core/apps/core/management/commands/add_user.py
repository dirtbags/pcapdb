from django.core.management.base import BaseCommand, CommandError
from django.conf import settings
from django.core.management import call_command

from apps.search_head_api.views.users import UserAddView

__author__ = 'pflarr'

class Command(BaseCommand):
    help = 'Makes migrations for and migrates each installed app and all databases.'

    def __init__(self, *args, **kwargs):

        super().__init__(*args, **kwargs)

        self.no_color = kwargs.get('no_color', False)

    def handle(self, *args, **options):
        if len(args) != 4:
            print("Usage: create_admin <username> <first_name> <last_name> <email>")

        username, first_name, last_name, email = args

        ser = UserAddView.PostSerializer(data={'username': username,
                                               'email': email,
                                               'first_name': first_name,
                                               'last_name': last_name,
                                               'user_type': UserExtraModel.BASIC,
                                               'timezone': 'UTC'})
        if not ser.is_valid():
            print("Invalid attributes: {}".format(ser.errors))
 
        # See if this user already exists.
        if User.objects.filter(username=username).exists():
            print ('User {} already exists.'.format(username))

        user = User.objects.create_user(username, email=ser.validated_data['email'],
                                        first_name=ser.validated_data['first_name'],
                                        last_name=ser.validated_data['last_name'])
        user.extra = UserExtraModel(type=user_type, timezone=ser.validated_data['timezone'])

        confirm = UserConfirmation.make_confirm(user)
        info.append(confirm.send_confirmation(request))

        user.save()
        user.extra.save()

        print("New user '{}' created")
