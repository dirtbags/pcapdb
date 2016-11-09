from celery import shared_task
import os

from apps.capture_node_api.models.status import InterfaceStats
from apps.capture_node_api.models import Interface
from apps.capture_node_api.serializers import InterfaceSerializer

__author__ = 'pflarr'

IF_SYS_PATH = '/sys/class/net/'


def sys_data(path, fn):
    with open(os.path.join(path, fn), 'r') as file:
        return file.read()


@shared_task(bind=True)
def gather_interface_stats(self):
    """Gather periodic statistics for each interface and store them in the capture_node db."""
    for dev in os.listdir(IF_SYS_PATH):
        path = os.path.join(IF_SYS_PATH, dev)
        stats_path = os.path.join(path, 'statistics')
        stats = InterfaceStats(name=dev,
                               up=sys_data(path, 'operstate') == 'up',
                               bytes=sys_data(stats_path, 'rx_bytes'),
                               packets=sys_data(stats_path, 'rx_packets'),
                               dropped=sys_data(stats_path, 'rx_dropped'))
        stats.save()


@shared_task(bind=True)
def interface_list(self):
    """Fetch a list of all interfaces."""

    ifaces = Interface.get_interfaces()
    return {'data': InterfaceSerializer(ifaces, many=True).data}


@shared_task(bind=True)
def interface_toggle(self, iface):
    """Toggle an interface from capturing to non-capturing mode. Capture restart is required for
    this to actually take effect."""

    interfaces = {i.name: i for i in Interface.get_interfaces()}
    if iface not in interfaces:
        return {'warning': "No such interface: {}".format(iface)}

    iface_ob = interfaces[iface]

    iface_ob.enabled = not iface_ob.enabled
    iface_ob.save()
    msg = "Interface {} is now {}.".format(iface,
                                           'enabled' if iface_ob.enabled else 'disabled')
    if iface_ob.enabled != iface_ob.running_enabled:
        msg += " You must restart capture for this to take effect."

    return {'info': msg, 'data': InterfaceSerializer(iface_ob).data}


@shared_task(bind=True)
def interface_set_queues(self, iface, queues):

    interfaces = {i.name: i for i in Interface.get_interfaces()}
    if iface not in interfaces:
        return {'warning': "No such interface: {}".format(iface)}

    iface_ob = interfaces[iface]

    if queues > iface_ob.max_queues:
        return {'warning': 'This interface and PcapDB support a max of {} queues.'
                           .format(iface_ob.max_queues)}

    iface_ob.target_queues = queues
    iface_ob.save()
    return {'info': 'Updated queue count for interface {}. You must restart capture for this to '
                    'take effect.'.format(iface_ob.name),
            'data': InterfaceSerializer(iface_ob).data}
