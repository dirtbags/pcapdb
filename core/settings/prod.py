# Import everything from the common config so we can override bits of it.
from .common import *
import os
import socket
import uuid

__author__ = 'pflarr'


if IS_SEARCH_HEAD:
    SECRET_KEY = config.get('pcapdb', 'session_secret')
else:
    # We need to set this for the capture nodes, but it doesn't need to actually be
    # consistent
    SECRET_KEY = str(uuid.uuid4())

os.environ['HTTPS'] = 'on'
os.environ['wsgi.url_scheme'] = 'https'
SESSION_COOKIE_SECURE = True
CSRF_COOKIE_SECURE = True

DEBUG = False

CAPTURE_USER = 'capture'
CAPTURE_GROUP = 'capture'

