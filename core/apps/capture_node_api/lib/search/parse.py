#!/usr/bin/python3

import ipaddress

import re

from apps.capture_node_api.lib.search.node import *

IPv4_re = r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}"
# regex matching IPv6 is tricky. The regex has to be very permissive due to all
# the possible variations.
IPv6_re = r"[0-9a-f:]*:[0-9a-f.:]+"
SRC_DIRS = ['src', 'source']
DST_DIRS = ['dst', 'dest', 'destination']
DIR_re = r"|".join(SRC_DIRS + DST_DIRS)

# Note: ipv6 token regex based on the work of Aeron (
# http://regexlib.com/REDetails.aspx?regexp_id=3065)
# Searches are case insensitive.
TOKENS = [
    ('OPEN_PAREN', r'\('),
    ('CLOSE_PAREN', r'\)'),
    ('NOT', r'not'),
    ('AND', r'and'),
    ('OR', r'or'),
    # The braces {,} in the regex are doubled so the ip re can be via .format
    ('IPv4', r'(?P<ipv4_dir>{dir_re})?\s*'  # An optional directionality
             # The (required) IPv4 address with an optional netmask.
             r'(?P<ipv4_addr>{ipv4_re}(?:\/{ipv4_re}|\/\d\d?)?)'
             # Optional port number
             r'(?::(?P<ipv4_port>\d{{1,5}}))?'.format(ipv4_re=IPv4_re, dir_re=DIR_re)),
    # The braces {,} in the regex are doubled so the ip re can be via .format
    ('IPv6', r'(?P<ipv6_dir>{dir_re})?\s*'  # An optional directionality
             # The (required) IPv6 address with an optional netmask.
             r'(?P<ipv6_addr>{ipv6_re}(?:\/{ipv6_re}|\/\d{{1,3}})?)'
     .format(ipv6_re=IPv6_re, dir_re=DIR_re)),
    ('PORT', r'(?P<port_dir>{dir_re})?\s*port\s+(?P<port>\d{{1,5}})'.format(dir_re=DIR_re)),
    # ('IPv6'             r'\b(?P<ConnectionType>src|dst){0,1} *(?P<address>[0-9a-f]*:[
    # 0-9a-f]*)+\b'),
    ('WS', r'\s+'),
    ('ERROR', r'[^\s]+')  # Match anything else as an error.
]

TOK_REGEX = '|'.join('(?P<%s>%s)' % pair for pair in TOKENS)
CTXT_AREA = 15
get_token = re.compile(TOK_REGEX, flags=re.DOTALL | re.IGNORECASE).match


def tokenize(search):
    """Tokenize the given search string.
    Yields a token for each token in the string, but does not return whitespace tokens.
    raises RuntimeError with (msg, error_pos)."""
    pos = 0
    match = get_token(search, pos)
    # Try to make token regex matches starting at the end of the last match.
    # Do this until either the string is consumed or we can't make a match.
    while match is not None:
        match_type = match.lastgroup
        if match_type == 'WS':
            # Ignore 'whitespace' tokens.
            pass
        else:
            context = match.group(match_type)
            tok_pos = (pos, match.end())

            groupd = match.groupdict()
            if match_type in 'IPv4':
                addr = groupd['ipv4_addr']
                try:
                    # Make sure our address is a sane one
                    addr = ipaddress.IPv4Network(addr)
                except (ValueError, ipaddress.AddressValueError) as err:
                    raise ParseError("Invalid IP network address: {}.".format(err),
                                     ErrorNode(tok_pos, context))

                port = groupd.get('ipv4_port')
                if port is not None:
                    port = int(port)

                ip_dir = groupd.get('ipv4_dir')
                if ip_dir in SRC_DIRS:
                    ip_dirs = ['src']
                elif ip_dir in DST_DIRS:
                    ip_dirs = ['dst']
                else:
                    ip_dirs = ['src', 'dst']

                # ip:port with no direction is turned into:
                # (srcip and (srcport or dstport) or dstip and (srcport or dstport))
                # With a direction, you only get one half or the other.
                # Without a port, you only get (srcip or dstip).
                yield OpenParenNode(tok_pos)
                for ip_dir in ip_dirs:
                    if ip_dir == 'src':
                        yield SrcIPv4Atom(tok_pos, addr, context)
                    else:
                        yield DstIPv4Atom(tok_pos, addr, context=context)

                    if port is not None:
                        yield AndNode(tok_pos)
                        yield OpenParenNode(tok_pos)
                        yield SrcPortAtom(tok_pos, port, context)
                        yield OrNode(tok_pos)
                        yield DstPortAtom(tok_pos, port, context)
                        yield CloseParenNode(tok_pos)

                    # If we're not on the last iteration, insert an OR
                    if ip_dir != ip_dirs[-1]:
                        yield OrNode(tok_pos)

                yield CloseParenNode(tok_pos)

            elif match_type == 'IPv6':
                addr = groupd['ipv6_addr']
                try:
                    # Make sure our address is a sane one
                    addr = ipaddress.IPv6Network(addr)
                except (ValueError, ipaddress.AddressValueError) as err:
                    raise ParseError("Invalid IP network address: {}.".format(err),
                                     ErrorNode(tok_pos, context))

                ip_dir = groupd.get('ipv6_dir')
                if ip_dir in SRC_DIRS:
                    ip_dirs = ['src']
                elif ip_dir in DST_DIRS:
                    ip_dirs = ['dst']
                else:
                    ip_dirs = ['src', 'dst']

                # ip:port with no direction is turned into:
                # (srcip and (srcport or dstport) or dstip and (srcport or dstport))
                # With a direction, you only get one half or the other.
                # Without a port, you only get (srcip or dstip).
                yield OpenParenNode(tok_pos)
                for ip_dir in ip_dirs:
                    if ip_dir == 'src':
                        yield SrcIPv6Atom(tok_pos, addr, context)
                    else:
                        yield DstIPv6Atom(tok_pos, addr, context)

                    # If we're not on the last iteration, insert an OR
                    if ip_dir != ip_dirs[-1]:
                        yield OrNode(tok_pos)
                yield CloseParenNode(tok_pos)

            elif match_type == 'PORT':
                port_dir = groupd.get('port_dir')
                port = int(groupd['port'])

                if port_dir is None:
                    yield OpenParenNode(tok_pos)
                    yield SrcPortAtom(tok_pos, port, context)
                    yield OrNode(tok_pos)
                    yield DstPortAtom(tok_pos, port, context)
                    yield CloseParenNode(tok_pos)
                elif port_dir in SRC_DIRS:
                    yield SrcPortAtom(tok_pos, port, context)
                elif port_dir in DST_DIRS:
                    yield DstPortAtom(tok_pos, port, context)

            elif match_type == 'OPEN_PAREN':
                yield OpenParenNode(tok_pos, context)
            elif match_type == 'CLOSE_PAREN':
                yield CloseParenNode(tok_pos, context)
            elif match_type == 'NOT':
                yield NotNode(tok_pos, context)
            elif match_type == 'AND':
                yield AndNode(tok_pos, context)
            elif match_type == 'OR':
                yield OrNode(tok_pos, context)
            else:
                raise ParseError("Invalid search token.", ErrorNode(tok_pos, context))

        pos = match.end()
        match = get_token(search, pos)


def build_tree(tokens, open_paren=None):
    """Build a parse tree of Node objects from the given sequence of tokens, and return
    the root node. The root node may be None (empty).

    raises RuntimeErrors (msg, token) for expected parse structure errors."""

    left = None
    token = next(tokens, None)
    subtree = None
    delayed = None
    while token is not None:
        # Implicit AND's are allowed, and automatically inserted between adjacent atoms or groups.
        if token.atom and left is not None:
            delayed = token
            token = AndNode(token.pos)

        if token.atom:
            # The case where left is not None is taken care of above.
            left = token
        elif token.type in (AndNode.type, OrNode.type):
            # There should always be something in 'left' at this point.
            if left is None:
                raise ParseError('Bad search syntax.', token)

            if subtree is None:
                token.append(left)
            elif subtree.type in (AndNode.type, NotNode.type):
                subtree.append(left)
                token.append(subtree)
            elif subtree.type == OrNode.type:
                token.append(left)
                subtree.append(token)
            else:
                raise ParseError('Bad syntax.', subtree)

            subtree = token
            left = None

        elif token.type == NotNode.type:
            if left is not None:
                raise ParseError("Bad syntax before NOT.", token)

            if subtree is not None:
                subtree.append(token)
            subtree = token

        elif token.type == OpenParenNode.type:

            # Parse the group of tokens as an independent parse tree.
            newtree = build_tree(tokens, open_paren=token)
            if newtree is None:
                raise ParseError("Empty sub-expression.", token)

            if left is None:
                left = newtree
            else:
                raise ParseError("Invalid expression", token)
        elif token.type == CloseParenNode.type:
            if open_paren is None:
                raise ParseError("Extra close parenthesis.", token)

            # We've reached the close parenthesis, so we can stop parsing this
            # parenthetical expression.
            break

        else:
            raise ParseError('Unhandled token.', token)

        if delayed:
            token = delayed
        else:
            token = next(tokens, None)

    if open_paren is not None and token is None:
        raise ParseError('Unclosed Parenthesis', open_paren)

    # Assign the dangling left item to the last subtree, or make it the subtree if there isn't one.
    if left is not None:
        if subtree is not None:
            subtree.append(left)
        else:
            subtree = left

    if subtree is None:
        return None

    return subtree.get_root()


def parse(search):
    """Parse the given search string, returning the resulting tree of Nodes."""

    return build_tree(tokenize(search))


if __name__ == '__main__':
    # s = ('((192.168.1.9 or 192.168.1.2) and 192.168.1.3) and (NOT 192.168.1.3)')
    import sys

    s = ' '.join(sys.argv[1:])

    try:
        # print(s)
        tree = parse(s)
        tree.graph('/tmp/parse1.png')
        tree = tree.normalize()
        tree.graph('/tmp/parse2.png')
        tree.prune()
        tree.graph('/tmp/parse3.png')
    except RuntimeError as E:
        print(E)
