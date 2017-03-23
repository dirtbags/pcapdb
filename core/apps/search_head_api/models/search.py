import os
import uuid
import socket

from django.db import models

from django.contrib.auth.models import User
from django.core.files.storage import FileSystemStorage
from django.core.urlresolvers import reverse
from django.db import transaction
from django.db.models import Q

from django.conf import settings

from apps.search_head_api.models import CaptureNode, Site

tmp_fs = FileSystemStorage(location=str(settings.SITE_ROOT/'tmp'))
media_fs = FileSystemStorage(location=str(settings.SITE_ROOT/'media'))

# TODO: Tie the sites involved in a search to the search itself for later permissions checks.
class SearchInfo(models.Model):
    """A search result."""

    T_FLOW = 'flows'
    T_PCAP = 'pcap'
    TYPES = [(T_FLOW, 'Flow'),
             (T_PCAP, 'Pcap')]

    started = models.DateTimeField(auto_now_add=True,
                                   help_text="When this search was started.")
    completed = models.DateTimeField(null=True,
                                     help_text="When this search finished.")
    type = models.CharField(max_length=10, choices=TYPES,
                            help_text="The type of file.")
    query = models.CharField(max_length=1000)
    proto = models.IntegerField(help_text="The transport protocol filter for the search.")
    start = models.DateTimeField(help_text="The start of the search window.")
    end = models.DateTimeField(help_text="The end of the search window.")

    @property
    def flow_results_url(self):
        """
        :return: An absolute url to where to get the flow results.
        """
        return self._url(reverse('search_head_api:flows', kwargs={'search_id': self.id}))

    def url(self):
        """
        Return an absolute url to where to fetch the whole results file.
        """

        return self._url(reverse('search_head_api:result', [self.id]))

    @staticmethod
    def _url(path):
        if os.environ.get('HTTPS', 'off') == 'on':
            url = 'https://{}'.format(settings.UI_HOST)
        else:
            url = 'http://{}'.format(settings.UI_HOST)

        if hasattr(settings, 'HTTP_PORT') and settings.HTTP_PORT is not None:
            url += ':{}'.format(settings.HTTP_PORT)

        url += path

        return url

    def has_permission(self, user):
        """Check whether the user has permission to view the results for each of
        the sites involved in this search.
        :param User user: The user to check auth for.
        :return: True or False
        """

        groups = user.groups.all()

        # Get all the sites for which the user has view permissions
        sites_q = Q(group__in=groups) | Q(admin_group__in=groups)
        sites = Site.objects.filter(sites_q)

        # Get the sites involved in this search that the user does not have permissions for.
        unmatched = self.sites.exclude(site__in=sites)

        if unmatched:
            return False
        else:
            return True


class SearchSite(models.Model):
    search = models.ForeignKey(SearchInfo, models.CASCADE,
                               related_name='sites')
    site = models.ForeignKey(Site, models.CASCADE,
                             related_name='searches')


class SearchResult(models.Model):
    search = models.ForeignKey(SearchInfo, models.CASCADE,
                               related_name='results')
    site = models.ForeignKey(Site, models.CASCADE,
                             null=True)
    file = models.FileField(storage=media_fs,
                            help_text="File location information.")


class NodeSearch(models.Model):
    """This represents a search on a single capture node, and the location where the result
    file will be saved."""
    capture_node = models.ForeignKey(CaptureNode, models.CASCADE,
                                     help_text="The capture node this should have run on.")
    search = models.ForeignKey(SearchInfo, models.CASCADE,
                               related_name="node_results")
    # Note: When looking for valid tokens for upload permissions, ALWAYS check that
    # result is also an empty string.
    token = models.UUIDField(unique=True,
                             help_text="A random token that will allow a file to be uploaded.")
    file = models.FileField(storage=tmp_fs,
                            help_text="File location information.")

    @classmethod
    def new(cls, search):
        """Create and save a new node search entry."""

        ns = cls(search=search,
                 token=uuid.uuid4(),
                 result='')
        ns.save()

        return ns

    @transaction.atomic
    def save_file(self, file_obj):
        self.result.save(uuid.uuid4(), file_obj)
        self.save()

    def post_url(self):
        return 'https://{}{}'.format(
            settings.UI_HOST,
            reverse('search_head_api:node-search-result', args=[str(self.token)]))
