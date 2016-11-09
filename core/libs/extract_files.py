#! /usr/bin/python3

import subprocess
import re
import sys
import os


def usage():
    print('./extract_file.py <filename>')
    sys.exit()


def main(argv):
    p1 = subprocess.Popen(["tcpxtract", "--file", argv[1]], stdout=subprocess.PIPE)
    output = p1.communicate()[0]

    d = {}

    export_pattern = re.compile(b'exporting\sto ')
    export_file = export_pattern.split(output)
    for i in range(1, len(export_file)):
        file_end = re.compile(b'\n')
        file_name = file_end.split(export_file[i])
        readable_name = str(file_name[0], "utf-8")
        size = os.path.getsize(readable_name)
        file_type_process = subprocess.Popen(["file", "-b", readable_name], stdout=subprocess.PIPE)
        file_type = str(file_type_process.communicate()[0].splitlines()[0], "utf-8")
        file_values = dict([('filename', readable_name), ('size', size), ('file type', file_type)])
        d[i - 1] = file_values
        print(d[i-1])


if __name__ == "__main__":
    main(sys.argv)


