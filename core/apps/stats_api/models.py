# from django.conf import settings
from django.db import models
from django.utils import timezone

from apps.search_head_api.models import CaptureNode

from math import ceil
import pytz

from apps.capture_node_api.models.capture import Index, Stats as IdxStats, TransportStats
# from apps.search_head_api.models import CaptureNode

__author__ = 'shannon'

import logging
log = logging.getLogger(__name__)

class StatsInterface(models.Model):
    """A model for storing unique interface names."""
    name = models.CharField(max_length=129, unique=True,
                            help_text="The interface on the capture node that the data "
                                      "originated from.")


class Stats(models.Model):
    """The stats for a given minute for a particular capture node and network interface.
    Note that while the stats on the capture nodes are exact, they are for irregular time
    periods. These stats, on the other hand, are for regular time periods and are the sum of the
    rates per minute of each of the index stats. Let me try to explain a bit better:

    For a given interface, a capture node creates a series of indexes over some span of time.
      idx files:    |     idx1     | idx2 |              idx3            |.....
      time/minutes:  ^     1     ^     2     ^     3     ^     4     ^

    The rates are broken down by minute, but the minutes may span multiple idx files, or less
    than a whole one. In the example above:
     Index    | packets | time span | packet_rate
      idx1    | 1000    | 1.5 min   | 666 packets/min
      idx2    | 1000    | .5 mins   | 2000 packets/min
      idx3    | 1000    | 3 mins    | 200 packets/min

    To generate stat summaries, we cut those rates into the timeslices they actually occupy.
    For minute 1, which is contained entirely in idx1, the total packets are estimated at:
      1 minute * 666 packets/min = 666 packets
    For minute 2, which has all three idxs in it (1dx1 and idx2 each cover 15 seconds of that
        minute):
      0.25 min * 666 pkts/min + 0.5 min * 2000 pkts/min + .25 min * 200 pkts/min == 1216.5

    The calculations for including the totals for each index need not occur together, and are in
     fact done separately, simply summing them with prior calculations.
    """
    capture_node = models.ForeignKey(CaptureNode, db_index=True,
        help_text="The capture_node this stats data originated from.")

    interface = models.ForeignKey(StatsInterface, models.DO_NOTHING, db_index=True,
        help_text="The associated interface.")

    minute = models.DateTimeField(db_index=True,
        help_text="The minute of time (UTC) this represents.")

    capture_size = models.BigIntegerField(help_text="Bytes captured")
    received = models.BigIntegerField(help_text="Packets captured")
    dropped = models.BigIntegerField(help_text="Packets dropped")
    ipv4 = models.BigIntegerField(help_text="Ipv4 packets captured")
    ipv6 = models.BigIntegerField(help_text="Ipv6 packets captured")
    network_other = models.BigIntegerField(help_text="Other network protocol packets captured.")
    tcp = models.BigIntegerField(help_text="TCP packets captured")
    udp = models.BigIntegerField(help_text="UDP packets captured")
    transport_other = models.BigIntegerField(help_text="Other transport protocol packets captured")

    def merge(self, new_stats):
        """Merge the information in this stats object with another. It's assumed that they're
        referring to the same capture_node, interface and minute.
        :param Stats new_stats:
        :return:
        """
        self.capture_size += new_stats.capture_size
        self.received += new_stats.received
        self.dropped += new_stats.dropped
        self.ipv4 += new_stats.ipv4
        self.ipv6 += new_stats.ipv6
        self.network_other += new_stats.network_other
        self.tcp += new_stats.tcp
        self.udp += new_stats.udp
        self.transport_other += new_stats.transport_other

    @classmethod
    def from_index(cls, idx, capture_node, interface):
        """Generate stats objects from capture node index objects (see main class docstring)
        :param Index idx: The Index Stats object to convert into
        :param CaptureNode capture_node: The associated capture node object
        :param StatsInterface interface: The associated interface object
        :rtype [Stats]:
        """

        start_ts = idx.start_ts
        minute = pytz.UTC.localize(timezone.datetime(*[start_ts.year,
                                                      start_ts.month,
                                                      start_ts.day,
                                                      start_ts.hour,
                                                      start_ts.minute]))

        stat_minutes = []
        # What fraction of a minute does this index cover
        time_base = (idx.end_ts - idx.start_ts).total_seconds()/60

        if idx.stats is None:
            log.error("Index missing stats. This usually only happens from a hard shutdown.")
            return []

        # Calculate the capture rates for each attribute
        capture_size = idx.stats.capture_size/time_base
        received = idx.stats.received/time_base
        dropped = idx.stats.dropped/time_base
        ipv4 = idx.stats.ipv4/time_base
        ipv6 = idx.stats.ipv6/time_base
        network_other = idx.stats.network_other/time_base
        try:
            tcp = idx.stats.transportstats_set.get(transport=6).count/time_base
        except TransportStats.DoesNotExist:
            tcp = 0
        try:
            udp = idx.stats.transportstats_set.get(transport=17).count/time_base
        except TransportStats.DoesNotExist:
            udp = 0

        # Get the total number of packets from transports that aren't TCP/UDP
        transport_other_q = idx.stats.transportstats_set.exclude(transport__in=[6, 17])\
                                     .aggregate(models.Sum('count'))['count__sum']
        if transport_other_q is None:
            transport_other = 0
        else:
            transport_other = transport_other_q/time_base

        while minute < idx.end_ts:
            # Calculate fraction of the index contained within this minute.
            partial = timezone.timedelta(minutes=1)
            if idx.start_ts > minute:
                partial -= (idx.start_ts - minute)
            minute_end = minute + timezone.timedelta(minutes=1)
            if idx.end_ts < minute_end:
                partial -= (minute_end - idx.end_ts)
            partial = partial.total_seconds()/60

            # Create the new stats object with amounts based on the fraction of a minute
            # covered by this item. These will be floats, but
            stat_minutes.append(cls(capture_node=capture_node,
                                    interface=interface,
                                    minute=minute,
                                    capture_size=ceil(capture_size*partial),
                                    received=ceil(received*partial),
                                    dropped=ceil(dropped*partial),
                                    ipv4=ceil(ipv4*partial),
                                    ipv6=ceil(ipv6*partial),
                                    network_other=ceil(network_other*partial),
                                    tcp=ceil(tcp*partial),
                                    udp=ceil(udp*partial),
                                    transport_other=ceil(transport_other*partial)))
            # Do the next minute
            minute += timezone.timedelta(minutes=1)

        return stat_minutes
