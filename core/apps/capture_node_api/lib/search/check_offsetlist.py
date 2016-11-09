import struct
import sys

OFFSET_FMT = 'Q'

file = open(sys.argv[1], 'rb')

offset_size = struct.calcsize(OFFSET_FMT)

last = 0

data = file.read(offset_size)
while len(data) == offset_size:
    offset = struct.unpack(OFFSET_FMT, data)[0]

    print(offset)

    if offset <= last:
        raise ValueError("Bad offset ordering.")

    last = offset
    data = file.read(offset_size)

if len(data) != 0:
    print("Incomplete file:", data)
