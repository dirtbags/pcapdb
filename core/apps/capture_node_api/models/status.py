import datetime
import subprocess

import os
import psutil
import tempfile
from django.conf import settings
from django.db import models
from django.utils import timezone

from apps.capture_node_api.lib import disk_management as disk_man
from apps.capture_node_api.models.capture import Disk
from apps.capture_node_api.models.interfaces import Interface
from libs.model_singleton import SingletonModel

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'


class Status(SingletonModel):
    """This keeps track of the running state of the capture server and its settings.
    It also holds everything needed to start and restart capture.

    Note that this is a singleton table that should have only one record. Use .load() to get that
    record or create it if it doesn't already exist.
    """

    MODE_LIBPCAP = 'libpcap'
    MODE_PFRING = 'pfring'
    MODE_PFRINGZC = 'pfring-zc'
    CAPTURE_MODES = ((MODE_LIBPCAP, 'libPCAP'),
                     (MODE_PFRING, 'PFring'),
                     (MODE_PFRINGZC, 'PFring-ZC'))
    STARTED = 'STARTED'
    RESTART = 'RESTART'
    STOPPED = 'STOPPED'
    CAPTURE_STATES = ((STARTED, STARTED),
                      (STOPPED, STOPPED),
                      (RESTART, RESTART))

    capture = models.CharField(max_length=max([len(s[0]) for s in CAPTURE_STATES]),
                               default=STOPPED,
                               help_text="Whether capture should be running.")
    running_message = models.CharField(max_length=200,
                                       default='',
                                       help_text='The last message we got when starting/stopping/'
                                                 'restarting capture.')
    capture_state_changed = models.DateTimeField(null=True,
                                                 help_text="When the server was last started.")
    pid = models.IntegerField(null=True, default=None,
                              help_text="The pid of the capture process.")
    index_uuid = models.UUIDField(null=True, default=None,
                                  help_text="UUID of the device that holds our indexes.")
    capture_mode = models.CharField(choices=CAPTURE_MODES, max_length=10, null=False,
                                    default='pfring-zc',
                                    help_text="The capture library to try to use.")
    last_stats_upload = models.DateTimeField(null=True,
                                             help_text="The last time we uploaded capture "
                                                       "statistics to the search head.")
    # Note that this needs to be changed manually
    settings_changed = models.BooleanField(default=True)

    NOT_OK = 'NOT_OK'
    OK = 'OK'
    REPAIRING = 'REPAIRING'
    DISABLED = 'DISABLED'

    # We need to keep track of when settings change. These columns don't hold settings
    # though, so they don't need to be watched.
    MAINT_ATTRS = ['id',
                   'pid',
                   'started',
                   'settings_changed',
                   'running_message',
                   'capture',
                   'last_stats_upload']

    def __setattr__(self, key, value):
        """We watch certain settings for changes so that we can note when our entry has been
        altered.
        """
        if key not in self.MAINT_ATTRS:
            self.settings_changed = True

        super().__setattr__(key, value)

    @property
    def is_capture_node(self):
        """Returns True if this is an capture_node host, False otherwise."""
        return settings.IS_CAPTURE_NODE

    @property
    def is_search_head(self):
        """Returns True if this is an search head host, False otherwise."""
        return settings.IS_SEARCH_HEAD

    @property
    def capture_status(self):
        """Returns the rudamentary state of capture. While this tells you whether the system is
        capturing, it can't tell you if it's capturing what you intended.

        :return: A tuple of the capture state (NOT_CAPTURING, CAPTURING or NEEDS_RESTART)
        followed by a message.
        :rtype: (str, str)
        """

        ifaces = Interface.get_interfaces()

        capture_ifaces = []

        if self.pid is None:
            return self.NOT_OK, "No known capture process."

        if self.settings_changed:
            return self.RESTART, "General capture settings have changed."

        pid_dir = os.path.join('/proc', '{:d}'.format(self.pid))
        if not os.path.exists(pid_dir):
            return self.NOT_OK, "Capture isn't running (pid {})".format(self.pid)
        else:
            readlink = []
            if os.getuid() != 0:
                readlink.append(settings.SUDO_PATH)
            readlink.extend(['/bin/readlink', '-f', os.path.join(pid_dir, 'exe')])
            proc = subprocess.Popen(readlink, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            outs, err = proc.communicate(input="")
            if proc.poll() != 0:
                log.error("Could not check process link path: {}".format(readlink))
            outs = outs.decode('utf8').strip()
            if outs != settings.CAPTURE_CMD:
                if outs.endswith("(deleted)") and outs.split()[0] == settings.CAPTURE_CMD:
                    return self.RESTART, "The capture program has been updated. Restart needed."
                else:
                    return self.NOT_OK, "Capture isn't running. ({} != {})".format(outs, 
                            settings.CAPTURE_CMD)

        # If the number of queues or the the enabled status doesn't match up with
        # what is currently being used, return NEEDS_RESTART
        for iface in ifaces:
            if (iface.enabled != iface.running_enabled or
                    iface.enabled and iface.queues != iface.target_queues):
                return self.RESTART, "Interface changes necessitate a restart."

            if iface.enabled:
                # If it's enabled and without weird changes, the device is capturing.
                capture_ifaces.append(iface.name)

        if capture_ifaces:
            return self.OK, "Capturing on {}.".format(', '.join(capture_ifaces))
        else:
            return self.NOT_OK, "No enabled interfaces."

    @property
    def index_disk_status(self):
        """
        :return: A tuple of the index_disk state and a message.
        :rtype: (str, str)
        """

        if self.index_uuid is None:
            return self.NOT_OK, "No known index device."

        idx_dev = disk_man.Device.find_device_by_uuid(self.index_uuid)

        if idx_dev is None:
            return self.NOT_OK, "Could not find index device: {}".format(self.index_uuid)

        if idx_dev.mountpoint is None:
            return self.NOT_OK, "Index device is not mounted."
        elif idx_dev.mountpoint != settings.INDEX_PATH:
            return self.NOT_OK, "Index device mounted in the wrong location: {} instead of {}".format(
                idx_dev.mountpoint, settings.INDEX_PATH
            )

        try:
            with tempfile.TemporaryFile(dir=idx_dev.mountpoint) as tmp:
                tmp.write(b'hello world')
        except PermissionError:
            return self.NOT_OK, "Cannot write to mounted index device."

        return self.OK, "Index disk ok."

    @property
    def capture_disk_status(self):
        """
        Returns the status of the capture disk
        :return: A tuple of status (DISK_OK, DISK_REPAIRING, or DISK_BAD) and a message
        :rtype: (str, str)
        """

        disks = Disk.objects.all()

        if not disks:
            return self.NOT_OK, "No known capture disks."

        for disk in disks:
            device = disk_man.Device.find_device_by_uuid(disk.uuid)

            if device is None:
                return self.NOT_OK, "Capture disk {} is missing.".format(disk.uuid)

            if disk.mode == 'REPAIRING':
                return self.REPAIRING, "Capture disk {} is under repair.".format(disk.uuid)

            if disk.mode == 'DISABLED':
                return self.DISABLED, "Capture disk {} is disabled.".format(disk.uuid)

            if not disk.mounted:
                return self.NOT_OK, "Disk {} not mounted correctly".format(disk.uuid)

        return self.OK, "All capture disks are ok."

    def start_capture(self):
        """Start/restart the capture system with the current parameter set.
        :return: status
        :rtype: str
        """

        if os.getuid() != 0:
            log.warning("Process trying to start capture must be root.")
            return self.NOT_OK

        # Make sure we're not already running, if we are, this becomes a restart
        cap_status = self.capture_status[0]
        if cap_status in (self.OK, self.RESTART):
            self.stop_capture()

        # Make sure our index disk is ok
        if self.index_disk_status[0] != self.OK:
            # Something's not right with the index disk.
            # Trying to mount it will either fix the problem or reveal it.
            idx_dev_status, idx_dev_msg = self.mount_index_device()
            if idx_dev_status != self.OK:
                self.running_message = idx_dev_msg
                self.save()
                return self.NOT_OK

        # Make sure all of our capture disks are fine.
        if self.capture_disk_status[0] != self.OK:
            # Something's not right with our capture disks.
            # As long as we can mount each of the ACTIVE and REPAIRING disks though,
            # we're fine.

            cap_disks = Disk.objects.all()
            for disk in cap_disks:
                try:
                    if disk.mode in [disk.ACTIVE, disk.REPAIRING]:
                        disk.mount()
                except RuntimeError as err:
                    self.running_message = 'Could not mount capture disk {}: {}'\
                                         .format(disk.uuid_dev, err)
                    self.save()
                    return self.NOT_OK

        capture_cmd = [settings.CAPTURE_CMD]

        if self.capture_mode == self.MODE_LIBPCAP:
            capture_cmd.append('-l')
        elif self.capture_mode == self.MODE_PFRING:
            capture_cmd.append('-n')
        elif self.capture_mode == self.MODE_PFRINGZC:
            capture_cmd.append('-z')

        # Get the various interfaces ready for capture.
        interfaces = Interface.objects.filter(enabled=True)
        if not interfaces:
            self.running_message = 'No interfaces are enabled.'
            self.save()
            return self.NOT_OK

        for iface in interfaces:
            iface.prepare()
            queue_count = iface.queues
            if (queue_count is not None and queue_count > 1 and
                    self.capture_mode in (self.MODE_PFRING, self.MODE_PFRINGZC)):

                # When using pfring we can specify an interface per device queue
                for q in range(queue_count):
                    capture_cmd.extend(['-i', '{}@{:d}'.format(iface.name, q)])
            else:
                capture_cmd.extend(['-i', iface.name])

        capture_cmd.extend(['-u', settings.CAPTURE_USER])
        capture_cmd.extend(['-g', settings.CAPTURE_GROUP])

        log.info('Starting capture with command: {}'.format(' '.join(capture_cmd)))

        env = os.environ.copy()
        env['LD_LIBRARY_PATH'] = '/usr/local/lib'
        env['SITE_ROOT'] = str(settings.SITE_ROOT)
        proc = subprocess.Popen(capture_cmd, close_fds=True,
                                env=env)

        # Make sure the process doesn't die immediately.
        try:
            ret = proc.wait(timeout=0.5)
            self.running_message = "Capture process failed to start, returned {}".format(ret)
            self.save()
            return self.NOT_OK
        except subprocess.TimeoutExpired:
            # This is what we hope will happen
            pass

        self.pid = proc.pid

        # Update the interface objects whose current state didn't match
        # their running state.
        for iface in Interface.objects.exclude(enabled=models.F('running_enabled')):
            log.debug("iface recently enabled: {}".format(iface.name))
            iface.running_enabled = iface.enabled
            iface.save()

        self.capture_state_changed = timezone.now()
        self.settings_changed = False
        self.capture = self.STARTED
        self.running_message = "Capture started successfully."
        self.save()

        return self.OK

    def stop_capture(self):
        """
        Tell any existing capture processes to die gently. They will finish doing whatever they
        were doing, but the they will pretty much immediately close their open capture interfaces
        and free all the memory they can.
        :return: status
        :rtype: str
        """

        if os.getuid() != 0:
            log.warning("Process trying to stop capture should be root.")
            return self.NOT_OK

        cmd = ['/bin/kill', '-15', str(self.pid)]

        # Send all known capture pids the TERM signal. This should put them into soft shutdown
        # mode.
        with tempfile.TemporaryFile() as err:
            if subprocess.call(cmd, stdout=err, stderr=err) != 0:
                err.seek(0)
                return self.NOT_OK, "Could stop capture: {}".format(err)

        self.capture_state_changed = timezone.now()
        self.running_message = "Stopped capture"
        self.save()

        return self.OK

    def mount_index_device(self):
        """Mount our index device in the correct location."""

        if self.index_uuid is None:
            return self.NOT_OK, "No known index device."

        index_dev = disk_man.Device.find_device_by_uuid(self.index_uuid)

        if index_dev is None:
            return self.NOT_OK, "Index device {} has disappeared.".format(self.index_uuid)

        mount_point = settings.INDEX_PATH
        if index_dev.mountpoint is not None and index_dev.mountpoint != mount_point:
                # The disk is mounted in the wrong place somehow. Try to unmount it.
                try:
                    Disk.umount_uuid(self.index_uuid)
                except RuntimeError as err:
                    return self.NOT_OK, 'Index device {} mounted incorrectly, ' \
                                        'but could not unmount: {}'.format(self.index_uuid, err)

        # Try to mount the index disk.
        try:
            Disk.mount_uuid(self.index_uuid, is_index=True)
        except RuntimeError as err:
            return self.NOT_OK, 'Index device {} could not ' \
                                'be mounted: {}'.format(self.index_uuid, err)

        return self.OK, 'Mounted successfully'


class InterfaceStats(models.Model):
    """For keeping track of the state of interfaces on the capture_node."""
    name = models.CharField(max_length=30,
                            help_text="The name of the interface.")
    up = models.BooleanField(help_text="Whether or not the interface was up.")
    bytes = models.BigIntegerField(help_text="How many bytes of data the interface had received at"
                                             "this point.")
    packets = models.BigIntegerField(help_text="How many packets the interface had received.")
    dropped = models.BigIntegerField(help_text="How many packets the interface has been dropped.")
    when = models.DateTimeField(auto_now_add=True)
