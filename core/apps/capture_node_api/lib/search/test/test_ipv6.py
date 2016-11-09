import ipaddress as ip
import sys

import re

sys.path.append('../../../')
from apps.capture_node_api.lib.search import parse

v6_re_str = None
for tok, re_str in parse.TOKENS:
    if tok == 'IPv6':
        v6_re_str = re_str
        break

if v6_re_str is None:
    raise RuntimeError("Could not find v6 re")

v6_re = re.compile(v6_re_str,flags=re.IGNORECASE)

with open("pos_ipv6_tests.txt") as pos_tests:
    for line in pos_tests:
        line = line.strip()
        if line.startswith('#') or not line:
            continue

        m = v6_re.match(line)
        if m is None:
            raise ValueError("Should have matched: {}, but didn't.".format(line))

        match_str = m.groupdict()['ipv6_addr']
        if len(match_str) != len(line):
            raise ValueError("Only {} of {} matched.".format(m.group(), line))

        v6 = ip.IPv6Address(match_str)


with open("neg_ipv6_tests.txt") as neg_tests:
    for line in neg_tests:
        line = line.strip()
        if line.startswith('#') or not line:
            continue

        m = v6_re.match(line)

        if m is None:
            continue

        match_str = m.groupdict()['ipv6_addr']
        if len(match_str) != len(line):
            continue

        passed = True
        try:
            v6 = ip.IPv6Address(match_str)
        except ValueError:
            continue

        raise ValueError("IP {} should have failed, but passed".format(line))
