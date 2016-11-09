from django.db import models


class CaptureConfig(models.Model):
    file_size = models.IntegerField(verbose_name="Capture File Size (in MB)",
                                    help_text="The size of the capture files, in MB. "
                                              "Defaults to 4GB")
    mode = models.TextField(verbose_name="Capture Library",
                            help_text="The library to use for packet capture. PF-ring is less "
                                      "prone to packet loss; more so with a zero-copy license.",
                            choices=['PFRING', 'LIBPCAP'])
    changed = models.DateTimeField(verbose_name="The last time these settings were changed.")
    changed_by = models.TextField(verbose_name="Who changed this setting last.")


# This table holds a list of enabled capture interfaces
# and their configuration.
class Interface(models.Model):
    name = models.TextField(verbose_name="Interface Name",
                            help_text="The name of the interface, as recognized by the local "
                                      "system.")
    queues = models.IntegerField(verbose_name="Interface Queues",
                                 help_text="If supported by the network card, queues allow "
                                           "capture traffic to be distributed by the card to "
                                           "multiple virtual interfaces.")
    since = models.DateTimeField(verbose_name="Interface Enable Timestamp",
                                 help_text="When this interface was last enabled.")