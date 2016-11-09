import os

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'settings.settings')
import django
django.setup()

from settings.celery import *
