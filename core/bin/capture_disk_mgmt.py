import argparse
import sys

import django
import os

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "settings.settings")

# Get the parent directory for our exectuable.
proj_path = os.path.dirname(os.path.dirname(sys.executable))
path = os.path.join(proj_path, 'core')

if path not in sys.path:
    sys.path.append(path)

django.setup()

import apps.capture_node_api.lib.disk_management as dm

BASE_PATH = '/var/pcapdb/'
ETC_PATH = os.path.join(BASE_PATH, 'etc/')
CMD_PATH = sys.argv[0].split()[0]


def eprint(*args, **kwargs):
    kwargs['file'] = sys.stderr
    print(*args, **kwargs)


def main():
    parser = argparse.ArgumentParser(description="Manage disks for packet capture.")
    sub_parsers = parser.add_subparsers(dest='cmd')
    list_p = sub_parsers.add_parser('list', help='List available storage devices. Filesystem '
                                                 'information for unmounted devices is only '
                                                 'available when run as root.')
    list_p.add_argument('-a', dest='only_avail', action='store_true', default=False,
                        help='Only show disks that are eligible to be added as capture disks.')
    list_p.add_argument('-r', dest='only_raidable', action='store_true', default=False,
                        help='Show only disks eligble for adding to a RAID.')
    raid_p = sub_parsers.add_parser('raid', help='Create a new RAID 5 from available disks.')
    raid_p.add_argument('devices', nargs=argparse.REMAINDER,
                        help='One or four or more device names. (sdb sdc etc...). '
                        'If only one device name is given, similar disks are chosen '
                        'to build a RAID of the given (or default) size. When picking disks '
                        'automatically, only disk within 1% of the size are chosen. Disks on the '
                        'same enclosure are prefered.')
    raid_p.add_argument('-n', '--raid-devices', action='store', default=9, type=int,
                        help='Number of devices from which to build the RAID.')
    write_p = sub_parsers.add_parser('write', help="Write out the mdadm.conf file.")
    write_p.add_argument('--outfile', '-o', action='store', default='/etc/mdadm.conf',
                         help="Where to write the file. Use - for stdout.")
    init_p = sub_parsers.add_parser('init', help='Initialize the given device for use in capture.')
    init_p.add_argument('devices', nargs=argparse.REMAINDER,
                        help='Initialize these devices for capture.')
    add_spare_p = sub_parsers.add_parser('add_spare', help='Make the given disks mdadm spares.')
    add_spare_p.add_argument('devices', nargs=argparse.REMAINDER,
                             help='The devices to add as spares. These will be added to RAIDs that '
                                  'they are compatible with (by size), assuming such RAIDS exist.')
    rem_spare_p = sub_parsers.add_parser('remove_spare', help='Remove the given devices from the '
                                                              'list of spares.')
    rem_spare_p.add_argument('devices', nargs=argparse.REMAINDER,
                             help='The devices to remove as spares.')
    init_idx_p = sub_parsers.add_parser('init_index', help='Denote which disk/s will be used for '
                                                           'the capture system index.')
    init_idx_p.add_argument('devices', nargs=argparse.REMAINDER,
                            help="One or more devices to serve as the index disk. The disks must "
                                 "be of similar size, and not otherwise in use. If two disks "
                                 "than one are given, they are RAID'ed together in a RAID 1 "
                                 "array. additional disks are added as spares to this array.")

    args = parser.parse_args()

    if args.cmd in ('init', 'spare', 'init_index') and not args.devices:
        print("You must specify at least one device.")
        exit(1)

    try:
        if args.cmd == 'list':
            print_block_devices(only_raidable=args.only_raidable, only_avail=args.only_avail)
        elif args.cmd == 'raid':
            dm.make_raid5(args.devices)
            print("Raid creation successful.")
            print("Note: RAID 5 arrays are created in the degraded state with one of the disks "
                  "marked as a spare. The array is then rebuilt in the background, which can take "
                  "several hours.")
        elif args.cmd == 'init':
            for dev in args.devices:
                print("Initializing device {}. This may take a while.")
                dm.init_capture_device(dev, sys.stdout)
        elif args.cmd == 'add_spare':
            dm.add_spare(args.devices)
        elif args.cmd == 'remove_spare':
            dm.remove_spare(args.devices)
        elif args.cmd == 'init_index':
            dm.init_index_device(*args.devices)
        elif args.cmd == 'write':
            dm.write_mdadm_config(args.outfile)
        else:
            parser.print_help()
            exit(1)
    except (ValueError, RuntimeError) as msg:
        print(msg)
        exit(1)


def print_block_devices(only_raidable=False, only_avail=False):
    bd = dm.Device.get_devices()

    raids = []
    disks = []
    for dev in bd.keys():
        if bd[dev].type == dm.Device.MD_TYPE:
            raids.append(dev)
        else:
            disks.append(dev)

    def print_list(items_to_print, item_dict, attributes, headings=None):
        headings = {} if headings is None else headings

        # Figure out how wide each column needs to be
        col_widths = {attr: len(attr) for attr in attributes}
        for col in headings:
            col_widths[col] = len(headings[col])
        col_widths['__name__'] = 0
        for item in items_to_print:
            if len(item) > col_widths['__name__']:
                col_widths['__name__'] = len(item)

            for attr in attributes:
                value = getattr(item_dict[item], attr)
                value = str(value) if value is not None else ''
                v_len = len(value)

                if v_len > col_widths[attr]:
                    col_widths[attr] = v_len

        # Print the column labels
        print(" "*(col_widths['__name__']), end=" ")
        for attr in attributes:
            label = attr if attr not in headings else headings[attr]
            print("{:{width}s}".format(label.capitalize(), width=col_widths[attr]), end=" ")
        print("")

        for item in items_to_print:
            print("{:{width}s}".format(item, width=col_widths['__name__']), end=" ")
            for attr in attributes:
                value = getattr(item_dict[item], attr)
                value = str(value) if value is not None else ''
                print("{:{width}}".format(value, width=col_widths[attr]), end=" ")
            print("")

        return

    hdgs = {'state_cs': 'state', 'size_hr': 'size'}

    if only_raidable:
        disks = [d for d in disks if bd[d].type == dm.Device.DISK_TYPE and not bd[d].state]

    if only_avail:
        raids = [r for r in raids if not bd[r].state]
        disks = [d for d in disks if not bd[d].state]

    if raids and not only_raidable:
        print("MDADM RAIDS")
        print_list(sorted(raids), bd,
                   ['size_hr', 'level', 'count', 'state_cs', 'degraded', 'spares', 'fs',
                    'mountpoint'], headings=hdgs)

    print("\nDISKS")
    print_list(sorted(disks), bd, ['size_hr', 'type', 'state_cs', 'mountpoint', 'fs', 'enclosure',
                                   'slot'], headings=hdgs)

if __name__ == '__main__':
    main()
