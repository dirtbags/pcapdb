__author__ = 'pflarr'

# Import everything from the common config so we can override bits of it.
from .common import *
import os

SECRET_KEY = 'volcanic_tuft'

INSTALLED_APPS.append('debug_toolbar')
DEBUG_TOOLBAR_PATCH_SETTINGS = False
MIDDLEWARE_CLASSES = ('debug_toolbar.middleware.DebugToolbarMiddleware',) + MIDDLEWARE_CLASSES
INTERNAL_IPS = ''

DEBUG = True

CAPTURE_USER = os.getlogin()
CAPTURE_GROUP = 'users'

# Should be in application configuration.
EMAIL_HOST = ''
LDAP_PORT = 389
LDAP_SERVER = ''
LDAP_SSL_PORT = 636
TIME_ZONE = 'US/Mountain'
