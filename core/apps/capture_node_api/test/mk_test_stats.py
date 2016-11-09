import os

from apps.capture_node_api.models.capture import Index, Stats, TransportStats

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'settings.settings')

import sys
sys.path.append(os.getcwd())

from django.utils import timezone
from datetime import timedelta
import random
import time

import math

MEAN = 5*60
STD_DEV = 3*60
BASE = 1*60

now = timezone.now()
duration = timedelta(days=10)
start = now - duration

period = (60**2 * 24)/(2 * math.pi)

def fcap_len(ts):
    secs = time.mktime(ts.timetuple())

    return random.normalvariate(BASE+MEAN+MEAN*math.sin(secs/period), STD_DEV)

drops = 0

while start < now:

    fcap_period = 0
    while fcap_period <= 0:
        fcap_period = fcap_len(start)
    print(fcap_period/60)

    end = start + timedelta(seconds=fcap_period)

    index = Index(start_ts=start,
                  end_ts=end, capture_slot=None, ready=False)
    index.save()

    drops += random.normalvariate(0, 100)

    packets = 90000 + int(10000*random.random())
    ipv4 = int((0.6 + 2 * random.random()) * packets)
    ipv6 = 0.99 * (packets - ipv4)
    other_net = packets - ipv4 - ipv6

    stats = Stats(capture_size=4*(1024**3) - random.randint(0,10000),
                  ipv4=ipv4,
                  ipv6=ipv6,
                  network_other=other_net,
                  received=packets,
                  dropped=drops,
                  index=index)
    stats.save()

    # Make about 70% of the packets tcp.
    tcp = int((0.6 + 2 * random.random()) * packets)
    # Make UDP 95% of what's left.
    udp = int(.9 * (packets - tcp))
    # The last bit is some other transport
    other = packets - tcp - udp

    TransportStats(transport=6, count=tcp, stats=stats).save()
    TransportStats(transport=17, count=udp, stats=stats).save()
    TransportStats(transport=37, count=other, stats=stats).save()

    start = end
