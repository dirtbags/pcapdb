import os
import shutil
import subprocess
import tempfile
import time
from django.conf import settings
from django.db import models

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'

SUDO_CMDS = settings.SITE_ROOT/'core'/'bin'/'sudo'
MOUNT_UUID_CMD = SUDO_CMDS/'mount_by_uuid'


class Disk(models.Model):

    ACTIVE = 'ACTIVE'
    DISABLED = 'DISABLED'
    REPAIRING = 'REPAIRING'
    CAPTURE_DEVICE_MODES = {ACTIVE: 'A device that is currently being used to store '
                                    'capture data.',
                            DISABLED: 'A device not currently suitable for capture or '
                                      'search.',
                            REPAIRING: 'A device under repair. Capture data can be read,'
                                       'but none should be written.'}

    UMOUNT_CMD = '/bin/umount'
    MOUNT_CMD = '/bin/mount'
    LSOF_PATH = '/usr/sbin/lsof'

    uuid = models.CharField(max_length=36)
    # What mode the disk is in relative to our system.
    mode = models.CharField(max_length=10, choices=[(m, m) for m in CAPTURE_DEVICE_MODES.keys()])
    # What enclosure this disk was last seen on.
    added = models.DateTimeField('Date Added', auto_now_add=True)
    # How many bytes this disk holds
    size = models.BigIntegerField()
    # How many times smaller this disk is than the largest in the system.
    # usage_inc = largest_disk_size/this_disk_size
    usage_inc = models.FloatField(default=0)
    # The disk with this least usage is used next. Usage is reset when disks a new
    # ACTIVE disk is added.
    usage = models.DecimalField(default=0, max_digits=25, decimal_places=2)

    def activate(self):
        """Put this disk in active mode."""

        if not self.mounted:
            self.mount()

        self.mode = self.ACTIVE
        self.save()

    def disable(self):
        """Put this disk in DISABLED mode.
        :return: None
        """

        self.mode = self.DISABLED
        self.save()

        # We don't unmount the disk; it's most likely busy.

    @property
    def uuid_dev(self):
        """Path the the disk's device by uuid."""
        return os.path.join('/dev/disk/by-uuid', self.uuid)

    @property
    def dev_name(self):
        return os.path.split(os.path.realpath(self.uuid_dev))[-1]

    @property
    def mountpoint(self):
        """Where this disk is expected to be mounted."""
        return settings.CAPTURE_PATH/self.uuid

    @property
    def device(self):
        """The device's dev name. ie: sda"""
        dev_path = os.path.realpath(self.uuid_dev)
        return dev_path.split('/')[-1]

    @property
    def mounted(self):
        """
        :return: True if the disk is mounted in the correct place, False otherwise.
        """

        # Search /proc/mounts for this disk.
        for line in open('/proc/mounts').readlines():
            mdev, mountpoint, fs = line.split()[:3]
            mdev = mdev.split('/')[-1]
            if mdev == self.device and mountpoint == self.mountpoint:
                return True

        return False

    def mount(self):
        """Attempt to mount this disk if it isn't mounted already.
        Raises RuntimeError's on failure.
        """
        
        if self.mounted:
            return

        if not os.path.exists(self.mountpoint):
            os.mkdir(self.mountpoint)

        self.mount_uuid(self.uuid)

    @staticmethod
    def mount_uuid(uuid, is_index=False):
        cmd = [settings.SUDO_PATH, MOUNT_UUID_CMD, str(uuid)]
        if is_index:
            cmd.append('-i')
        with tempfile.TemporaryFile() as err:
            ret = subprocess.call(cmd, stdout=err, stderr=err)

            if ret != 0:
                err.seek(0)
                log.error(err.read())
                raise RuntimeError("Error mounting disk {}.".format(uuid))
        

    def umount(self):
        """Attempt to un-mount this capture disk."""

        if self.mode == self.ACTIVE:
            raise RuntimeError("Cannot unmount, disk is still in ACTIVE state.")

        if self.mounted:
            self.umount_uuid(self.uuid)

    @staticmethod
    def umount_uuid(uuid):
        """
        Attempt to unmount this disk.
        :param uuid: The uuid string of the mounted disk.
        """

        uuid_path = os.path.join('/dev/disk/by-uuid', uuid)
        dev_path = os.path.realpath(uuid_path)

        mount_lines = open('/etc/mtab').read().split('\n')
        mount_points = []
        for line in mount_lines:
            parts = line.split()
            if parts and parts[0] == dev_path:
                mount_points.append(parts[1])
        if not mount_points:
            raise RuntimeError("Device {} is not currently mounted.".format(dev_path))

        with tempfile.TemporaryFile() as err:
            if subprocess.call([settings.SUDO_PATH, Disk.UMOUNT_CMD, uuid_path],
                               stdout=err, stderr=err) != 0:
                err.seek(0)
                raise RuntimeError("Could not un-mount: {}".format(err.read()))

    def populate(self, task=None, progress_bounds=(0, 100)):
        """
        Fill this disk with fallocated capture files, if it hasn't already been.
        :param task: An optional celery task to send periodic updates to.
        :param progress_bounds: The base and target progress values when reporting status.
        :return: None
        """

        # Make sure we're mounted
        self.mount()

        # Update the status every two seconds
        update_period = 2
        last_update = time.time()

        expected_slots = shutil.disk_usage(self.mountpoint).free//settings.FCAP_SIZE
        slots_made = 0

        # Fill the disk with capture slots.
        while shutil.disk_usage(self.mountpoint).free > settings.FCAP_SIZE:
            slot = CaptureSlot(disk=self)
            slot.save()
            try:
                slot.fallocate()
            except OSError:
                # This can happen, occasionally. Just make sure we are somewhat near our expected
                # slot count.
                if expected_slots - slots_made > 10:
                    raise RuntimeError("Did not make sufficient slots.")
                else:
                    break

            slots_made += 1
            if task is not None and time.time() - update_period > last_update:
                # Linearly increase the progress according to the number of slots we've created,
                # starting at the first progress bound until we're finished and reach the second
                # bound.
                progress = progress_bounds[0] + (slots_made/expected_slots)*(progress_bounds[1] -
                                                                             progress_bounds[0])
                task.update_state(state="WORKING", meta={'msg': 'Creating capture slots.',
                                                         'progress': progress})
                last_update = time.time()


class CaptureSlot(models.Model):
    # The disk on which this slot resides
    disk = models.ForeignKey(Disk)
    # When this slot was last used. This is the same as the start time for
    # the data saved here, or when the slot was added.
    used = models.DateTimeField('Date Added', auto_now_add=True)

    @property
    def fn(self):
        """The filename of this slot."""
        return os.path.join(self.disk.mountpoint,
                            "p{0:0{len:d}d}.fcap".format(self.id, len=settings.SLOT_NAME_LEN))

    @property
    def path(self):
        """The expected path to this slot."""
        return os.path.join(self.disk.mountpoint, self.fn)

    @property
    def exists(self):
        return os.path.exists(self.path)

    def fallocate(self):
        """
        Attempts to allocate this capture slot, if it doesn't already exist.
        :return: None
        """

        with open(self.fn, 'wb') as file:
            os.posix_fallocate(file.fileno(), 0, settings.FCAP_SIZE)


class Index(models.Model):
    """Describes an index for one FCAP file worth of captured traffic."""
    # The id field for this model is part of the folder name for the indexes
    # on disk. Filename: "i%020d"
    start_ts = models.DateTimeField(help_text="The timestamp of the first packet indexed.")
    end_ts = models.DateTimeField(help_text="The timestamp of the last packet indexed.")
    capture_slot = models.ForeignKey(CaptureSlot, null=True,
                                     help_text="The capture slot that this index indexes.")
    ready = models.BooleanField(default=False,
                                help_text="Is this ready for search?")
    readied = models.DateTimeField(null=True,
                                   help_text="When was this index tagged as 'ready'")
    expired = models.BooleanField(default=False,
                                  help_text="Set when the index is slated to be deleted.")

    @property
    def path(self):
        """The path to this index directory."""
        return settings.INDEX_PATH/'{:020d}'.format(self.id)

    def size(self):
        """The total size of the contents of this directory."""

        idx_path = self.path
        total = 0
        try:
            for file in os.listdir(idx_path):
                file_path = os.path.join(idx_path, file)
                if os.path.isfile(file_path) and not os.path.islink(file_path):
                    # We don't want to include symlinks
                    # (or directories, though there should never be any).
                    total += os.path.getsize(file_path)
        except FileNotFoundError:
            return 0

        return total

    def delete(self, **kwargs):
        """Remove all the files associated with this index, and then delete it from
        the db."""

        try:
            shutil.rmtree(self.path)
        except FileNotFoundError:
            log.warning("Tried to delete index at {}, but the directory no longer "
                        "existed.".format(self.path))
        super().delete(**kwargs)


class Stats(models.Model):
    """Represents the statistics for the packets in a single FCAP file."""
    # The interface capture occurred on.
    interface = models.CharField(max_length=129)
    # Total data in capture file (headers + packets)
    capture_size = models.BigIntegerField()
    # Number of IPv4 Packets
    ipv4 = models.IntegerField()
    # Number of IPv6 Packets
    ipv6 = models.IntegerField()
    # Other network layer protocol count
    network_other = models.IntegerField()

    # Total Packets Received according to libpcap
    received = models.BigIntegerField()
    # Packets dropped by libpcap
    dropped = models.IntegerField()

    # The related index
    index = models.OneToOneField(Index, models.CASCADE)


class TransportStats(models.Model):
    """There are as many as 256 possible transports, though we should only see a few."""
    transport = models.SmallIntegerField()
    count = models.BigIntegerField()
    stats = models.ForeignKey(Stats, null=False)


class ErrorStats(models.Model):
    """Hopefully, the normal case should be such that these values are all zero. In such
    a case, we won't create rows of this table."""
    # Count of locally dropped packets
    dropped = models.IntegerField()

    # Errors when parsing the Data Link Layer
    dll = models.IntegerField()
    # Errors when parsing the Network Layer
    network = models.IntegerField()
    # Errors when parsing the Transport Layer
    transport = models.IntegerField()
    # Related Index
    stats = models.ForeignKey(Stats, null=False)
