from django.db import models
from django.contrib.auth.models import Group
from django.core.urlresolvers import reverse
import socket


class Site(models.Model):
    """Records for sites that group capture nodes into geographical or logical units."""

    name = models.SlugField(max_length=15, unique=True,
                            help_text="The name of this site.")
    group = models.ForeignKey(Group, models.CASCADE, related_name='site',
                              help_text="The user group for this site.")
    admin_group = models.ForeignKey(Group, models.CASCADE, related_name='site_admin',
                                    help_text="The admin group for this site.")


class CaptureNode(models.Model):
    """This model keeps track of who our capture node hosts are."""

    # The domain name or ip of an capture node
    hostname = models.CharField(max_length=100, unique=True,
                                help_text="The fully qualified domain name for this capture node host.")
    site = models.ForeignKey(Site, models.CASCADE, related_name='capture_nodes',
                             help_text="The site this Capture Node belongs to.")

    @property
    def addr(self):
        """Try to get the addr of this CaptureNode based on it's hostname. If one can't be found,
        None is returned."""
        try:
            return socket.getaddrinfo(self.hostname, None)[0][4][0]
        except (socket.gaierror, IndexError):
            return None

    @property
    def disks_uri(self):
        """
        :return str: The URI to this capture node's disk configuration menu.
        """
        return reverse('search_head_gui:conf_capture_node_disks', args=(self.id,))

    @property
    def ifaces_uri(self):
        """
        :return str: The URI to this capture node's iface configuration menu.
        """

        return reverse('search_head_gui:conf_capture_node_ifaces', args=(self.id,))


