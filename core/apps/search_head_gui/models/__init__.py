from django.db import models
from django.db.models.signals import pre_save
from django.dispatch import receiver
from django.contrib.postgres.fields import JSONField
# Contrib Models
from django.contrib.auth.models import User
import json
from django.utils.timezone import now   # UTC now()

import re

class AppConfig(models.Model):
    category = models.CharField(max_length=64)
    key = models.CharField(max_length=256, unique=True)
    value = JSONField()
    modified_by = models.CharField(max_length=16)   # moniker or znum

    def __str__(self):
        return "%s - %s - %s" % (self.category, self.key, self.value)


@receiver(pre_save, sender=AppConfig)
def appconfig_pre_save(sender, instance, *args, **kwargs):
    instance.key = instance.key.upper()
    if isinstance(instance.value, str):
        instance.value = json.dumps(instance.value)
    if not re.match(r'^[A-Z0-9_]+$', instance.key):
        raise Exception("Invalid characters used in AppConfig key. "
                        "Attempted key was [%s]." % instance.key)


class AppConfigHistory(models.Model):
    current_config = models.ForeignKey(AppConfig)
    old_value = JSONField()
    timestamp = models.DateTimeField(default=now)
    modified_by = models.CharField(max_length=16)   # moniker or znum

    def __str__(self):
        return "%s - %s - %s" % (self.current_config.category,
                                 self.current_config.key,
                                 self.old_value)


# Your models here
