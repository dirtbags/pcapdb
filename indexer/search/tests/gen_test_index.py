#!env python3

from random import randint, randrange, random, getrandbits
from functools import reduce
from datetime import datetime, timezone
import os


# Must be stored in network order
IPV4_PRIVATE_PREFIX = bytes([192,168])

# Must be stored in network order
# Used randomly generated Global ID B0:E379:C9C8 and Subnet 376A
IPV6_PRIVATE_PREFIX = bytes([0xFD,0xB0,0xE3,0x79,0xC9,0xC8,0x37,0x6A])

KEY_TYPES = {
    "port": {"number": 1, "size": 2},
    "ipv4": {"number": 2, "size": 4},
    "ipv6": {"number": 3, "size": 16},
    "flow": {"number": 4, "size": 77},
}


def header(key_type, start_ts, end_ts, record_count, version=1, offset_size=4,
           index_name="test_", shortcut=0):
    """Generate and return a bytearray, ready to be written to disk, of the
    header section of the index.

    start_ts and end_ts must be (int, int) tuples representing seconds since
    epoch (1970-01-01 00:00:00) and microseconds, respectively.

    record_count, version, offset_size, and shortcut must all be one-byte
    (<=256), positive integers.  Further, though not enforced, currently valid
    values are as follows:
    version: 1
    offset_size: 4, 8

    The shortcut depth (shortcut) should be chosen such that:
    shortcut <= log2((4096-(header size)) / (key size))
    """

    if index_name == "test_":
        index_name = "test_" + key_type

    if len(index_name) > 15:
        raise IndexError(
            'string overflow: index_name must be 15 characters or less (was %d)'
            % len(index_name))

    ident_binary = bytearray("FCAP_INDEX", "ascii") # definitely 10 bytes
    # I know that byte order doesn't matter on elements of one byte, but...
    version_binary = version.to_bytes(1, "little")
    key_type_binary = KEY_TYPES[key_type]["number"].to_bytes(1, "little")
    offset_size_binary = offset_size.to_bytes(1, "little")

    index_name_binary = bytearray(16) # Automagically null-terminated! Bonus!
    index_name_binary[:len(index_name)] = bytearray(index_name, "ascii")
    start_ts_binary = (start_ts[0].to_bytes(8, "little")
                       + start_ts[1].to_bytes(8, "little"))
    end_ts_binary = (end_ts[0].to_bytes(8, "little")
                       + end_ts[1].to_bytes(8, "little"))
    record_count_binary = record_count.to_bytes(8, "little")
    shortcut_binary = shortcut.to_bytes(1, "little")

    header_binary = (ident_binary + version_binary + key_type_binary
            + offset_size_binary + index_name_binary + start_ts_binary
            + end_ts_binary + record_count_binary + shortcut_binary)
    header_padded_binary = bytearray(4096)
    header_padded_binary[:len(header_binary)] = header_binary
    return header_padded_binary


# Could / should probably make this a full-fledged class, but...
def record(key_offset_tuple, key_type, offset_size=4):
    key, offset = key_offset_tuple
    key_binary = bytes(0)

    if key_type == "flow":
        # For the test, there will only be real values for the first_ts
        # timestamp; the rest will be null bytes
        key_binary += (
            # first_ts.tv_secs             first_ts.tv_usecs
            key[0].to_bytes(8, "little") + key[1].to_bytes(8, "little")
            # last_ts     proto      src_ip      dest_ip     src_port
            + bytes(16) + bytes(1) + bytes(16) + bytes(16) + bytes(2)
            # dest_port  size       packets
            + bytes(2) + bytes(4) + bytes(4))
    elif key_type == "port":
        key_binary += key.to_bytes(length=2, byteorder="big")
    else:
        # IPs should already be bytearrays
        key_binary += key

    offset_binary = offset.to_bytes(length=offset_size, byteorder="little")

    return key_binary + offset_binary


def port_generator():
    """A generator for keys (type port) that will skip some keys and repeat
    most, just for the sake of a good spread of keys.  Completely random
    offsets are generated.
    """
    min_ = randint(1,100)
    max_ = randint(min_, 65535)
    cur = min_
    off = randrange(4294967296)

    while cur < max_:
        while random() < 0.85:
            yield (cur, off)
            off = randrange(off, 4294967296)
        cur += 1
        off = randrange(4294967296)


def ipv4_generator():
    """A generator for keys (type ipv4) that will skip some keys and repeat
    most, just for the sake of a good spread of keys.  Completely random
    offsets are generated.
    """
    min_ = randint(1,100)
    max_ = randint(min_, 100000)
    cur = min_
    off = randrange(4294967296)

    while cur < max_:
        while random() < 0.7:
            yield (IPV4_PRIVATE_PREFIX + cur.to_bytes(2, "big"), off)
            off = randrange(off, 4294967296)
        cur += 1
        off = randrange(4294967296)


def ipv6_generator():
    """A generator for keys (type ipv6) that will skip some keys and repeat
    most, just for the sake of a good spread of keys.  Completely random
    offsets are generated.
    """
    min_ = randint(1,100)
    max_ = randint(min_, 100000)
    cur = min_
    off = randrange(4294967296)

    while cur < max_:
        while random() < 0.7:
            yield (IPV6_PRIVATE_PREFIX + cur.to_bytes(8, "big"), off)
            off = randrange(off, 4294967296)
        cur += 1
        off = randrange(4294967296)


def flow_generator():
    """A generator for keys (type flow, or, rather, timestamp) that will skip
    some keys and repeat most, just for the sake of a good spread of keys.
    Completely random offsets are generated.
    """
    min_ = randrange(946684800, 983404800, 5400)
    max_ = randrange(min_, 1262304000, 5400)
    cur = min_
    cur_us = randrange(999999)
    off = randrange(4294967296)

    while cur < max_:
        while random() < 0.7:
            yield ((cur, cur_us), off)
            off = randrange(off, 4294967296)
        cur += 5400
        cur_us = randrange(1000000)
        off = randrange(4294967296)


def add(x, y):
    return x + y


def main():

    # Yep, these generators are, in fact, creating lists in memory.  Wheeee!
    port_keys = list(port_generator())
    ipv4_keys = list(ipv4_generator())
    ipv6_keys = list(ipv6_generator())
    flow_keys = list(flow_generator())

    port_test_atom = port_keys[randrange(len(port_keys))][0]
    ipv4_test_atom = ipv4_keys[randrange(len(ipv4_keys))][0]
    ipv6_test_atom = ipv6_keys[randrange(len(ipv6_keys))][0]
    flow_test_atom = flow_keys[randrange(len(flow_keys))][0]

    port_test_range_start_index = randrange(len(port_keys) // 2)
    port_test_range_start = port_keys[port_test_range_start_index][0]
    port_test_range_end = port_keys[randrange(port_test_range_start_index,
                                              len(port_keys))][0]
    ipv4_test_range_start_index = randrange(len(ipv4_keys) // 2)
    ipv4_test_range_start = ipv4_keys[ipv4_test_range_start_index][0]
    ipv4_test_range_end = ipv4_keys[randrange(ipv4_test_range_start_index,
                                              len(ipv4_keys))][0]
    ipv6_test_range_start_index = randrange(len(ipv6_keys) // 2)
    ipv6_test_range_start = ipv6_keys[ipv6_test_range_start_index][0]
    ipv6_test_range_end = ipv6_keys[randrange(ipv6_test_range_start_index,
                                              len(ipv6_keys))][0]
    flow_test_range_start_index = randrange(len(flow_keys) // 2)
    flow_test_range_start = flow_keys[flow_test_range_start_index][0]
    flow_test_range_end = flow_keys[randrange(flow_test_range_start_index,
                                              len(flow_keys))][0]

    port_test_atom_results = [y for x, y in port_keys if x == port_test_atom]
    ipv4_test_atom_results = [y for x, y in ipv4_keys if x == ipv4_test_atom]
    ipv6_test_atom_results = [y for x, y in ipv6_keys if x == ipv6_test_atom]
    flow_test_atom_results = [y for x, y in flow_keys if x == flow_test_atom]


    port_test_range_results = [y for x, y in port_keys
                               if x >= port_test_range_start
                               and x <= port_test_range_end]
    ipv4_test_range_results = [y for x, y in ipv4_keys
                               if x >= ipv4_test_range_start
                               and x <= ipv4_test_range_end]
    ipv6_test_range_results = [y for x, y in ipv6_keys
                               if x >= ipv6_test_range_start
                               and x <= ipv6_test_range_end]
    flow_test_range_results = [y for x, y in flow_keys
                               if x >= flow_test_range_start
                               and x <= flow_test_range_end]


    port_test_atom_results.sort()
    ipv4_test_atom_results.sort()
    ipv6_test_atom_results.sort()
    flow_test_atom_results.sort()
    port_test_range_results.sort()
    ipv4_test_range_results.sort()
    ipv6_test_range_results.sort()
    flow_test_range_results.sort()


    port_test_atom_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in port_test_atom_results])
    ipv4_test_atom_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in ipv4_test_atom_results])
    ipv6_test_atom_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in ipv6_test_atom_results])
    flow_test_atom_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in flow_test_atom_results])

    port_test_range_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in port_test_range_results])
    ipv4_test_range_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in ipv4_test_range_results])
    ipv6_test_range_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in ipv6_test_range_results])
    flow_test_range_results_binary = reduce(add, [x.to_bytes(4, "little") for x
                                                 in flow_test_range_results])


    port_records = list(map(lambda x: record(x, "port"), port_keys))
    ipv4_records = list(map(lambda x: record(x, "ipv4"), ipv4_keys))
    ipv6_records = list(map(lambda x: record(x, "ipv6"), ipv6_keys))
    flow_records = list(map(lambda x: record(x, "flow"), flow_keys))


    port_records_binary = bytes(reduce(add, port_records))
    ipv4_records_binary = bytes(reduce(add, ipv4_records))
    ipv6_records_binary = bytes(reduce(add, ipv6_records))
    flow_records_binary = bytes(reduce(add, flow_records))


    port_header = header("port", (946684800, 0), (4102444799, 999999),
                         len(port_keys))
    ipv4_header = header("ipv4", (946684800, 0), (4102444799, 999999),
                         len(ipv4_keys))
    ipv6_header = header("ipv6", (946684800, 0), (4102444799, 999999),
                         len(ipv6_keys))
    flow_header = header("flow", flow_keys[0][0], flow_keys[-1][0],
                         len(flow_keys))

    with open("test_port", "wb") as fd:
        fd.write(port_header + port_records_binary)
        fd.close()

    with open("test_ipv4", "wb") as fd:
        fd.write(ipv4_header + ipv4_records_binary)
        fd.close()

    with open("test_ipv6", "wb") as fd:
        fd.write(ipv6_header + ipv6_records_binary)
        fd.close()

    with open("test_flow", "wb") as fd:
        fd.write(flow_header + flow_records_binary)
        fd.close()


    offset_size = 4


    with open("test_port_atom_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + port_test_atom_results_binary)
        fd.close()

    with open("test_ipv4_atom_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + ipv4_test_atom_results_binary)
        fd.close()

    with open("test_ipv6_atom_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + ipv6_test_atom_results_binary)
        fd.close()

    with open("test_flow_atom_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + flow_test_atom_results_binary)
        fd.close()


    with open("test_port_range_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + port_test_range_results_binary)
        fd.close()

    with open("test_ipv4_range_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + ipv4_test_range_results_binary)
        fd.close()

    with open("test_ipv6_range_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + ipv6_test_range_results_binary)
        fd.close()

    with open("test_flow_range_results_py", "wb") as fd:
        fd.write(offset_size.to_bytes(1, "little")
                 + flow_test_range_results_binary)
        fd.close()


    with os.fdopen(os.open("test.sh", os.O_WRONLY | os.O_CREAT, 0o0755),
                   "w", 1) as fd:
        fd.write('#!/bin/sh' + "\n\n")
        fd.write("../bin/search_atom test_port 1 {0} test_port_atom_results_c\n"
                 .format(port_test_atom))
        fd.write("../bin/search_atom test_ipv4 2 {0[0]}.{0[1]}.{0[2]}.{0[3]}"
                 " test_ipv4_atom_results_c\n".format(ipv4_test_atom))
        fd.write("../bin/search_atom test_ipv6 3 {0[0]:02X}{0[1]:02X}:"
                 "{0[2]:02X}{0[3]:02X}:{0[4]:02X}{0[5]:02X}:"
                 "{0[6]:02X}{0[7]:02X}:{0[8]:02X}{0[9]:02X}:"
                 "{0[10]:02X}{0[11]:02X}:{0[12]:02X}{0[13]:02X}:"
                 "{0[14]:02X}{0[15]:02X} test_ipv6_atom_results_c\n"
                 .format(ipv6_test_atom))
        fd.write("../bin/search_atom test_flow 4 \"{0}.{1}\""
                 " test_flow_atom_results_c\n".format(
                     datetime.fromtimestamp(flow_test_atom[0], tz=timezone.utc)
                     .strftime("%Y-%m-%d %H:%M:%S"), flow_test_atom[1]
                 ))
        fd.write("../bin/search_range test_port 1 {0} {1}"
                 " test_port_range_results_c\n"
                 .format(port_test_range_start, port_test_range_end))
        fd.write("../bin/search_range test_ipv4 2 {0[0]}.{0[1]}.{0[2]}.{0[3]}"
                 " {1[0]}.{1[1]}.{1[2]}.{1[3]} test_ipv4_range_results_c\n"
              .format(ipv4_test_range_start, ipv4_test_range_end))
        fd.write("../bin/search_range test_ipv6 3 {0[0]:02X}{0[1]:02X}:"
                 "{0[2]:02X}{0[3]:02X}:{0[4]:02X}{0[5]:02X}:"
                 "{0[6]:02X}{0[7]:02X}:{0[8]:02X}{0[9]:02X}:"
                 "{0[10]:02X}{0[11]:02X}:{0[12]:02X}{0[13]:02X}:"
                 "{0[14]:02X}{0[15]:02X} {1[0]:02X}{1[1]:02X}:"
                 "{1[2]:02X}{1[3]:02X}:{1[4]:02X}{1[5]:02X}:"
                 "{1[6]:02X}{1[7]:02X}:{1[8]:02X}{1[9]:02X}:"
                 "{1[10]:02X}{1[11]:02X}:{1[12]:02X}{1[13]:02X}:"
                 "{1[14]:02X}{1[15]:02X} test_ipv6_range_results_c\n"
                 .format(ipv6_test_range_start, ipv6_test_range_end))
        fd.write("../bin/search_range test_flow 4 \"{0}.{1}\" \"{2}.{3}\""
                 " test_flow_range_results_c\n"
                 .format(
                     datetime.fromtimestamp(flow_test_range_start[0],
                                            tz=timezone.utc)
                     .strftime("%Y-%m-%d %H:%M:%S"),
                     flow_test_range_start[1],
                     datetime.fromtimestamp(flow_test_range_end[0],
                                            tz=timezone.utc)
                     .strftime("%Y-%m-%d %H:%M:%S"),
                     flow_test_range_end[1]
                 ))
        for x in ['atom', 'range']:
            for y in KEY_TYPES.keys():
                fd.write("cmp test_{0}_{1}_results_c test_{0}_{1}_results_py\n"
                         .format(y, x))
        fd.flush()
        os.fsync(fd)


if __name__ == "__main__":
    main()
