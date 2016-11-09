import glob
import sys
import os

# This file sits in the project in core/bin/. We need to add
# the path to core/ to the python path
sys.path.append(os.path.dirname(os.path.dirname(__file__)))

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "settings.settings")
import django
django.setup()

from django.conf import settings


def usage():
    print("pcapdb/bin/python bin/clean_indexes.py <search_description>\n"
          "Go through all the indexes listed in the search description (as\n"
          "produced when running a search using the search task), and \n"
          "delete all the cache files from that and other searches.\n")

search_descr = None
try:
    search_descr = open(sys.argv[1], "rb")
except (IndexError, IOError):
    usage()
    exit()

indexes = []

for line in search_descr.readlines():
    if line.startswith(b"FULL") or line.startswith(b"PARTIAL"):
        # Cut out the command name and the result name
        for idx in line.split()[2:]:
            try:
                idx = int(idx)
            except ValueError:
                print("Bad index id:", idx)
                exit()
            idx_path = settings.INDEX_PATH/"{:020d}".format(int(idx))

            for file in glob.glob(str(idx_path) + '/_*'):
                os.unlink(file)