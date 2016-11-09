
import django
django.setup()

from django.conf import settings
from django.template import Template, Context

import logging
log = logging.getLogger(__name__)

# Use the configuration info above to render some config templates for other things on the system
with open(settings.SITE_ROOT/'etc'/'syslog.conf.tmpl') as tmpl_file:
    with open(settings.SITE_ROOT/'etc'/'syslog.conf', 'w') as out_file:
        _syslog_conf = Template(tmpl_file.read())
        _context = Context({'log_path': settings.SITE_ROOT/'log'/'capture.log'})
        out_file.write(_syslog_conf.render(_context))


