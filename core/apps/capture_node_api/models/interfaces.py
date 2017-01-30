import datetime
import netifaces
import os
import subprocess as sp
import tempfile

from django.db import models
from django.conf import settings


def _get_prop(*args):
    def getter(s):
        return s.read_attr(*args)
    return getter


def _get_int_prop(*args):
    """Returns a getter function for getting a property that's expected to be an integer.
    :return callable:
    """
    def getter(s):
        prop = s.read_attr(*args)
        if prop:
            try:
                return int(prop)
            except ValueError:
                return 0
        else:
            return 0

    return getter


class Interface(models.Model):
    name = models.CharField(max_length=30, null=False, unique=True,
                            help_text="The system name for the interface.")
    target_queues = models.IntegerField(default=1,
                                        help_text="How many queues this interface should have.")
    enabled = models.BooleanField(default=False,
                                  help_text="Whether the interface is set to be enabled for "
                                            "capture at the next restart.")
    running_enabled = models.BooleanField(default=False,
                                          help_text="Whether the interface is currently being used"
                                                    "for capture.")
    when_changed = models.DateTimeField(auto_now=True, help_text="When last changed.")
    when_setup = models.DateTimeField(default=datetime.datetime.fromtimestamp(0),
                                      help_text="When these values were used to set up capture. "
                                                "If this is less than the last time capture was "
                                                "started, the capture system is out of date in "
                                                "regards to this interface configuration.")

    _NET_CLASS_PATH = '/sys/class/net'

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        if self.name is None:
            raise ValueError("Each interface must have a name.")

        self._max_queues = None
        self.sys_path = os.path.join('/sys/class/net', self.name)

    @classmethod
    def get_interfaces(cls):
        """
        Returns a list of interface objects for each interface on the system. If a row already
        exists in the db for this interface, that row object is returned. Otherwise a new
        object is created.
        :return list[Interface]:
        :rtype: list[Interface]
        """

        interfaces = os.listdir(cls._NET_CLASS_PATH)
        ifaces = []

        known_ifaces = {iface.name: iface for iface in cls.objects.all()}

        for iface in interfaces:
            if iface in known_ifaces:
                ifaces.append(known_ifaces[iface])
            else:
                ifaces.append(cls(name=iface))

        return ifaces

    @staticmethod
    def read(*path):
        """
        Read the contents of the file at path. This is meant for reading the contents of
        files in /sys and /proc.
        :return str: File contents as a UTF-8 string. An empty string if the file can't be read.
        """
        try:
            file_path = os.path.join(*path)
            return open(file_path, 'rb').read().strip().decode('utf8')
        except IOError:
            return ''

    def read_attr(self, *path):
        return self.read(self.sys_path, *path)

    def exists(self):
        """
        Returns true if this corresponds to a device that actually exists on the system.
        :return bool:
        """

        return os.path.exists(self.sys_path)

    @property
    def speed(self):
        speed = _get_int_prop('speed')(self)

        # An unset speed may give MAXUINT32
        if speed == 4294967295:
            return 0

        return speed

    @property
    def speed_hr(self):

        speed = self.speed
        if speed == 0:
            return ''

        units = ['Kbps', 'Mbps', 'Gbps']

        unit = units.pop(0)
        while speed >= 1000 and units:
            speed /= 1000
            unit = units.pop(0)

        return '{0:.1f} {1}'.format(speed, unit)

    mac = property(_get_prop('address'))
    dev_id = property(_get_prop('dev_id'))
    dev_port = property(_get_prop('dev_port'))
    duplex = property(_get_prop('duplex'))
    flags = property(_get_prop('flags'))
    iflink = property(_get_prop('iflink'))
    mtu = property(_get_int_prop('mtu'))

    rx_bytes = property(_get_int_prop('statistics', 'rx_bytes'))
    rx_packets = property(_get_int_prop('statistics', 'rx_packets'))
    rx_dropped = property(_get_int_prop('statistics', 'rx_dropped'))
    rx_errors = property(_get_int_prop('statistics', 'rx_errors'))
    tx_bytes = property(_get_int_prop('statistics', 'tx_bytes'))
    tx_packets = property(_get_int_prop('statistics', 'tx_packets'))
    tx_dropped = property(_get_int_prop('statistics', 'tx_dropped'))
    tx_errors = property(_get_int_prop('statistics', 'tx_errors'))

    @property
    def addresses(self):
        """
        Finds all the IP addresses associatied with this interface.
        :return dict:
        """
        try:
            ifaces = netifaces.ifaddresses(self.name)
        except ValueError:
            return {}

        v6 = [a['addr'] for a in ifaces.get(netifaces.AF_INET6, [])]
        v4 = [a['addr'] for a in ifaces.get(netifaces.AF_INET, [])]

        return {'v4': v4, 'v6': v6}

    @property
    def pfring_zc_supported(self):


        return False

    @property
    def driver(self):
        """
        :return: The name of the first driver listed for this interface.
        """
        driver_path = os.path.join(self.sys_path, 'device/driver/module/drivers')

        try:
            drivers = os.listdir(driver_path)
        except FileNotFoundError:
            return None

        return drivers[0]

    _ETHTOOL_PATH = '/sbin/ethtool'

    @property
    def queues(self):
        """Returns the number of receive side scaling (RSS) queues on this interface.
        Returns None if RSS isn't supported."""

        proc = sp.Popen(['/sbin/ethtool', '-l', self.name], stdout=sp.PIPE, stderr=sp.PIPE)
        data = []
        error = []
        new_data = 1
        while proc.poll() is None or new_data:
            new_data = proc.stdout.read(4096)
            data.append(new_data)
            error.append(proc.stderr.read(4096))

        if proc.returncode != 0:
            # This interface probably isn't compatible with multiple queues.
            return None

        data = [d.decode('UTF8') for d in data]

        seen_current = False
        combined = None
        max_combined = None
        for line in ''.join(data).split('\n'):
            if line.startswith('Current hardware settings'):
                seen_current = True
            elif line.startswith('Combined'):
                if not seen_current:
                    try:
                        max_combined = int(line.split()[1])
                    except ValueError:
                        pass
                else:
                    try:
                        combined = int(line.split()[1])
                    except ValueError:
                        pass

        if max_combined is not None and max_combined < 2:
            # This device can have a max of one queue
            return None

        return combined

    @queues.setter
    def queues(self, count):
        """Set up 'count' queues for this interface."""

        if self.queues is None:
            raise RuntimeError("Multi-queue mode not supported for this interface.")

        # They already match, so we don't have to do anything.
        if self.queues == count:
            return

        cmd = ['sudo', self._ETHTOOL_PATH, '-L', self.name, 'combined', str(count)]

        with tempfile.TemporaryFile() as tmpfile:
            if sp.call(cmd, stdout=tmpfile, stderr=tmpfile) != 0:
                tmpfile.seek(0)
                raise RuntimeError("Could not set multi-queue mode", tmpfile.read())

    @property
    def max_queues(self):
        """Return the maximum number of queues for this interface from a pcapDB perspective,
        which is one quarter the number of CPU's."""

        if self.queues is None:
            return 1
        else:
            py = settings.SITE_ROOT/'bin'/'python'
            core_script = settings.SITE_ROOT/'bin'/'core_count'
            cores = sp.call([py, core_script], stdout=sp.DEVNULL,
                            stderr=sp.DEVNULL)

            return cores/4

    def prepare(self):
        """Prepare the interface for capture. If the interface won't be used for capture,
        update its status accordingly to.
        :raises: RuntimeError"""

        # Make sure this interface is up.
        cmd = ['ip', 'link', 'set',
               self.name, 'up',
               'promisc', 'on',
               'mtu', str(settings.MTU)]
        with tempfile.TemporaryFile() as tmpfile:
            if sp.call(cmd, stdout=tmpfile, stderr=tmpfile) != 0:
                tmpfile.seek(0)
                raise RuntimeError("Could not configure interface {}. {}"
                                   .format(self.name, tmpfile.read()))

        #TODO: Properly enable the interface for PFRING ZC

        self.running_enabled = self.enabled
        if self.queues is not None:
            self.queues = self.target_queues
        self.save()
