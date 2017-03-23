from collections import defaultdict
import fcntl
import io
import os
import re
import subprocess
import sys
import tempfile

from django.conf import settings

#from celery.utils import log as logging

import logging
log = logging.getLogger(__name__)

#log = logging.task_logger

__author__ = 'pflarr'

SUDO_CMDS = settings.SITE_ROOT/'core'/'bin'/'sudo'
ADD_SPARE_CMD = SUDO_CMDS/'mdadm_add_spare.sh'
CREATE_CMD = SUDO_CMDS/'mdadm_create.sh'
CREATE_INDEX_CMD = SUDO_CMDS/'mdadm_create_index.sh'
DESTROY_CMD = SUDO_CMDS/'mdadm_destroy.sh'
STOP_CMD = SUDO_CMDS/'mdadm_stop.sh'
REMOVE_SPARE_CMD = SUDO_CMDS/'mdadm_remove_spare.sh'
MKXFS_CMD = SUDO_CMDS/'mkfs.xfs.sh'
UDEV_TRIGGER_CMD = SUDO_CMDS/'udev_trigger.sh'
JBOD_LOCATE_CMD = SUDO_CMDS/'JBOD_locate'

BLKID_CMD = '/sbin/blkid'

# /sys/block does not contain block device partitions.
BLOCK_PATH = '/sys/class/block'

# Consider disks to be of similar size if they within this many bytes of size.
SIZE_VARIATION = 1024**3


class Device:
    MD_TYPE = 'MD_RAID'
    DISK_TYPE = 'DISK'
    LVM_TYPE = 'LVM'
    type = None

    # All the devices that we currently know about.
    _DEVICES = {}

    # A dictionary on mount information by device.
    _MOUNT_MAP = None

    # A dictionary mapping device_name -> raid device object.
    _RAID_MAP = {}

    # Disks that host LVM devices
    _LVM_SLAVES = {}

    # Map of device_name -> (enclosure, slot)
    _ENCLOSURE_MAP = {}

    # A dictionary of each enclosures empty slots.
    _EMPTY_SLOTS = defaultdict(lambda: set())

    _SYS_BLK = '/sys/class/block'

    # The attributes to return when asked for a json representation of this Device.
    DICT_ATTRS = ['alias',
                  'dev_path',
                  'enclosure',
                  'fs',
                  'size_hr',
                  'label',
                  'locate',
                  'mountpoint',
                  'name',
                  'size',
                  'slot',
                  'state',
                  'slot_status',
                  'type',
                  'uuid']

    def __init__(self, dev):
        # The size for block devices is in 512 byte blocks
        self.size = int(self.read(self._SYS_BLK, dev, 'size')) * 512
        self.name = dev

        # Some device types have other names
        self.alias = dev

    @property
    def uuid(self):
        # Get the filesystem's UUID using the blkid cmd.
        blkid_cmd = [settings.SUDO_PATH, BLKID_CMD, '-o', 'value', '-s', 'UUID', self.dev_path]
        blkid_proc = subprocess.Popen(blkid_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      universal_newlines=False)
        data = blkid_proc.stdout.read()
        uuid = bytes(data.strip()).decode('utf8')
        if uuid:
            return uuid
        else:
            return None

    @property
    def dev_path(self):
        """
        :return: The /dev path to this device.
        """
        return os.path.join('/dev', self.name)

    _STATE_CODES = {'in_sync': '',
                    'spare': '(S)',
                    'faulty': '(F)'}

    @property
    def enclosure(self):
        """The enclosure in which this device resides."""
        return self._ENCLOSURE_MAP.get(self.name, {}).get('encl')

    @property
    def slot(self):
        """The enclosure slot of this device, if any."""
        return self._ENCLOSURE_MAP.get(self.name, {}).get('slot')

    @property
    def locate(self):
        return self._ENCLOSURE_MAP.get(self.name, {}).get('locate') == '1'

    @locate.setter
    def locate(self, value):
        _set_locate(self, value)
        self._ENCLOSURE_MAP[self.name]['locate'] = self.read('/sys/class/enclosure',
                                                             self.enclosure, self.slot,
                                                             'locate')

    @classmethod
    def locate_clear_all(cls):
        if subprocess.call([settings.SUDO_PATH, JBOD_LOCATE_CMD, 'clear']) != 0:
            raise RuntimeError("Could not clear locate flags.")

    @property
    def state(self):
        """A list of the known conditions for the device."""

        state = []
        if self.mountpoint is not None:
            state.append('mounted')

        if self.list_like(self._SYS_BLK, self.name, r'{}\d+$'):
            # This device appears to have partitions
            state.append('partitioned')

        raid = self._RAID_MAP.get(self.name)
        if raid is not None:
            rdisk_state = raid.disks.get(self.name)
            state.append('In RAID {} ({})'.format(raid.name, rdisk_state))

        parts = self.list_like(self._SYS_BLK, self.name, self.name+'\d+$')
        if parts:
            state.append('partitioned')

        swaps = self.read('/proc/swaps')
        swaps = swaps.split('\n')[1:]
        for line in swaps:
            try:
                dev = line.split()[0].split('/')[-1]
            except IndexError:
                continue
            if dev == self.name:
                state.append('swap')

        if self.name in Device._LVM_SLAVES:
            state.append('lvm_slave')

        return state

    @property
    def slot_status(self):
        if self.enclosure is None or self.slot is None:
            return None

        return self.read('/sys/class/enclosure', self.enclosure, self.slot, 'status')

    @property
    def raid(self):
        return self._RAID_MAP.get(self.name)

    @property
    def uuid_dev_path(self):
        """
        :return: The /dev/disk/by-uuid/ path to this device.
        """
        if not self.uuid:
            raise RuntimeError("Device {.name} has no UUID".format(self))

        return os.path.join('/dev/disk/by-uuid', self.uuid)

    @property
    def size_hr(self):
        """
        :return: A string of the human readible size for the device.
        """
        units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB']
        unit = units.pop(0)
        size = self.size
        while units and size > 1024:
            size /= 1024.0
            unit = units.pop(0)
        return "{0:.2f} {1:s}".format(size, unit)

    @property
    def state_cs(self):
        return ','.join(self.state)

    @property
    def mountpoint(self):
        if self.name in self._MOUNT_MAP:
            return self._MOUNT_MAP[self.name]['mountpoint']
        elif self.type == Device.LVM_TYPE and self.alias in self._MOUNT_MAP:
            return self._MOUNT_MAP[self.alias]['mountpoint']
        else:
            return None

    @property
    def fs(self):
        if self.name in self._MOUNT_MAP:
            return self._MOUNT_MAP[self.name]['fs']
        elif self.type == Device.LVM_TYPE and self.alias in self._MOUNT_MAP:
            return self._MOUNT_MAP[self.alias]['fs']
        elif os.getuid() == 0:
            # If we can't get the filesystem from the mount table, check with blkid.
            blkid_cmd = [settings.SUDO_PATH, BLKID_CMD, '-o', 'value', '-s', 'TYPE', 
                         '-p', self.dev_path]
            blkid_proc = subprocess.Popen(blkid_cmd, stdout=subprocess.PIPE)
            fs = bytes(blkid_proc.stdout.read().strip()).decode('utf8')
            return fs if fs else None

        return None

    @property
    def label(self):
        blkid_cmd = [settings.SUDO_PATH, BLKID_CMD, '-o', 'value', '-s', 'LABEL', self.dev_path]
        blkid_proc = subprocess.Popen(blkid_cmd, stdout=subprocess.PIPE)
        label = blkid_proc.stdout.read().strip()
        label = bytes(label).decode('utf8')

        return label if label else None

    @staticmethod
    def read(*args):
        try:
            file_path = os.path.join(*args)
            return open(file_path, 'rb').read().strip().decode('utf8')
        except IOError:
            return ''

    @staticmethod
    def list_like(*args):
        # All but the last arg are path components, rooted at BLOCK_PATH
        # The last arg is the fnmatch pattern
        file_path = os.path.join(*args[:-1])
        re_str = args[-1] + '$'
        fn_re = re.compile(re_str)
        if not os.path.exists(file_path):
            return []
        return [file for file in os.listdir(file_path) if fn_re.match(file) is not None]

    @classmethod
    def map_enclosures(cls):
        """
        Map the enclosures listed in sysfs, and add that info to the given devices
        :return: None
        """

        cls._ENCLOSURE_MAP = {}
        cls._EMPTY_SLOTS = defaultdict(lambda: set())

        for encl in Device.list_like('/sys/class/enclosure', r'.+'):
            for slot in Device.list_like('/sys/class/enclosure', encl, r'Slot \d+'):
                disks = Device.list_like('/sys/class/enclosure', encl, slot, 'device', 'block',
                                         r'sd\w+')
                locate = Device.read('/sys/class/enclosure', encl, slot, 'locate')
                status = Device.read('/sys/class/enclosure', encl, slot, 'status')
                for disk in disks:
                    cls._ENCLOSURE_MAP[disk] = {'encl': encl,
                                                'slot': slot,
                                                'locate': locate,
                                                'status': status}

                if not disks:
                    cls._EMPTY_SLOTS[encl].add(slot)

    def format_xfs(self, label=None):
        """Format this device with xfs.
        :param label: The label to give the filesystem, if any.
        """
        # Create xfs filesystem
        err = tempfile.TemporaryFile()
        mkfs_cmd = [settings.SUDO_PATH, MKXFS_CMD, self.dev_path]
        if label is not None:
            mkfs_cmd.append(label)

        if subprocess.call(mkfs_cmd, stdout=err, stderr=err) != 0:
            err.seek(0)
            raise RuntimeError("Error formatting device {}: {}".format(self.name, err.read()))

    COMPAT_RE = None

    @classmethod
    def is_compat(cls, dev):
        """Returns true if this class is compatible with the given device."""
        return cls.COMPAT_RE.match(dev) is not None

    @classmethod
    def refresh(cls):
        """Refresh cached disk information. This only applies to mount and fs info, current."""

        devices = set(os.listdir(BLOCK_PATH))
        old_devices = set(cls._DEVICES.keys())

        # Remove any devices that are no longer present.
        for mdev in old_devices - devices:
            del cls._DEVICES[mdev]

        # Add any new devices
        for dev in devices - old_devices:
            if MDRaidDevice.is_compat(dev):
                # This is an mdadm RAID.
                cls._DEVICES[dev] = MDRaidDevice(dev)

            elif DiskDevice.is_compat(dev):
                # This is a regular serial device
                cls._DEVICES[dev] = DiskDevice(dev)

            elif LVMDevice.is_compat(dev):
                cls._DEVICES[dev] = LVMDevice(dev)
            else:
                # We only consider the above device types.
                continue

        cls.map_enclosures()

        # Make a dictionary of mounted disks -> (mount_point, filesystem).
        mounts = {}
        for line in open('/proc/mounts').readlines():
            mdev, mountpoint, fs = line.split()[:3]
            mdev = mdev.split('/')[-1]
            mounts[mdev] = {'mountpoint': mountpoint, 'fs': fs}
        Device._MOUNT_MAP = mounts

        # Gather information from composite devices.
        for dev in cls._DEVICES:
            dev_ob = cls._DEVICES[dev]
            if dev_ob.type == Device.MD_TYPE:
                dev_ob.map_member_devices()
            if dev_ob.type == Device.LVM_TYPE:
                dev_ob.map_slaves()

        cls._clean_raid_map()

    @classmethod
    def _clean_raid_map(cls):
        """Remove any entries in the RAID map for which their RAID no longer exists."""
        for dev in list(cls._RAID_MAP.keys()):
            raid = cls._RAID_MAP[dev]
            if raid.name not in cls._DEVICES:
                del cls._RAID_MAP[dev]

    @classmethod
    def get_devices(cls):
        """Get a dictionary of device_name -> Device_object that includes all disk devices
        that we recognize on the system.
        :rtype: dict[str, Device]
        """
        cls.refresh()
        return cls._DEVICES

    @classmethod
    def find_device_by_uuid(cls, uuid):
        """ Find the device with the given UUID, and return a Device object (or child) for it.
        :return: The device node identified by the given uuid, or None if not found.
        """

        uuid = str(uuid)

        cls.refresh()

        uuid_path = os.path.join('/dev/disk/by-uuid', uuid)
        if not os.path.exists(uuid_path):
            return None

        dev_name = os.path.split(os.path.realpath(uuid_path))[-1]
        return cls._DEVICES.get(dev_name)

    @classmethod
    def get_empty_slots(cls):
        """Return a list of empty slot objects for each empty JBOD slot."""

        cls.refresh()

        empty_slots = []
        for encl in cls._EMPTY_SLOTS:
            for slot in cls._EMPTY_SLOTS[encl]:
                empty_slots.append(EmptySlot(encl, slot))

        return empty_slots

    def as_dict(self):
        d = {}
        for attr in self.DICT_ATTRS:
            val = getattr(self, attr)
            if val is not None:
                d[attr] = val
        return d

    def __getitem__(self, index):
        attr = self.DICT_ATTRS[index]
        return getattr(self, attr)

    def __str__(self):
        return "{}({})".format(self.type, self.dev_path)

    def __repr(self):
        return str(self)


class MDRaidDevice(Device):
    """An MDADM RAID device."""

    type = Device.MD_TYPE
    COMPAT_RE = re.compile(r'md\d+$')

    DICT_ATTRS = Device.DICT_ATTRS + ['degraded',
                                      'level',
                                      'count',
                                      'spares',
                                      'array_state',
                                      'rebuild_status']

    def __init__(self, dev):
        Device.__init__(self, dev)
        self.degraded = False if self.read(self._SYS_BLK, dev, 'md', 'degraded') == '0' else True
        self.level = self.read(self._SYS_BLK, dev, 'md', 'level')
        count = self.read(self._SYS_BLK, dev, 'md', 'raid_disks')
        self.count = int(count) if count else 0
        self.disks = {}
        self.spares = 0

    @property
    def array_state(self):
        """Grab the state value for the array from sys."""
        return self.read('/sys/class/block', self.name, 'md', 'array_state')

    def map_member_devices(self):
        """
        Update our map of device -> RAID for those devices that are part of an
        MDADM raid.
        :return:
        """

        new_map = {}
        self.disks = {}
        self.spares = 0

        raid_disks = self.list_like(self._SYS_BLK, self.name, 'md', 'dev-\w+')
        for disk in raid_disks:
            rdisk_state = self.read(self._SYS_BLK, self.name, 'md', disk, 'state')
            dev_name = disk.split('-', 1)[1]
            new_map[dev_name] = self
            self.disks[dev_name] = rdisk_state
            if rdisk_state == 'spare':
                self.spares += 1

        Device._RAID_MAP.update(new_map)
        for dev in list(Device._RAID_MAP.keys()):
            raid = Device._RAID_MAP[dev]
            if dev not in new_map and raid.name == self.name:
                del Device._RAID_MAP[dev]

    @property
    def rebuild_status(self):
        """Read through /proc/mdstat and get the RAID rebuild status, if it exists."""
        data = self.read('/proc/mdstat')

        line = ''
        lines = data.split('\n')
        while lines:
            line = lines.pop(0)
            if line.startswith(self.name + ' '):
                break

        if lines:
            line = lines.pop(0)
        while lines and not line.startswith('md'):
            line = line.strip()
            if line.startswith('['):
                return line
            line = lines.pop(0)

        return None

class LVMDevice(Device):
    type = Device.LVM_TYPE
    COMPAT_RE = re.compile(r'dm-\d+$')

    def __init__(self, dev):
        Device.__init__(self, dev)
        self.alias = self.read(self._SYS_BLK, dev, 'dm', 'name')

    def map_slaves(self):
        my_old_slaves = {dev for dev, dep in self._LVM_SLAVES.items()
                         if dep.name == self.name}

        my_slaves = {}
        for dev in self.list_like(self._SYS_BLK, self.name, 'slaves', '.*$'):
            my_slaves[dev] = self

        Device._LVM_SLAVES.update(my_slaves)
        for dev in my_old_slaves - set(my_slaves.keys()):
            del Device._LVM_SLAVES[dev]


class DiskDevice(Device):
    type = Device.DISK_TYPE
    COMPAT_RE = re.compile(r'(?:sd|xvd)[a-z]+\d*$')

    def __init__(self, dev):
        Device.__init__(self, dev)


class EmptySlot:
    """This class is for representing empty slots in enclosures."""

    DICT_ATTRS = ['enclosure',
                  'slot',
                  'slot_status',
                  'locate']

    def __init__(self, enclosure, slot):
        self.enclosure = enclosure
        self.slot = slot

    read = Device.read
    slot_status = Device.slot_status

    @property
    def locate(self):
        return self.read('/sys/class/enclosure', self.enclosure, self.slot, 'locate') == '1'

    @locate.setter
    def locate(self, value):
        _set_locate(self, value)

    as_dict = Device.as_dict


class Lockfile:
    LOCK_DIR = settings.SITE_ROOT/'var/spool/lock/'

    def __init__(self, name):
        self.name = name
        self.lockfile = None

    @property
    def path(self):
        return os.path.join(self.LOCK_DIR, self.name)

    def lock(self):
        if not os.path.exists(self.LOCK_DIR):
            os.makedirs(self.LOCK_DIR)

        self.lockfile = open(self.path, 'w')
        if fcntl.lockf(self.lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB):
            raise IOError("Could not obtain disk management lock.\nExiting.")

    def unlock(self):
        if self.lockfile:
            fcntl.flock(self.lockfile, fcntl.LOCK_UN)
            self.lockfile.close()
            self.lockfile = None

    def __enter__(self):
        self.lock()

    def __exit__(self, *args):
        self.unlock()


def _set_locate(dev, value):
    if value:
        value = '1'
    else:
        value = '0'

    with tempfile.TemporaryFile() as tmp:
        if subprocess.call([settings.SUDO_PATH, JBOD_LOCATE_CMD,
                            dev.enclosure, dev.slot, value], stdout=tmp, stderr=tmp) != 0:
            tmp.seek(0)
            raise RuntimeError("Could not set locate flag for {}, {}: {}".format(
                dev.enclosure, dev.slot, tmp.read()
            ))


def _next_md(devices):
    """
    Find an available md device number to use.
    :param devices: A dictionary of device names as returned by block_devices.
    :return: An integer device number.
    """
    md_devices = [disk for disk in devices.keys() if devices[disk].type == Device.MD_TYPE]
    md_nums = [int(re.match('md(\d+)', md).groups()[0]) for md in md_devices]
    md_num = 1
    while md_num in md_nums:
        md_num += 1
    return md_num


def make_raid5(disks, trial_run=False):
    """Make a RAID 5 array from the given set of disks.
    :param bool trial_run: Don't actually create the raid.
    """

    with Lockfile('raid_lock'):

        # Get our block device information.
        bd = Device.get_devices()

        if len(disks) < 4:
            raise RuntimeError("You must specify at least four disks. {} given.".format(len(disks)))

        # Only disks without a current state are eligible (a state
        # implies they are being used for some purpose.
        eligible = []
        for dev in bd.keys():
            if len(bd[dev].state) == 0 and bd[dev].type == Device.DISK_TYPE:
                eligible.append(dev)

        # Make sure all the disks requested are eligible to be added to the RAID.
        for disk in disks:
            if disk not in eligible:
                raise RuntimeError('Disk {} not eligible for being added to a raid.')

        md_num = _next_md(bd)

        # Make the RAID
        mdadm_cmd = [settings.SUDO_PATH, CREATE_CMD, '/dev/md{0:d}'.format(md_num),
                     len(disks), (str(5))]
        for disk in disks:
            mdadm_cmd.append(os.path.join('/dev', disk))

        if trial_run:
            return

        proc = subprocess.Popen(mdadm_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        # Send a 'y' to the mdadm command in case it asks whether it should continue.
        try:
            outs, errs = proc.communicate(input=b"y\n", timeout=30)
        except subprocess.TimeoutExpired:
            raise RuntimeError("Command Timed Out: {}".format(mdadm_cmd))

        if proc.poll() != 0:
            raise RuntimeError("Command Failed: {}, {}".format(outs, errs))


def destroy_raid(dev_name, trial_run=False):
    """Destroy the RAID identified by the """

    bd = Device.get_devices()

    dev_ob = bd.get(dev_name)
    if dev_ob is None:
        raise RuntimeError("No such device: {}.".format(dev_name))

    from apps.capture_node_api.models.capture import Disk
    if dev_ob.mountpoint is not None:
        Disk.umount_uuid(dev_ob.uuid)

    raid_devs = [bd[dev].dev_path for dev in dev_ob.disks.keys()]

    if not trial_run:
        if subprocess.call([settings.SUDO_PATH, STOP_CMD, dev_ob.dev_path]) != 0:
            raise RuntimeError("Could not stop raid {}.".format(dev_name))

        if subprocess.call([settings.SUDO_PATH, DESTROY_CMD] + raid_devs) != 0:
            raise RuntimeError("Could not destroy devices {}.".format(raid_devs))


def init_capture_device(devices, status_file=None, trial_run=False, task=None):
    """Initialize the given device for capture. This involves:
    - Formatting the disk with XFS.
    - Registering the disk with the database.
    - Mounting the disk.
    - Preallocating BUCKET_SIZE files to fill the disk.
    - Registering those buckets with the database.
    :param str dev: The device to initialize as a capture disk.
    :param file status_file: A file like object where status updates should be written. Turned
    off by default.
    :param bool trial_run: Prepare the action, but don't do anything.
    :param task: A celery task for giving progress updates.
    """

    dev = devices[0]

    from apps.capture_node_api.models.capture import Disk

    with Lockfile('init_lock'):
        if status_file:
            print("Initializing {}. This may take a while.".format(dev), file=status_file)

        bd = Device.get_devices()

        if dev not in bd.keys():
            raise RuntimeError("Unknown device: {}".format(dev))

        dev_ob = bd[dev]

        if dev_ob.state:
            # If this device has a state, then it's not eligble to be a capture device.
            raise RuntimeError("Device is not eligible to be a capture because its state is {}"
                               .format(dev_ob.state))

        if task is not None:
            task.update_state(state='WORKING', meta={'msg': 'Formatting device. This may take a '
                                                            'while.',
                                                     'progress': 5})

        if status_file:
            print("Formatting {} as xfs.".format(dev_ob.dev_path), file=status_file)
        if not trial_run:
            dev_ob.format_xfs()

        if task is not None:
            task.update_state(state='WORKING', meta={'msg': 'Finished formating.',
                                                     'progress': 25})

        # Make sure this disk has a UUID now.
        if not dev_ob.uuid:
            raise RuntimeError("Could not get uuid of recently formatted disk.")

        if status_file:
            print("Adding {} to database.".format(dev_ob.dev_path), file=status_file)

        # Add the disk to the capture database.
        # It's added as disabled so we don't try to use it right away.
        disk = Disk(uuid=dev_ob.uuid, mode='DISABLED', size=dev_ob.size)

        # Figure out which disk is the biggest so we can reset the usage_inc for each disk.
        # When trying to figure out which disk to use next, capture chooses the disk with
        # the lowest 'usage'. After using it, the disk's usage is increased by the usage_inc.
        # If all disks are the same size, their usage_inc's should all be about 1.
        # If they're of different sizes, the increment will be larger for the smaller disks,
        # so that those disks will be used less frequently.
        max_size = max([d.size for d in Disk.objects.all()] + [dev_ob.size])
        disk.usage_inc = max_size/disk.size

        if not trial_run:
            for d in Disk.objects.all():
                # Reset the usage_inc and usage for all disks. If we don't do this, the new
                # disk will be used exclusively until it catches up in usage to the old disks.
                d.usage_inc = max_size/disk.size
                d.usage = 0
                d.save()

            disk.save()

        if task is not None:
            task.update_state(state='WORKING', meta={'msg': 'Device mounted and dded device to '
                                                            'the system.',
                                                     'progress': 30})

        if status_file:
            print("Mounting {} and filling it with capture slots.".format(dev_ob.dev_path),
                  file=status_file)

        # Fill our new capture disk with capture slots. This also mounts it.
        if not trial_run:
            try:
                disk.populate(task)
            except RuntimeError:
                disk.umount()
                disk.delete()
                raise

        if status_file:
            print("Finished initializing {}.".format(dev_ob.dev_path), file=status_file)

def uninitialize_device(dev, status_file=None):

    bd = Device.get_devices()

    if dev not in bd.keys():
        pass


def _compat_raids(dev, devices):
    """
    Find all the compatable RAID arrays for the given device.
    :param dev: The device we're comparing to.
    :param devices: The dict of device attributes from Device.get_devices()
    :return: A list of compatible raid device names.
    """
    dev_attr = devices[dev]

    compat_raids = set()
    for cdev in devices.keys():
        cattr = devices[cdev]
        # Only consider actual disks. (Though these may not actually be disks at present, could be
        # LUNs. Also, the disk must be in an mdadm RAID.
        if cattr.type == Device.DISK_TYPE and cattr.raid is not None:
            if abs(dev_attr.size - cattr.size) < SIZE_VARIATION:
                compat_raids.add(cattr.raid.name)

    return compat_raids


def add_spare(spares):
    """Denote a disk as an active spare for mdadm RAID.
    - The spare is added to a RAID with similar disks.
    - It will be shared amongst RAIDS with similar disks.
    :param bool trial_run: Do everything but run the command.
    """

    devices = Device.get_devices()

    for spare in spares:
        if spare not in devices:
            raise RuntimeError("No such device: {}.".format(spare))
        dev_ob = devices[spare]

        if dev_ob.state:
            raise RuntimeError("Disk {} already in use: {}.".format(spares, dev_ob.state))

        compat_raids = _compat_raids(spare, devices)
        if not compat_raids:
            raise RuntimeError("No compatible RAID arrays exist.")

        # Add the spare to an arbitrary RAID.
        raid = list(compat_raids)[0]

        err = tempfile.TemporaryFile()

        cmd = [settings.SUDO_PATH, ADD_SPARE_CMD, devices[raid].dev_path, dev_ob.dev_path]
        log.error('cmd: {}'.format(cmd))
        if subprocess.call(cmd, stdout=err, stderr=err) != 0:
            err.seek(0)
            raise RuntimeError("Could not add spare: {}".format(err.read()))


def remove_spare(spares, trial_run=False):
    """Remove the given spare device from the appropriate RAID, if possible.
    This should not be done if the RAID is degraded, as it may be copying data to the spare.
    :param bool trial_run: Do everything but run the command."""

    devices = Device.get_devices()

    for spare in spares:
        if spare not in devices:
            raise RuntimeError("No such device: {}.".format(spare))
        dev_ob = devices[spare]

        raid = dev_ob.raid
        if raid is not None:
            rdisk_state = raid.disks.get(dev_ob.name)
            if rdisk_state != 'spare':
                raise RuntimeError("Device {} is not a spare.".format(dev_ob.name))
        else:
            raise RuntimeError("Device {} is not in a RAID.".format(dev_ob.name))

        if raid.degraded:
            raise RuntimeError("Raid device {} (of which {} is a member) is degraded. "
                               "Spares should not be removed.".format(raid.name, dev_ob.name))

        if trial_run:
            return

        err = tempfile.TemporaryFile()
        cmd = [settings.SUDO_PATH, REMOVE_SPARE_CMD, raid.dev_path, dev_ob.dev_path]
        if subprocess.call(cmd, stdout=err, stderr=err) != 0:
            err.seek(0)
            raise RuntimeError("Could not remove spare: {}".format(err.read()))


def write_mdadm_config(outfile='/etc/mdadm.conf'):
    """
    Write out an mdadm config for the detected RAID devices.
    :param str outfile: Where the config will be written.
    """
    md = Device.get_devices()

    raids = [md[dev] for dev in md.keys() if md[dev].type == Device.MD_TYPE]

    conf = io.StringIO()

    def write(line):
        conf.write(line)
        conf.write('\n')

    write('# This configuration is generated automatically by PcapDB. It should not be '
          'edited manually.\n')
    write('# Send alerts here. This depends on mdadm monitor running in scan mode.')
    if hasattr(settings, 'disk_admin'):
        write('MAILADDR {}'.format(settings.disk_admin))
    else:
        write('# MAILADDR (Could not find an address in settings.disk_admin)')
    write('')
    write('# Automatically search for devices in /proc/partitions.')
    write('DEVICE partitions')
    write('')

    # Figure out what spare groups we need.
    spare_groups = {}
    for raid in raids:
        for disk in raid.disks.keys():
            found = False
            for group in spare_groups:
                if abs(group - md[disk].size) < SIZE_VARIATION:
                    spare_groups[group].add(raid)
                    found = True
                    break
            if not found:
                spare_groups[md[disk].size] = {raid}

    # We need to create a mapping from raid to spare_groups, and make sure our groups make sense
    raid_groups = {}
    for group in spare_groups:
        for raid in spare_groups[group]:
            if raid in raid_groups and group != raid_groups[raid]:
                raise RuntimeError("Raid has disks in multiple spare groups.")
            else:
                raid_groups[raid] = group

    # We need more sane names than the raw size of the disk.
    spare_group_names = {}
    i = 0
    for group in sorted(spare_groups.keys()):
        spare_group_names[group] = 'group{0:d}'.format(i)
        i += 1

    # Now we can finally add the ARRAY lines to the config file.
    for raid in raids:
        sg_name = spare_group_names[raid_groups[raid]]
        if raid.uuid is not None:
            write('ARRAY {raid.dev_path} level={raid.level} num-devices={raid.count:d} '
                  'UUID={raid.uuid} spares={raid.spares} spare-group={spare_group}'.format(
                      raid=raid, spare_group=sg_name))
        else:
            write('ARRAY {raid.dev_path} level={raid.level} num-devices={raid.count:d} '
                  'spares={raid.spares} spare-group={spare_group}'.format(
                      raid=raid, spare_group=sg_name))

    conf.seek(0)
    if outfile == '-':
        sys.stdout.write(conf.read())
    else:
        conf_file = open(outfile, 'w')
        conf_file.write(conf.read())
        conf_file.close()


def init_index_device(*devices, task=None):
    """
    Initialize a set of disks to serve as our index device.
     - There can only be one index device.
     - Two disks are expected and combined as a RAID 1, though a single disk can be used as well.
     - Additional disks are added as spares.
    :param devices: The disks to include in the RAID.
    :param task: celery.Task
    :return: None
    :raises: ValueError, RuntimeError
    """

    bd = Device.get_devices()
    dev_obs = []

    # Make sure the disks are suitable for use.
    for dev in devices:
        if dev not in bd:
            raise RuntimeError("Can't find disk: {}.".format(dev))
        dev_ob = bd[dev]
        if dev_ob.state:
            raise RuntimeError("Disk {} already in use: {]".format(dev, dev_ob.state))

        dev_obs.append(dev_ob)

    # Make sure the disks are reasonably close in size.
    base = dev_obs[0].size
    for dev_ob in dev_obs[1:]:
        if abs(dev_ob.size - base) >= SIZE_VARIATION:
            raise RuntimeError("Disk sizes are too different. {d1.dev_path} - {d1.size_hr()}, "
                               "{d2.dev_path} - {d2.size_hr()}".format(d1=base, d2=dev_ob))

    if not devices:
        raise ValueError("You must specify at least one devices.")

    md_num = _next_md(bd)
    # Create a RAID 1, but with only one workind disk.
    mdadm_cmd = [settings.SUDO_PATH, CREATE_INDEX_CMD, '/dev/md{0:d}'.format(md_num),
                 dev_obs[0].dev_path]
    log.info('create index raid command:{}'.format(mdadm_cmd))

    proc = subprocess.Popen(mdadm_cmd, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    outs, errs = proc.communicate(input=b'y', timeout=5)
    if proc.poll() != 0:
        raise RuntimeError("Raid creation failed: {}.".format(errs))

    if len(dev_obs) == 2:
        # Now add the second disk as a spare
        mdadm_add_cmd = [settings.SUDO_PATH, ADD_SPARE_CMD, '/dev/md{0:d}'.format(md_num),
                         dev_obs[1].dev_path]
        proc = subprocess.Popen(mdadm_add_cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, stdin=subprocess.PIPE)
        outs, errs = proc.communicate(input=b'y', timeout=5)
        if proc.poll() != 0:
            raise RuntimeError("Fail/ed to add second device: {}.".format(errs))

    if task is not None:
        task.update_state(state='WORKING', meta={'msg': 'Formatting device. This may take a '
                                                        'while.',
                                                 'progress': 5})

    index_device = MDRaidDevice('md{0:d}'.format(md_num))

    index_device.format_xfs(label=settings.INDEX_DEV_LABEL)

    return index_device


def find_index_device():
    """
    Find the device that should be our index device by label. Note that this there is
    a delay between filesystem creation and the appearance of the label.
    :return: None or Device
    :rtype: None or Device
    """

    subprocess.call([settings.SUDO_PATH, UDEV_TRIGGER_CMD],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    idx_dev = None
    for dev_ob in Device.get_devices().values():
        if dev_ob.label == settings.INDEX_DEV_LABEL:
            if idx_dev is None:
                idx_dev = dev_ob
            else:
                raise RuntimeError("Multiple index devices. {} and {}."
                                   .format(dev_ob.uuid, idx_dev.uuid))

    return idx_dev

