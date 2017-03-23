import os
import time

from celery import shared_task
from django.conf import settings

from apps.capture_node_api.lib import disk_management as dm
from apps.capture_node_api.models import Disk
from apps.capture_node_api.models.status import Status
from apps.capture_node_api.serializers import DiskSerializer

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'

IF_SYS_PATH = '/sys/class/net/'


def sys_data(path, fn):
    with open(os.path.join(path, fn), 'r') as file:
        return file.read()


STATUS_DEV_TYPES = ('disks', 'empty_slots', 'arrays')

@shared_task(bind=True)
def list(self):
    """Return a list of disks suitable for display in a client side DataTables table.
    :param Request request: A request object
    :param str dev_type: ({})""".format(', '.join(STATUS_DEV_TYPES))

    # Refresh our catalog of devices.
    dm.Device.refresh()

    dev_status = Status.load()

    host_devices = dm.Device.get_devices().values()
    log.info('host_devices: {}'.format(host_devices))

    known_uuids = [disk.uuid for disk in Disk.objects.all()]

    empty_slots = [dev.as_dict() for dev in dm.Device.get_empty_slots()]
    # All the disks that aren't initialized.
    devices = []
    init_devices = []
    for dev in host_devices:
        if dev.uuid in known_uuids:
            init_devices.append(dev.as_dict())
        else:
            devices.append(dev.as_dict())

    for device in init_devices:
        if 'uuid' in device:
            try:
                disk_row = Disk.objects.get(uuid=device['uuid'])
                device['disk'] = DiskSerializer(disk_row).data
            except Disk.DoesNotExist:
                pass
            if 'label' in device and \
               device['label'] == settings.INDEX_DEV_LABEL:
                if device['uuid'] == str(dev_status.index_uuid):
                    if 'mountpoint' in device:
                        device['index_dev_status'] = 'ACTIVE INDEX DEVICE'
                    else:
                        device['index_dev_status'] = 'UNMOUNTED INDEX DEVICE'
                else:
                    device['index_dev_status'] = 'UNREGISTERED INDEX DEVICE'

    return {'data': {'empty_slots': empty_slots, 'devices': devices, 'init_devices': init_devices}}


@shared_task(bind=True)
def spare_set(self, devices):
    """Set the given devices as spares in compatible RAIDS."""

    try:
        dm.add_spare(devices)
    except RuntimeError as err:
        return {'warning': 'Could not add spare: {}'.format(err)}

    return {'success': 'Added {} Spares'.format(len(devices))}


@shared_task(bind=True)
def spare_remove(self, devices):
    """Remove the devices as spares."""

    try:
        dm.remove_spare(devices)
    except RuntimeError as err:
        return {'warning': 'Could not remove spare: {}'.format(err)}

    return {'success': 'Removed {} Spares'.format(len(devices))}


ACTIVATION_ACTIONS = ['activate', 'deactivate']

@shared_task(bind=True)
def activation(self, devices, action):
    """Enable or disable the given device for capture. It must already be initialized."""

    device = devices[0]

    known_devices = dm.Device.get_devices()

    dev = known_devices[device]

    try:
        dev_row = Disk.objects.get(uuid=dev.uuid)
    except Disk.DoesNotExist:
        return {'warning': 'Unregistered device: {}'.format(dev.name)}

    if action == 'activate':
        dev_row.activate()
    elif action == 'deactivate':
        dev_row.disable()

    return {'success': 'Device {} {}d successfully.'.format(device, action)}


@shared_task(bind=True)
def destroy_raid(self, devices):
    """Destroy the given RAID device, permanently.
    :param devices: A single item list of the device whose RAID is to be destroyed."""

    known_devices = dm.Device.get_devices()

    device = devices[0]

    if device not in known_devices.keys():
        return {'warning': 'Unknown device: {}'.format(device)}

    dev = known_devices[device]
    try:
        dev_row = Disk.objects.get(uuid=dev.uuid)
        if dev_row.mode == dev_row.ACTIVE:
            return {'warning': 'You must deactivate device {} before destroying it.'
                               .format(dev.name)}
    except Disk.DoesNotExist:
        # It's ok if the hasn't been initialized.
        pass

    try:
        dm.destroy_raid(dev.name)
    except RuntimeError as err:
        return {'warning': str(err)}

    return {'success': 'Raid device {} destroyed.'.format(device)}


@shared_task(bind=True)
def disk_locate_toggle(self, enclosure, slot):
    """
    Toggle the locate bit for the enclosure and slot given.
    :param Request request:
    :return:
    """

    # Grab the list of devices and find the device that matches this request.
    devices = dm.Device.get_devices().values()
    device = None
    for dev in devices:
        if dev.enclosure == enclosure and dev.slot == slot:
            device = dev
            break

    if device is None:
        devices = dm.Device.get_empty_slots()
        for dev in devices:
            if dev.enclosure == enclosure and dev.slot == slot:
                device = dev
                break

    if device is None:
        return {'warning': 'Device at {},{} could not be found'
                           .format(enclosure, slot)}

    # Toggle the locate flag.
    try:
        device.locate = not device.locate
    except RuntimeError:
        return {'warning': 'Error setting locate for {},{}'.format(enclosure, slot)}

    return {'locate': device.locate}


@shared_task(bind=True)
def make_raid5(self, disks):
    """Create a RAID 5 array from the given disks.
    :param self: This is run as a method.
    :param disks: [str]
    """

    dm.make_raid5(disks)

    print(disks)
    return {'msg': "Raid created successfully from {}.".format(', '.join(disks))}


@shared_task(bind=True)
def make_index_device(self, disks):
    """
    Take two disks to make into the index disk. They are put into a RAID 1
    configuration, formatted and mounted.
    :param self: This is run as a method.
    :param disks: [str]
    :return:
    """

    if len(disks) not in [1,2]:
        raise RuntimeError("You must provide one or two disks")

    # Make sure we don't already have an index device.
    status = Status.load()
    if status.index_uuid is not None and dm.Device.find_device_by_uuid(status.index_uuid):
        raise RuntimeError("Index device already exists.")

    if status.index_uuid is None:
        # If there is a device with the index_device label, always use it.
        idx_dev = dm.find_index_device()
        if idx_dev is not None:
            status.index_uuid = idx_dev.uuid
            status.save()
            raise RuntimeError("Index device already exists.")

    self.update_state(state="WORKING")

    idx_dev = dm.init_index_device(*disks, task=self)
    while idx_dev.uuid is None:
        time.sleep(1)
    status.index_uuid = idx_dev.uuid
    status.save()

    result, msg = status.mount_index_device()
    if result != status.OK:
        raise RuntimeError(msg)

    return {'msg': "Index disk created and initialized successfully."}


@shared_task(bind=True)
def init_capture_device(self, device):
    """Initialize the given device for capture.
    :param self: This is run as a method.
    :param str device: The device to initialize."""

    self.update_state(state="WORKING")

    try:
        dm.init_capture_device(device, task=self)
    except BlockingIOError:
        return {'msg': "Cannot initialize more than one device at once."}

    return {'msg': "Device {} initialized successfully.".format(device)}
