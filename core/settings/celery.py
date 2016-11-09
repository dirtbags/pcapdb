import os

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'settings.settings')
import django
from django.conf import settings

from celery import Celery

__author__ = 'pflarr'

app = Celery()

# Configure celery logging
app.config_from_object('django.conf:settings')
app.autodiscover_tasks(lambda: settings.INSTALLED_APPS)
