from django.contrib.auth.models import Group
from django.contrib.auth.mixins import LoginRequiredMixin, UserPassesTestMixin
from django.core.exceptions import PermissionDenied
from django.db.models import Q
from django.db import transaction
from django.conf import settings
from django.http import Http404, HttpRequest
from django.views.generic.base import TemplateView

from apps.search_head_api.models import Site, CaptureNode
from apps.search_head_api.models.auth import UserConfirmation, UserExtraModel, GroupTypeModel

import datetime
import pytz
import random
import time

import logging
log = logging.getLogger(__name__)

MAX_TZ_LONGNAME_LEN = 18


class AuthenticatedTemplateView(LoginRequiredMixin, TemplateView):
    pass


class AdminView(UserPassesTestMixin, AuthenticatedTemplateView):
    def test_func(self):
        return len(self.request.user.groups.filter(name=settings.ADMIN_GROUP)) != 0


class UserManagementView(AdminView):
    template_name = 'conf/users.html'

    @transaction.atomic
    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        # We use loading this page as an excuse to clean up groups.
        # We should probably pull this out and call it explicitly whenever the admin
        # or ldap_users group is changed.

        # Get all the unused, untyped groups (LDAP groups with no associated user).
        unused_unlabeled = Group.objects.filter(type=None, user=None)
        for grp in unused_unlabeled:
            grp.delete()

        if settings.LDAP_AUTH_ENABLED and not settings.LDAP_GROUPS_ENABLED:

            orphaned_users = set()

            # Make sure we have only one ldap required group.
            ldap_user_grp = None
            ldap_user_grps = Group.objects.filter(type__type='ldap_users')
            for grp in ldap_user_grps:
                if grp.name != settings.LDAP_REQUIRED_GROUP:
                    orphaned_users.union(grp.user_set.all())
                    log.info("Someone changed the ldap users group in the config. Deleting"
                             "old group: {}".format(grp.name))
                    grp.delete()
                else:
                    ldap_user_grp = grp

            if ldap_user_grp is None:
                # We have no group labeled as our required group that is actually it.
                # Try to get one that isn't properly labeled.
                try:
                    ldap_user_grp = Group.objects.get(name=settings.LDAP_REQUIRED_GROUP)
                except Group.DoesNotExist:
                    # Or if that fails, make it.
                    ldap_user_grp = Group(name=settings.LDAP_REQUIRED_GROUP)
                    ldap_user_grp.save()

                # Now label it.
                grp_type = GroupTypeModel(group=ldap_user_grp, type='ldap_users')
                ldap_user_grp.type = grp_type
                grp_type.save()

            # Reassign the orphaned users
            for user in orphaned_users:
                ldap_user_grp.user_set.add(user)


        admin_group = None
        orphaned_admins = set()
        for grp in Group.objects.filter(type__type='admin'):
            # Delete any extra, old admin groups. This happens when you change the admin
            # group name.
            if grp.name != settings.ADMIN_GROUP:
                orphaned_admins.union(grp.user_set.all())
                log.info("Orphaned admin group deleted ({}). This happens when the admin "
                         "group name changes in the configuration.".format(grp.name))
                grp.delete()
            else:
                admin_group = grp

        if admin_group is None:
            # Find the admin group, or create it if it doesn't exist.
            try:
                admin_group = Group.objects.get(name=settings.ADMIN_GROUP)
            except Group.DoesNotExist:
                admin_group = Group(name=settings.ADMIN_GROUP)
                admin_group.save()

            admin_group_type = GroupTypeModel(group=admin_group, type='admin')
            admin_group_type.save()
            admin_group.save()

        # Reassign the orphaned users.
        for user in orphaned_admins:
            admin_group.user_set.add(user)

        query = Q(type__type='site') | Q(type__type='site_admin') | Q(type__type='admin')
        if settings.LDAP_AUTH_ENABLED and not settings.LDAP_GROUPS_ENABLED:
            query |= Q(type__type='ldap_users')

        groups = Group.objects.filter(query)

        context['user_extra'] = UserExtraModel
        context['groups'] = groups

        context['default_tz'] = settings.DEFAULT_TZ
        context['timezones'] = UserExtraModel.TIMEZONES

        return context


class PasswordResetView(TemplateView):
    template_name = 'auth/reset.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        token = kwargs.get('token')

        age_limit = pytz.UTC.localize(datetime.datetime.now()) - UserConfirmation.AGE_LIMIT

        # Grab the confirmation object for this view, if it exists.
        confirm = UserConfirmation.objects.filter(token=token, created__gt=age_limit)

        time.sleep(random.random()*UserConfirmation.DELAY_BASE)

        if not confirm:
            # No such valid token
            raise Http404

        context['confirm'] = confirm[0]
        context['token'] = token

        return context


class SiteManagementView(AdminView):
    template_name = 'conf/sites.html'

    def get_context_data(self, **kwargs):

        context = super().get_context_data(**kwargs)
        return context


class CaptureNodeTemplateView(AuthenticatedTemplateView):

    def dispatch(self, request, *args, **kwargs):
        """
        :param HttpRequest request:
        """

        log.info("Stuff: {} {} {}".format(kwargs.keys(), request.user, kwargs['capnode']))

        if 'capnode' not in kwargs:
            raise RuntimeError("View requires a capture node but the url does not give one.")

        try:
            capnode = CaptureNode.objects.get(id=kwargs['capnode'])
        except CaptureNode.DoesNotExist:
            raise Http404

        log.info("Ok, punk")

        try:
            request.user.groups.get(id=capnode.site.admin_group.id)
        except Group.DoesNotExist:
            raise PermissionDenied("Not an admin for this site.")

        return super().dispatch(request, *args, **kwargs)

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context['capnode'] = CaptureNode.objects.get(id=kwargs['capnode'])
        return context


class CaptureNodesView(AdminView):
    template_name = 'conf/capture_nodes.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context['sites'] = Site.objects.all()

        return context


class CaptureNodeDiskView(CaptureNodeTemplateView):
    template_name = 'conf/remote_disks_gui.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        return context


class CaptureNodeIfacesView(CaptureNodeTemplateView):
    template_name = 'conf/remote_capture.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        return context
