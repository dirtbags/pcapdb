from django.core.management.base import BaseCommand, CommandError
from django.conf import settings
from django.core.management import call_command

__author__ = 'pflarr'

class Command(BaseCommand):
    help = 'Makes migrations for and migrates each installed app and all databases.'

    def __init__(self, *args, **kwargs):

        super().__init__(*args, **kwargs)

        self.no_color = kwargs.get('no_color', False)

    def handle(self, *args, **options):
        app_labels = [app.split('.')[-1] for app in settings.INSTALLED_APPS]

        # Run the make_migrations command for every installed app.
        call_command('makemigrations', *app_labels)

        for db in settings.DATABASES:
            print("Migrating to database: {}".format(db))
            call_command('migrate', database=db)
