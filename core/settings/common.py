"""
Django settings for PcapDB interface project.

The basic model of the PCAPdb interface is as follows:
 - The search head/s are the only hosts that serve the HTML based interface.
 - The search head also provides an API to all functionality served by the HTML interface.
 - Authentication and permissions are governed by the search head.
 - All interactions with capture nodes happens via celery tasks initiated via the search head.
 - Permissions to search or modify the capture nodes are managed on a per-site basis. Sites are
   really just django.contrib.auth.models.Group objects.
 - For each site group (which gives search permissions) there is a site admin group for host
   management permissions.

For more information on this file, see
https://docs.djangoproject.com/en/1.8/topics/settings/

For the full list of settings and their values, see
https://docs.djangoproject.com/en/1.8/ref/settings/
"""

# Build paths inside the project like this: os.path.join(BASE_DIR, ...)
from configparser import ConfigParser
import datetime
from distutils.spawn import find_executable
from path import Path
import pytz
import socket
import sys

# The root directory of the project. The 'core' directory in the repo.
PROJECT_ROOT = Path(__file__).abspath().dirname().dirname()
# The root directory of the site. This is where our virtual environment, logs,
# and everything else related to the running of the system will live.
# It will also include the project root.
SITE_ROOT = PROJECT_ROOT.dirname()

# The path to where our virtual environment python should be located.
SITE_PYTHON = SITE_ROOT/'bin'/'python'

CAPTURE_USER = 'capture'
CAPTURE_GROUP = 'capture'

# Ensure our project libraries are in the python path.
sys.path.append(SITE_ROOT)
sys.path.append(PROJECT_ROOT/'apps')
sys.path.append(PROJECT_ROOT/'libs')

# These should be pulled from a system config file
config = ConfigParser()
config.read(SITE_ROOT/'etc'/'pcapdb.cfg')
IS_SEARCH_HEAD = config.getboolean('pcapdb', 'search_head', fallback=False)
IS_CAPTURE_NODE = config.getboolean('pcapdb', 'capture_node', fallback=False)

# The node name for this instance is just the host's fqdn.
NODE_NAME = socket.getfqdn()

UI_HOST = config.get('pcapdb', 'search_head_ui_host', fallback=NODE_NAME)

# If this is the search head, we don't need to be told who the search head is explicitely.
if IS_SEARCH_HEAD and not config.has_option('pcapdb', 'search_head_host'):
    config.set('pcapdb', 'search_head_host', NODE_NAME)

if IS_SEARCH_HEAD:
    SEARCH_HEAD_HOST = config.get('pcapdb', 'search_head_host', fallback=NODE_NAME)
else:
    SEARCH_HEAD_HOST = config.get('pcapdb', 'search_head_host')

# Group that defines admin access to pcapdb.
ADMIN_GROUP = config.get('pcapdb', 'admin_group', fallback='pcapdb_admin')

# Get the default timezone. fallback the fallback to America/Denver
DEFAULT_TZ = config.get('pcapdb', 'default_timezone', fallback='America/Denver')
if DEFAULT_TZ not in pytz.common_timezones:
    # If the timezone is bad, fallback to UTC
    DEFAULT_TZ = 'UTC'

# Get SMTP settings for sending users email
SMTP_USER = config.get('smtp', 'user', fallback=None)
SMTP_PASSWORD = config.get('smtp', 'password', fallback='')
SMTP_HOST = config.get('smtp', 'host', fallback='localhost')
SMTP_PORT = config.get('smtp', 'port', fallback=25)
SMTP_FROM = config.get('smtp', 'from', fallback='pcapdb@' + socket.getfqdn())

# Do everything in UTC. Trust the browser to convert timestamps to local time.
TIME_ZONE = 'UTC'

if not (IS_CAPTURE_NODE or IS_SEARCH_HEAD):
    raise RuntimeError("Must set each node to be a search head, capture node, or both.")

# Paths to commonly used executables
SUDO_PATH = find_executable('sudo')
CAPTURE_CMD = SITE_ROOT/'bin'/'capture'

MERGECAP_PATH = find_executable('mergecap')

SPLASH_TITLE = config.get('pcapdb', 'splash_title', fallback='System Usage Warning')
SPLASH_TEXT = config.get('pcapdb', 'splash_text',
                         fallback='This is a system owned by people who probably consider it '
                                  'illegal, bad, or at least rude if you used it without '
                                  'authorization. If you have authorization, then you probably '
                                  'signed away your right to privacy in regards to using this '
                                  'system. NOTE: This message is entirely configurable.')

# How big should our FCAP files be?
# This should not be toyed with lightly; A change here requires rebuilding all of the capture
# disks to have slots of this size.
# The default size, 4 GiB, was chosen because that is the limit of 32 bit addressing.
# The system can handle larger or smaller, and will dynamically choose 32 or 64 bit addressing
# for the index files depending on the size of the file they're indexing. Since the addressing is
# dynamic per index file, larger FCAP's shouldn't come with that big of a size penalty. The FLOW
# index will be 4 bytes larger per record, but the sub-indexes won't have to use 64 bit
# addressing unless the FLOW index is larger than 4 GiB.
FCAP_SIZE = 4*(1024**3)

# The MTU to set on all interfaces. Depending on the capture system used, this may not actually
# have any effect. We set it to 9000 because that's the largest MTU most interfaces will accept.
# Note that the MTU for actual capture is set separately, at compile time, about twice this. We
# have seen packets far larger than this, and IPv6 can support jumbo frames of up to 2^32 bytes.
# The reason for this discrepancy that dynamic packet reassembly at the interface can result
# in huge packets.
# Note that I've seen interfaces that support an MTU up to 9710.
MTU = 9000

# Protocols to support directly filtering in the interface. The search system supports
# every transport protocol, but by default we only allow filtering by TCP or UDP.
# Protocol 0 is used as a wildcard.
SEARCH_TRANSPORT_PROTOS = {
    0: 'all',
    6: 'tcp',
    17: 'udp'
}

# The label that the device that will host the PCAPdb indexes is expected to have. If you build
# the device with PCAPdb, it will be given this label. When the system starts, PCAPdb will expect
# to find a device with this label, and will mount it if necessary.
INDEX_DEV_LABEL = 'pcapdb_index'
if len(INDEX_DEV_LABEL) > 12:
    raise ValueError("Invalid device label {}.")

# The base directory for all our capture mount points.
CAPTURE_PATH = SITE_ROOT/'capture'

# The mount point for our index device
INDEX_PATH = CAPTURE_PATH/'index'
# The fraction of index disk reserved as 'slack' indexes are only deleted
# to make room for this slack.
INDEX_DISK_RESERVED = 0.2

# How long should our index and capture slot names be?
# NOTE: These should either be passed as a parameter to the capture system (it's currently
# hard-coded), or loaded directly from the capture system libraries.
INDEX_NAME_LEN = 20
SLOT_NAME_LEN = 9

# How often to tell the server about progress, in seconds
CELERY_TASK_STATE_UPDATE_PERIOD = 1.5

# SECURITY WARNING: don't run with debug turned on in production!
DEBUG = False

ALLOWED_HOSTS = []
if IS_SEARCH_HEAD:
    ALLOWED_HOSTS.append(NODE_NAME)
    if config.has_option('pcapdb', 'allowed_hosts'):
        ALLOWED_HOSTS.append(config.get('pcapdb', 'allowed_hosts'))

CACHES = {
    'default': {
        'BACKEND': 'django.core.cache.backends.dummy.DummyCache',
    }
}

LOGIN_URL = '/auth/unauthorized'

# Application definition
INSTALLED_APPS = [
    'django.contrib.contenttypes',
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    'djcelery',
    'libs.custom_tags',
    'rest_framework',
    # The celery tasks, at least, will need to be understood by the search head.
    'apps.capture_node_api',
    'apps.core',
    'apps.task_api',
    'apps.search_head_gui',
    'apps.search_head_api',
    'apps.stats_api',
    'apps.login_gui',
    'apps.login_api'
]


# These should only be installed if this is a search head.
# This is super dumb, but necessary thanks to PyCharm and the fact that it hacks INSTALLED_APPS
# out of this file rather than actually run it.
SEARCH_HEAD_ONLY_APPS = ['django.contrib.admin',
                         'django.contrib.sessions',
                         'django.contrib.messages',
                         'django.contrib.staticfiles',
                         'libs.custom_tags',
                         'apps.login_api',
                         'apps.login_gui',
                         'apps.task_api',
                         'apps.search_head_gui']
if not IS_SEARCH_HEAD:
    for app in SEARCH_HEAD_ONLY_APPS:
        INSTALLED_APPS.remove(app)

MIDDLEWARE_CLASSES = (
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    #'django.contrib.auth.middleware.SessionAuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
    'django.middleware.security.SecurityMiddleware',
)

ROOT_URLCONF = 'settings.urls'

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [PROJECT_ROOT/'static'/'templates'],
        # 'APP_DIRS': True,
        'OPTIONS': {
            'context_processors': [
                'django.template.context_processors.debug',
                'django.template.context_processors.request',
                'django.contrib.auth.context_processors.auth',
                'django.contrib.messages.context_processors.messages',
                'django.template.context_processors.static',
            ],
            'loaders': [
                'django.template.loaders.filesystem.Loader',
                'django.template.loaders.app_directories.Loader',
                'django.template.loaders.eggs.Loader',
            ]
        },
    },
]

WSGI_APPLICATION = 'settings.wsgi.application'


# Database
# https://docs.djangoproject.com/en/1.8/ref/settings/#databases
DATABASES = {'default': {
    'ENGINE': 'django.db.backends.postgresql_psycopg2',
    'NAME': config.get('pcapdb', 'db_name', fallback='pcapdb'),
    'USER': config.get('pcapdb', 'db_user'),
    'PASSWORD': config.get('pcapdb', 'db_pass')
    }
}

# Use peer auth unless this isn't the search head
if not IS_SEARCH_HEAD:
    DATABASES['default']['HOST'] = SEARCH_HEAD_HOST

if IS_CAPTURE_NODE:
    # Use peer authentication
    DATABASES['capture_node'] = {
        'ENGINE': 'django.db.backends.postgresql_psycopg2',
        'NAME': config.get('pcapdb', 'capnode_db_name', fallback='capture_node'),
        }

DATABASE_ROUTERS = ['apps.core.routers.BaseRouter',
                    'apps.capture_node_api.CaptureNodeRouter']

REST_FRAMEWORK = {
    'DEFAULT_PERMISSION_CLASSES': (
        'rest_framework.permissions.IsAuthenticated',
    ),
    'DEFAULT_AUTHENTICATION_CLASSES': (
        'rest_framework.authentication.SessionAuthentication',
        'rest_framework.authentication.BasicAuthentication',
        'rest_framework_jwt.authentication.JSONWebTokenAuthentication',
    ),
}

# Internationalization
# https://docs.djangoproject.com/en/1.8/topics/i18n/

LANGUAGE_CODE = 'en-us'
USE_I18N = True
USE_L10N = True
USE_TZ = True

# Static files (CSS, JavaScript, Images)
# https://docs.djangoproject.com/en/1.8/howto/static-files/

STATIC_URL = '/static/'
STATIC_ROOT = SITE_ROOT/'static'
STATICFILES_FINDERS = (
    'django.contrib.staticfiles.finders.FileSystemFinder',
    'django.contrib.staticfiles.finders.AppDirectoriesFinder',
)
STATICFILES_DIRS = []

# How many things we can combine in a single command when searching.
MAX_SEARCH_BATCH = 500

# Django 1.8 Security settings
# SECURE_HSTS_SECONDS = 600
SECURE_CONTENT_TYPE_NOSNIFF = True
# SECURE_HSTS_INCLUDE_SUBDOMAINS = True
SECURE_SSL_REDIRECT = False            # Force SSL
SECURE_BROWSER_XSS_FILTER = True      # Tell browsers to enable XSS tools
SESSION_COOKIE_SECURE = False
CSRF_COOKIE_SECURE = False
SESSION_COOKIE_HTTPONLY = True
# If this is set to True, client-side JavaScript will not
# to be able to access the CSRF cookie, it should be set to
# true in prod
CSRF_COOKIE_HTTPONLY = False
CSRF_COOKIE_NAME = 'pcapdbcsrftoken'
CSRF_FAILURE_VIEW = 'apps.search_head_api.views.csrf_failure'
SESSION_EXPIRE_AT_BROWSER_CLOSE = True
SESSION_COOKIE_AGE = 60 * 60 * 8      # 8 hours
SESSION_COOKIE_NAME = "pcapdb"

PRIMARY_LOG_FILE = SITE_ROOT/'log'/'django.log'
LOGGING = {
    'version': 1,
    'disable_existing_loggers': False,
    'filters': {
        'require_debug_false': {
            '()': 'django.utils.log.RequireDebugFalse'
        }
    },
    'formatters': {
        'standard': {
            'format': "[%(asctime)s] %(levelname)s [%(name)s:%(lineno)s] %(message)s",
            'datefmt': "%d/%b/%Y %H:%M:%S"
        },
    },
    'handlers': {
        'logfile': {
            'level': 'DEBUG',
            'class': 'logging.handlers.RotatingFileHandler',
            'filename': PRIMARY_LOG_FILE,
            'maxBytes': 1024 * 1024 * 10,  # 10MB log file limit
            'backupCount': 10,
            'formatter': 'standard',
        },
        'console': {
            'level': 'DEBUG',
            'class': 'logging.StreamHandler',
            'stream': sys.stdout
        },
    },
    'loggers': {
        'django': {
            'handlers': ['console', 'logfile'],
            'propagate': True,
            'level': 'ERROR',
        },
        'django.db.backends': {
            'handlers': ['console', 'logfile'],
            'propagate': False,
            'level': 'INFO',
        },
        '': {
            'handlers': ['console', 'logfile'],
            'level': 'DEBUG',
        },
    }
}

# Acceptable password alphabet
PASSWORD_RE = r'^[a-zA-Z0-9+=!@#$%^&*().,><;:/?\][}{` ~_-]{8,100}$'
# lib zxcvbn password strength, on a scale of 0-4
PASSWORD_STRENGTH_MIN = 3

AUTHENTICATION_BACKENDS = []
if IS_SEARCH_HEAD:
    # We only need user based authentication for the search head.
    AUTHENTICATION_BACKENDS.append('django.contrib.auth.backends.ModelBackend')

LDAP_AUTH_ENABLED = False
LDAP_GROUPS_ENABLED = False

# LDAP Authentication Configuration
if config.has_section('ldap_auth'):
    from django_auth_ldap.config import *

    AUTHENTICATION_BACKENDS.append('django_auth_ldap.backend.LDAPBackend')

    LDAP_AUTH_ENABLED = True

    # LDAP configuration is notoriously hard to debug. Make sure to log everything.
    import logging
    ldap_logger = logging.getLogger('django_auth_ldap')
    logger.addHandler(logging.StreamHandler())
    logger.setLevel(logging.DEBUG)

    # Baseline configuration.
    AUTH_LDAP_SERVER_URI = config.get('ldap_auth', 'server')

    # Default user group to put new users in.
    LDAP_REQUIRED_GROUP = config.get('ldap_auth', 'default_group', fallback='pcapdb_user')

    AUTH_LDAP_BIND_DN = config.get('ldap_auth', 'auth_bind_dn', fallback='')
    AUTH_LDAP_BIND_PASSWORD = config.get('ldap_auth', 'auth_bind_password', fallback='')
    _USER_SEARCH_BASE = config.get('ldap_auth', 'user_search_base')
    _USER_ATTR = config.get('ldap_auth', 'user_attr', fallback='uid')
    AUTH_LDAP_USER_SEARCH = LDAPSearch(_USER_SEARCH_BASE,
                                       ldap.SCOPE_SUBTREE,
                                       "({}=%(user)s)".format(_USER_ATTR))

    AUTH_LDAP_USER_ATTR_MAP = {}
    for attr in 'username', 'first_name', 'last_name', 'email':
        key_name = '{}_attr'.format(attr)
        if config.has_option('ldap_auth', key_name):
            AUTH_LDAP_USER_ATTR_MAP[attr] = config['ldap_auth'][key_name]
    import logging
    log = logging.getLogger(__name__)

    if 'ldap_groups' in config.sections():
        LDAP_GROUPS_ENABLED = False

        AUTH_LDAP_MIRROR_GROUPS = True

        # Use LDAP group membership to calculate group permissions.
        AUTH_LDAP_FIND_GROUP_PERMS = True

        _GROUP_NAME_ATTR = config.get('ldap_groups', 'group_name_attr', fallback='cn')
        _GROUP_SEARCH_BASE = config.get('ldap_groups', 'group_search_base')

        _GROUP_TYPE = config.get('ldap_groups', 'group_type')
        if _GROUP_TYPE == 'posix':
            AUTH_LDAP_GROUP_TYPE = PosixGroupType(_GROUP_NAME_ATTR)
            _GROUP_OBJECT_CLASS = 'posixGroup'
        elif _GROUP_TYPE == 'memberdn':
            AUTH_LDAP_GROUP_TYPE = MemberDNGroupType(['owner', 'member'], _GROUP_NAME_ATTR)
            _GROUP_OBJECT_CLASS = 'posixGroup'
        else:
            raise ValueError("Invalid LDAP group type in config: {}. Choices: {}"
                             .format(_GROUP_TYPE, ', '.join(['posix', 'memberdn'])))

        _GROUP_NAME_PREFIX = config.get('ldap_groups', 'group_name_prefix', fallback=None)
        if _GROUP_NAME_PREFIX is not None:
            _group_filter = '(&(objectClass={})({}={}*))'.format(_GROUP_OBJECT_CLASS,
                                                                 _GROUP_NAME_ATTR,
                                                                 _GROUP_NAME_PREFIX)
        else:
            _group_filter = '(objectClass={})'.format(_GROUP_OBJECT_CLASS)

        # Set up the basic group parameters.
        AUTH_LDAP_GROUP_SEARCH = LDAPSearch(_GROUP_SEARCH_BASE,
                                            ldap.SCOPE_SUBTREE,
                                            _group_filter)

        # log.error('require group: {}'.format(AUTH_LDAP_REQUIRE_GROUP))
        log.error('group_filter: {}'.format(_group_filter))

        # Cache group memberships for an hour to minimize LDAP traffic
        AUTH_LDAP_CACHE_GROUPS = True
        AUTH_LDAP_GROUP_CACHE_TIMEOUT = 3600


# Celery Configuration variables.
BROKER_URL = 'amqp://{user}:{password}@{host}:{port}//'.format(
    user=config.get('celery', 'amqp_user', fallback='pcapdb'),
    password=config.get('celery', 'amqp_password'),
    host=SEARCH_HEAD_HOST,
    port=config.get('celery', 'amqp_port', fallback='5672'),
)
CELERY_RESULT_BACKEND = 'djcelery.backends.database:DatabaseBackend'
from kombu import Queue, Exchange

CELERY_QUEUES = []
if IS_SEARCH_HEAD:
    CELERY_QUEUES.append(Queue('celery'))
    CELERY_QUEUES.append(Queue('search_head', exchange=Exchange('search_head')))
if IS_CAPTURE_NODE:
    CELERY_QUEUES.append(Queue(NODE_NAME, exchange=Exchange('capture_node')))

CELERY_ROUTES = ['capture_node_api.routers.capture_node_router']
CELERY_EVENT_SERIALIZER = 'json'
CELERY_TASK_SERIALIZER = 'json'
CELERY_RESULT_SERIALIZER = 'json'
CELERY_ACCEPT_CONTENT = ['json']
CELERYBEAT_SCHEDULE = {}
CELERYBEAT_SCHEDULE_FILENAME = SITE_ROOT/'celerybeat-schedule'
CELERYD_PREFETCH_MULTIPLIER = 1

if IS_CAPTURE_NODE:
    # Send stats to the search head every five minutes
    CELERYBEAT_SCHEDULE['gather_if_stats'] = {
        'options': {'queue': NODE_NAME},
        'task': 'apps.capture_node_api.tasks.stats.update_stats',
        'schedule': datetime.timedelta(seconds=60)
    }
    CELERYBEAT_SCHEDULE['clean_indexes'] = {
        'options': {'queue': NODE_NAME},
        'task': 'apps.capture_node_api.tasks.maint.clean_indexes',
        'schedule': datetime.timedelta(minutes=15)
    }
