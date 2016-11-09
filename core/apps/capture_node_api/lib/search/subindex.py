import binascii
import struct
import datetime
import ipaddress
import pytz
from collections import namedtuple, UserList

__author__ = 'pflarr'

# This provides a parser for sub-index files for debugging and testing.
# You can run it as a script to simply print the contents of a subindex file

def read_ipv6(d):
    """Read an IPv6 address from the given file descriptor."""
    u, l = struct.unpack('>QQ', d)
    return ipaddress.IPv6Address((u << 64) + l)

PreviewNode = namedtuple('PreviewNode', ['value', 'left', 'right'])

class Flow:
    def __init__(self, data, keep_hex=False):
        (first_s, first_us,
         last_s, last_us,
         srcip, srcvers,
         proto, srcport,
         packets,
         dstip, dstvers,
         powers, dstport,
         flowsize) = struct.unpack(FLOW_FILE_FMT, data)

        self.first_ts = first_s + first_us/10**6
        self.last_ts = last_s + last_us/10**6

        if srcvers == 4:
            self.srcip = ipaddress.IPv4Address(srcip[:4])
        elif srcvers == 6:
            self.srcip = ipaddress.IPv6Address(srcip)
        else:
            self.srcip = 'no_ip'

        if dstvers == 4:
            self.dstip = ipaddress.IPv4Address(dstip[:4])
        elif dstvers == 6:
            self.dstip = ipaddress.IPv6Address(dstip)
        else:
            self.dstip = 'no_ip'

        self.first_ts = first_s + first_us/10**6
        self.last_ts = first_s + first_us/10**6
        self.srcport = srcport
        self.dstport = dstport
        self.proto = proto
        self.size = flowsize
        self.packets = packets
        if keep_hex:
            self.hex = binascii.hexlify(data)
        else:
            self.hex = None

    @property
    def first_dt(self):
        return pytz.UTC.localize(datetime.datetime.fromtimestamp(self.first_ts))

    @property
    def last_dt(self):
        return pytz.UTC.localize(datetime.datetime.fromtimestamp(self.last_ts))

    def __str__(self):
        return '{0.first_ts} {0.last_ts} {0.srcip}:{0.srcport} ' \
               '{0.dstip}:{0.dstport} {0.proto} {0.size} ' \
               '{0.packets}'.format(self)


FLOW_FILE_FMT = ('<'    # This is (mostly) little endian
                 'II'   # first_ts              0,1
                 'II'   # last_ts               2,3
                 '16s'  # Source IP v4/v6       4
                 'B'    # Source IP version     5
                 'B'    # Transport Proto       6
                 'H'    # Source Port           7
                 'I'    # Total Packets         8
                 '16s'  # Dest. IP v4/v6        9
                 'B'    # Dest. IP version      10
                 'B'    # Size/Packets Power    11
                 'H'    # Dest Port             12
                 'I')   # Flow Size             13


class SubIndex(UserList):
    FORMAT = 'ccccBBHIIIIQ'
    DATA_LEN = struct.calcsize(FORMAT)

    TYPES = ['FLOW',
             'SRCv4',
             'DSTv4',
             'SRCv6',
             'DSTv6',
             'SRCPORT',
             'DSTPORT']

    KEYSIZES = {
        'FLOW': 64,
        'SRCv4': 4,
        'DSTv4': 4,
        'SRCv6': 16,
        'DSTv6': 16,
        'SRCPORT': 2,
        'DSTPORT': 2
    }

    KEYREADERS = {
        'FLOW': Flow,
        'SRCv4': lambda d: ipaddress.IPv4Address(struct.unpack('>I', d)[0]),
        'DSTv4': lambda d: ipaddress.IPv4Address(struct.unpack('>I', d)[0]),
        'SRCv6': read_ipv6,
        'DSTv6': read_ipv6,
        'SRCPORT': lambda d: struct.unpack('<H', d)[0],
        'DSTPORT': lambda d: struct.unpack('<H', d)[0],
    }

    def __init__(self, filename):
        """Unpack the header data and fill out header attributes.
        :param filename: Path to a subindex file
        :return:
        """

        super().__init__()

        file = open(filename, 'rb')

        header_data = file.read(self.DATA_LEN)
        unpacked = struct.unpack(self.FORMAT, header_data)

        self.ident = unpacked[0] + unpacked[1] + unpacked[2] + unpacked[3]
        self.version = unpacked[4] & 0x7f
        self.offset64 = unpacked[4] & 0x80
        self.key_type = self.TYPES[unpacked[5]]
        self.preview = unpacked[6]
        self.start_ts = datetime.datetime.utcfromtimestamp(unpacked[7] + unpacked[8]/10**6)
        self.end_ts = datetime.datetime.utcfromtimestamp(unpacked[9] + unpacked[10]/10**6)
        self.records = unpacked[11]

        reader = self.KEYREADERS[self.key_type]
        key_len = self.KEYSIZES[self.key_type]
        if self.preview != 0:
            preview_data = file.read(4096 - self.DATA_LEN)

            preview_keys = []

            for i in range(self.preview):
                pos = i * key_len
                preview_keys.append(reader(preview_data[pos:pos+key_len]))

            for node in preview_keys:
                print(node)

        self._records = []
        self._len = 0
        
        offset_len = 4 #4 if not self.offset64 else 8
        offset_format = '<I'# if not self.offset64 else 'Q'
        index = 1
        data = file.read(key_len + offset_len)
        while data:
            try:
                key = reader(data[:key_len])
                offset = struct.unpack(offset_format, data[key_len:])[0]
                self.append((key, offset))
            except:
                print('Bad data{}, key_len', data, key_len)
                raise

            data = file.read(key_len + offset_len)
            index += 1

if __name__ == '__main__':

    import sys

    subindex = SubIndex(sys.argv[1])

    for key, offset in subindex:
        print(key, offset)
