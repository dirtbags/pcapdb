from django.db import models
from django.contrib.auth.models import User, Group
from django.core.urlresolvers import reverse

from django.conf import settings

from collections import namedtuple
import binascii
import datetime
import pytz
import smtplib

import logging
log = logging.getLogger(__name__)

UserType = namedtuple('UserType', ['id', 'name', 'help'])


class UserExtraModel(models.Model):
    """Adds additional fields used to keep track of users."""

    BASIC = 'BASIC'
    SCRIPT = 'SCRIPT'
    LDAP = 'LDAP'

    USER_TYPES = [UserType(BASIC, 'Basic - Password Auth', 'Basic Auth users will be emailed '
                                  'automatically to finish registration and set a password.'),
                  UserType(SCRIPT, 'Script - Token Auth',
                           'Script users will be issued a token they can use to authenticate.')]

    MAX_TZ_LONGNAME_LEN = 18
    TIMEZONES = []
    # Build a list of tuples of timezone, limited_len_timezone
    for _tz in pytz.common_timezones:
        if len(_tz) < MAX_TZ_LONGNAME_LEN:
            TIMEZONES.append((_tz, _tz))
        else:
            # If the timezone longname is too long, abbreviate the continent and
            # truncate it.
            _cont, _rest = _tz.split('/', 1)
            _tz_short = '{}/{}'.format(_cont[:2], _rest)
            TIMEZONES.append((_tz, _tz_short[:18]))

    DEFAULT_USER_TYPE = 'BASIC'

    if settings.LDAP_AUTH_ENABLED:
        USER_TYPES.append(UserType(LDAP, 'LDAP - Remote Auth',
                                   'LDAP Users will authenticate against LDAP.'
                                   'Without LDAP groups enabled, you must create LDAP users here '
                                   'to give them permissions in the system before they may log in. '
                                   'With group mirroring, their groups are entirely determined by '
                                   'LDAP. They must be in the {} group to log in, the {} group to '
                                   'generally administer PcapDB, and the appropriate site groups '
                                   'to search those sites and administer systems.'
                                   .format(settings.LDAP_REQUIRED_GROUP, settings.ADMIN_GROUP)))

    user = models.OneToOneField(User,
                                primary_key=True,
                                related_name='extra',
                                help_text="The associated User.")
    type = models.CharField(max_length=max((len(t.id) for t in USER_TYPES)),
                            choices=[(t.id, t.name) for t in USER_TYPES],
                            help_text="Different user types are managed and authenticated "
                                      "differently.")
    timezone_name = models.CharField(max_length=max(map(len, pytz.common_timezones)),
                                     choices=TIMEZONES,
                                     help_text="Timezone to use to display times for this user.")

    @property
    def timezone(self):
        return pytz.timezone(self.timezone)

    @property
    def is_pcapdb_admin(self):
        admin_group = Group.objects.get(name=settings.ADMIN_GROUP)

        try:
            self.user.groups.get(id=admin_group.id)
            return True
        except Group.DoesNotExist:
            return False

    def is_admin_for(self, capture_node):
        """Returns True if this user is a site admin for the site that this node belongs to."""

        log.info("Checking admin for: {}".format(capture_node))

        try:
            self.user.groups.get(id=capture_node.site.admin_group_id)
            return True
        except Group.DoesNotExist:
            return False

    def is_user_for(self, capture_node):
        try:
            self.user.groups.get(id=capture_node.site.group_id)
            return True
        except Group.DoesNotExist:
            return False


class GroupTypeModel(models.Model):
    """Adds additional fields for keeping track of groups. Mostly we need to know
    which groups are core groups, which our 'site' groups, are 'site_admin' groups, and which are
    external groups. External groups won't have entries in this table."""

    LDAP_USERS = 'ldap_users'
    ADMIN = 'admin'
    SITE = 'site'
    SITE_ADMIN = 'site_admin'

    # These are transparent to the user, so no need for big explanations
    GROUP_TYPES = ((LDAP_USERS, 'ldap_users'),
                   (ADMIN, 'admin'),
                   (SITE, 'site'),
                   (SITE_ADMIN, 'site_admin'))

    group = models.OneToOneField(Group,
                                 primary_key=True,
                                 related_name='type',
                                 help_text='The associated group.')
    type = models.CharField(max_length=max([len(k) for k,d in GROUP_TYPES]),
                            choices=GROUP_TYPES)


class UserConfirmation(models.Model):
    """Keeps track of password reset tokens for basic authentication."""

    # 256 bits of possible tokens creates a significantly large space to brute force
    TOKEN_BYTES = 16

    # The max to randomly delay when working with user passwords and/or confirmation tokens.
    # The goal is to make timing attacks on the db impractical.
    DELAY_BASE = 2

    user = models.OneToOneField(User, on_delete=models.CASCADE, unique=True,
                                help_text="The user this confirmation record is for.")
    # The length is TOKEN_BYTES*2 because it's stored as a hex string.
    token = models.CharField(max_length=TOKEN_BYTES*2, unique=True,
                             help_text="A random token that will lead to a page where the user "
                                       "can set (or reset) their password.")
    created = models.DateTimeField(help_text="When this was created.")

    # How old can these be while still remaining valid.
    AGE_LIMIT = datetime.timedelta(days=2)
    RESET_RATE_LIMIT = datetime.timedelta(minutes=5)

    @classmethod
    def make_confirm(cls, user):
        """Check for an existing UserConfirmation object, or make a new one. Generate
        a random token in either case. This is limited to once every RESET_RATE_LIMIT per
        user. The object is updated in the database."""

        now = pytz.UTC.localize(datetime.datetime.now())

        try:
            confirm = cls.objects.get(user=user)
            # Limit how often passwords can be reset, and confirmation
            if now - confirm.sent < cls.RESET_RATE_LIMIT:
                return "Reset limit reached. Wait at least {:d} minutes." \
                    .format(int(cls.RESET_RATE_LIMIT/60))
        except cls.DoesNotExist:
            confirm = cls(user=user, created=now)

        # dev/random would be better, but we probably don't have a good source of randomness.
        # This is admin initiated, so it should be random enough.
        urandom = open('/dev/urandom', 'rb')
        confirm.token = binascii.hexlify(urandom.read(cls.TOKEN_BYTES))

        confirm.save()
        return confirm

    CONFIRM_MSG = '''Subject: PcapDB Account Confirmation
    From: {from_addr:}
    To: {to_addr:}

    To set/reset your password, go to {link:}.
    '''

    def send_confirmation(self, request):
        """Send the confirmation email.
        :return: A message on how the transaction went."""
        smtp_class = smtplib.SMTP

        try:
            with smtp_class(host=settings.SMTP_HOST, port=settings.SMTP_PORT) as smtp:

                if settings.SMTP_USER is not None:
                    smtp.login(settings.SMTP_USER, settings.SMTP_PASSWORD)

                link = request.build_absolute_uri(
                    reverse('search_head_gui:conf_password_reset', args=[self.token]))
                msg = self.CONFIRM_MSG.format(from_addr=settings.SMTP_FROM,
                                              to_addr=self.user.email,
                                              link=link)
                smtp.sendmail(settings.SMTP_FROM, [self.user.email], msg)
        except smtplib.SMTPRecipientsRefused:
            return "Email address rejected: {}".format(self.user.email)
        except smtplib.SMTPAuthenticationError:
            return "Authentication to the email server failed."
        except smtplib.SMTPHeloError:
            return "Could not connect to mail server."
        except smtplib.SMTPSenderRefused:
            return "Server rejected sender {}.".format(settings.SMTP_FROM)
        except smtplib.SMTPException:
            return "Server returned unknown error."

        # Set the sent time to now.
        self.sent = pytz.UTC.localize(datetime.datetime.now())
        self.save()

        return "Confirmation email to {} sent.".format(self.user.email)


