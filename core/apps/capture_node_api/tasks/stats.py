import logging
from collections import defaultdict

import pytz
from celery import shared_task
from django.conf import settings
from django.db import transaction
from django.utils import timezone
from django.core.exceptions import ObjectDoesNotExist

from apps.capture_node_api.models.capture import Index, Stats as IdxStats
from apps.capture_node_api.models.status import Status
from apps.search_head_api.models import CaptureNode
from apps.stats_api.models import Stats, StatsInterface
log = logging.getLogger(__name__)


@shared_task(bind=True)
def update_stats(self):
    """Normalize and upload the most recent capture stats for this indexer to the search head."""
    # See the search_head_api Stats model for a detailed description of what's going on here.

    status = Status.load()

    last = status.last_stats_upload

    # Get all the indexes that have been readied since our last check
    new_indexes = Index.objects.filter(ready=True).order_by('start_ts')
    if last is not None:
        new_indexes = new_indexes.filter(readied__gt=last)

    if len(new_indexes) == 0:
        # Nothing to do...
        return

    first_ts = new_indexes[0].start_ts
    # Truncate the start to get the first minute we'll mess with
    start_minute = pytz.UTC.localize(timezone.datetime(*[first_ts.year,
                                                         first_ts.month,
                                                         first_ts.day,
                                                         first_ts.hour,
                                                         first_ts.minute]))

    capture_node = CaptureNode.objects.get(hostname=settings.NODE_NAME)

    oldest = None

    # We have to do this as a single transaction on the search head.
    with transaction.atomic(using='default'):

        # All the interfaces must exist in the db, so we create any that don't already
        interfaces = {iface.name: iface for iface in StatsInterface.objects.all()}
        touched_ifaces = IdxStats.objects.values('interface').distinct()
        for rec in touched_ifaces:
            iface_name = rec['interface']
            if iface_name not in interfaces:
                # The atomic transaction should ensure uniqueness here.
                new_iface = StatsInterface(name=iface_name)
                new_iface.save()
                interfaces[iface_name] = new_iface

        # Get the stats objects that might overlap with what we're adding for this capture node.
        # prefetch the interface table data too.
        existing_stats = Stats.objects.filter(capture_node=capture_node, minute__gte=start_minute)\
                                      .select_related('interface')
        # A dictionary of interface names to a dictionary of minutes.
        # Note that while interface id's would be faster, they haven't necessarily been created yet.
        interface_minutes = defaultdict(lambda: {})
        # Populate our interface minutes with the existing objects
        for stat in existing_stats:
            interface_minutes[stat.interface.name][stat.minute] = stat

        # Update/create new minutes for all the stats we're processing.
        for idx in new_indexes:
            if oldest is None or idx.readied > oldest:
                oldest = idx.readied

            # Get all the stat minutes objects referenced by this object
            try:
                iface = idx.stats.interface
            except ObjectDoesNotExist:
                continue
            stat_minutes = Stats.from_index(idx, capture_node, interfaces[iface])

            iface_minutes = interface_minutes[idx.stats.interface]
            for stat in stat_minutes:
                minute = stat.minute
                old_stat = iface_minutes.get(minute, None)
                if old_stat is None:
                    # No pre-existing minute data, so this is it.
                    iface_minutes[minute] = stat
                else:
                    # Update the existing minute with the new info.
                    old_stat.merge(stat)

        # Separate all the already existing objects from those that need to be created.
        # This allows us to bulk save the new ones.
        new = []
        old = []
        for idx_minutes in interface_minutes.values():
            for stat in idx_minutes.values():
                if not stat.pk:
                    new.append(stat)
                else:
                    old.append(stat)

        for stat in old:
            stat.save()
        Stats.objects.bulk_create(new)

    status.last_stats_upload = oldest
    status.save()

    return {}
