import os
import subprocess as sp
from tempfile import TemporaryFile

import sys

if __name__ == '__main__':
    os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'settings.settings')
    proj_path = os.path.dirname(os.path.dirname(sys.executable))
    sys.path.append(os.path.join(proj_path, 'core'))
    import django
    django.setup()

from django.conf import settings
from apps.capture_node_api.models.capture import Index


def check_index(idx_id):
    """
    Check the index <idx_id> to make sure it's sub indexes and flow index are all sane.
    This requires reading the entire FCAP file off of disk, so it's not recommended to do this
    constantly.
    :param idx_id:
    :return:
    """

    idx = Index.objects.get(id=idx_id)

    if idx is None:
        raise RuntimeError("Could not find index: {}".format(idx_id))

    print("Checking index {} sub-indexes: ".format(idx_id), end='')

    with TemporaryFile('w+') as out:
        if sp.call([settings.SITE_ROOT/'bin'/'sub_index_check', idx.path],
                   stdout=out, stderr=out) != 0:
            print('BAD')
            out.seek(0)
            print(out.read())

            return 1

    print('OK')

    if idx.capture_slot is None:
        print("Index does not have a capture slot.")
        return 0

    print("Checking flow index {} against fcap: ".format(idx_id), end='')

    with TemporaryFile('w+') as out:
        if sp.call([settings.SITE_ROOT/'bin'/'flow_index_check',
                    os.path.join(idx.path, 'FLOW'), idx.capture_slot.path],
                   stdout=out, stderr=out) != 0:
            print('BAD')
            print('Capture slot was ', idx.capture_slot.path)
            out.seek(0)
            print(out.read())

            return 1
    print('OK')

    return 0

if __name__ == '__main__':
    import sys

    if len(sys.argv) < 2:
        print("Usage: {} <idx_id> ...".format(sys.argv[0]))
        print("    Check the given index directory for sanity. Each sub-index entry is checked\n"
              "    to make sure it points to a valid flow index entry. Each flow index entry\n"
              "    is similarly checked against the FCAP file. Note that this requires reading\n"
              "    the entire FCAP file.")
        sys.exit(1)

    try:
        ids = map(int, sys.argv[1:])
    except ValueError:
        print("Invalid index id.")
        sys.exit(1)

    for idx_id in ids:
        if check_index(idx_id) != 0:
            sys.exit(1)
