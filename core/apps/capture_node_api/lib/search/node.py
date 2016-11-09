from collections import defaultdict
from datetime import datetime
import functools
from hashlib import sha1
import io
import subprocess

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'


class ParseError(ValueError):
    """Describes an error parsing or tokenizing the search.
    Will contain the following attributes:
      .msg - Error message describing what went wrong.
      .pos - A tuple (start, end) of where the error occured relative to the search string.
      .real - If true, this corresponds to a token directly generated from the search text. If
      false, it corresponds to generated content.
    """

    def __init__(self, msg, token):
        self.msg = msg
        self.token = token


@functools.total_ordering
class Node:
    """A node in the parse tree.

    The comparison operations for nodes are setup to bring non-inverted atoms to
    the top when sorted.
    """

    # Used to reset hashes while debugging. Should generally be an empty string.
    HASH_KEY = b'123'

    id = 0

    # This will be overloaded by all children classes
    type = None

    # What the node will be shown as if it has no context.
    _default_context = None

    # These are overloaded as instance attributes in some subclasses.
    value = None
    _subtrees = []

    def __init__(self, pos, context=None):
        self.id = Node.id
        Node.id += 1
        self.pos = pos
        self.context = context if context is not None else self._default_context
        self.parent = None
        self.inverted = False

    @property
    def atom(self):
        return False

    def serial_data(self):
        """Return the pertinant data as a serializable dict."""
        return {'id': self.id,
                'pos': self.pos,
                'context': self.context,
                'str': str(self),
                'type': self.type
                }

    def __eq__(self, other):
        """Nodes are equal if they are of the same type and inversion."""
        return self.type == other.type and self.inverted == other.inverted

    def __lt__(self, other):
        """Atoms are less than other nodes, and non-inverted nodes are less than inverted."""
        if self.atom and not other.atom:
            return True
        else:
            return (not self.inverted) and other.inverted

    def __str__(self):
        if self.atom:
            return "{} {}".format(self._pretty_type, str(self.value))
        elif self.type == 'ERROR':
            return self.context
        else:
            return self._default_context

    def __repr__(self):
        return "<{} at {} '{}'>".format(str(self), self.pos, self.context)

    def __contains__(self, item):
        """This contains checks by object identity within the _subtree list."""
        for subtree in self._subtrees:
            if id(subtree) == id(item):
                return True
        return False

    def __iter__(self):
        return iter(list(self._subtrees))

    def __len__(self):
        return len(self._subtrees)

    def total_size(self):
        """Return the total number of nodes in the tree."""
        size = 1
        for node in self:
            size += node.total_size()
        return size

    def append(self, node):
        """Add the given node to the list of sub-trees under this node, and set the added node's
        parent to this."""

        if self.atom:
            raise ParseError("Trying to assign a child node to an atom.", self)
        if node is self:
            raise ParseError("Trying to assign a node as the child of itself.", self)
        self._subtrees.append(node)
        node.parent = self

    def remove(self, node):
        """Remove the given node from the list of sub-trees for this node."""
        node.parent = None
        for i in range(len(self._subtrees)):
            # We need to look for the exact same node object, since we've re-purposed comparison.
            if self._subtrees[i] is node:
                self._subtrees.pop(i)
                return
        raise ParseError("No such subtree for this node.", self)

    def copy(self):
        """Returns a copy of this node and all of it's sub-trees."""
        if self.atom:
            newnode = type(self)(self.pos, self.value, self.context)
        else:
            newnode = type(self)(self.pos, self.context)
        newnode.inverted = self.inverted
        for subtree in self._subtrees:
            newnode.append(subtree.copy())
        return newnode

    def normalize(self):
        """Recursively normalize this tree.
        The normalized form has three levels:
            - A single OR as the root.
            - The OR level has only AND's as children.
            - The AND's have only ATOM's as children.
            - All NOT's are pushed down to apply directly to the ATOMs.
            - The OR and AND's may be implicit if they have a single child.

        returns - the new root of the tree.
        """

        # Move the NOT's down to the ATOM's.
        self.collapse_nots()

        # Re-arrange nodes to collapse the tree.
        root = self.collapse()

        # Make sure the root of the tree is an OR
        if root.type != OrNode.type:
            new_root = OrNode(root.pos)
            new_root.append(root)
            root = new_root

        # Make sure the second level is all AND's
        for subtree in root:
            if subtree.type != AndNode.type:
                new_node = AndNode(subtree.pos)
                root.remove(subtree)
                new_node.append(subtree)
                root.append(new_node)

        return root

    def collapse_nots(self, _invert=False):
        """Push down all the not nodes to the ATOM nodes at the bottom of the tree. The _invert
        argument is for when this calls itself recursively and needs to know whether to invert
        the next subtree."""

        # Simply invert the atoms, if we need to.
        if self.atom:
            self.inverted = _invert
        elif self.type == NotNode.type:
            if len(self._subtrees) != 1:
                # NOT's always should have only one sub-item, and we don't check for that
                # in the parse step.
                raise ParseError('Invalid target for a NOT.', self)
            elif self.parent is None:
                # It would be silly for us to have an entire search inverted, plus I would
                # have keep track of the root to change it on the fly.
                raise ParseError('Basing a search on a NOT is a bad idea.', self)
            else:
                # Remove this node, then recursively run this method on the subtree (inverted).
                parent = self.parent
                parent.remove(self)
                parent.append(self._subtrees[0])
                self._subtrees[0].collapse_nots(not _invert)
        elif self.type in (AndNode.type, OrNode.type):
            # Apply De Morgan's law to AND's and OR's to propagate the NOT's downward.
            # !(A and B) => !A or !B
            # !(A or B) => !A and !B
            if _invert:
                # We can't change change the 'value' of the tokens, which may lead
                # to weird error reporting occasionally.
                if self.type == AndNode.type:
                    self.type = OrNode.type
                else:
                    self.type = AndNode.type
            for sub_node in list(self._subtrees):
                sub_node.collapse_nots(_invert)
        else:
            # We have some crazy node type in our tree. Fail hard.
            raise ParseError('Invalid node type: {}'.format(self.type), self)

    def collapse(self):
        """Recursively collapses the tree.
    - Uses the commutative property to combine AND->AND and OR->OR relationships.
    - Uses the distributive property to distribute AND relationships below OR's.
      A and (B or C) => (A and B) or (A and C)

    Returns the new root of the (sub)tree.
        """

        # Go through each subtree and merge commutatively after collapsing the subtree.
        for subtree in list(self._subtrees):
            # Remove the node, it may be a different subtree root after collapsing it.
            self.remove(subtree)
            subtree = subtree.collapse()
            if subtree.type == self.type and self.type in (AndNode.type, OrNode.type):
                # Merge this child's subtrees
                # Since we already collapsed the subtrees, there shouldn't be any chains of
                # AND's or OR's left in the subtrees, so we don't have to worry about that.
                for sst in subtree:
                    self.append(sst)

            else:
                # Only add the node back if we didn't merge it.
                self.append(subtree)

        if self.type == AndNode.type:
            # Look for the first 'OR' to apply the distributive property against.
            # After we distribute against the first OR, any others will collapse commutatively.
            for subtree in list(self._subtrees):
                if subtree.type == OrNode.type:
                    # 1. Remove the 'OR' subtree we found. We'll be copying everything else in
                    #    the 'AND' clause, and we don't want to copy that too.
                    self.remove(subtree)
                    # 2. Remove the subtrees from this 'OR' and keep a list of them. We'll reuse
                    #    the now empty 'OR' clause as the new root.
                    or_subtrees = []
                    for otree in subtree:
                        or_subtrees.append(otree)
                        subtree.remove(otree)
                    # 3. 'AND' each 'OR' subtree in a new 'AND' node. We'll make new copies of
                    #    the original 'AND' clause and all it's children (except for this 'OR'
                    #    subtree). We reuse the original AND clause for the last OR subtree.
                    #    Append these 'AND' clause to new 'OR' root.
                    for otree in or_subtrees[:-1]:
                        new_and = self.copy()
                        new_and.append(otree)
                        subtree.append(new_and)
                    self.append(or_subtrees[-1])
                    subtree.append(self)

                    # 4. This rearranging may have made some subtrees collapsible, so rerun
                    #    collapse on this subtree and return that root.
                    return subtree.collapse()

        # The root only changes when we do the distributive AND->OR swap above.
        return self

    def get_root(self):
        """Return the root of this tree reachable from this node."""
        root = self
        while root.parent is not None:
            root = root.parent
        return root

    @property
    def output_filename(self):
        raise NotImplementedError("The name attribute must be defined for runable nodes.")

    _graph_colors = defaultdict(lambda: 'skyblue',
                                **{'AND': 'khaki1',
                                   'OR': 'orange',
                                   'NOT': 'grey60',
                                   'inverted': 'pink'})

    _node_style = 'node{0:d} [label="{1:s}",fillcolor={2:s},color={3:s},style=filled]\n'

    def graph(self, outfile, show_ids=False):
        """Produce a graphviz graph of this parse tree as a png.
        outfile - Name of the output file.
        show_id's - Show the unique node id's next to names."""
        graph = io.StringIO()
        graph.write('digraph Search {\n')

        def graph_node(node):
            """Recursively produce the graph output for this node and its children."""
            invert_flag = '!' if node.inverted else ''
            label = '{0:s}{1:s}'.format(invert_flag, str(node))
            if show_ids:
                label += ' {0:d}'.format(node.id)

            color = 'black'
            fill_color = self._graph_colors[node.type]
            if node.inverted:
                fill_color = self._graph_colors['inverted']
            graph.write(self._node_style.format(node.id, label, fill_color, color))

            if node in node:
                raise RuntimeError("Cycle in parse tree graph.", node)
            for sub_n in node:
                graph.write('node{0:d} -> node{1:d};\n'.format(node.id, sub_n.id))
                graph_node(sub_n)

            return

        graph_node(self)
        graph.write('}')
        graph.seek(0)

        data = graph.read()
        datab = bytes(data, 'ascii')

        dot = subprocess.Popen(['dot', '-Tpng', '-o', outfile], stdin=subprocess.PIPE)
        dot.communicate(input=datab)
        dot.wait()


class ErrorNode(Node):
    type = 'ERROR'

    @property
    def output_filename(self):
        raise TypeError("Method not supported.")


class LogicNode(Node):

    def __init__(self, pos, context=None):
        super().__init__(pos, context)
        self._subtrees = []
        self.parent = None

    @property
    def output_filename(self):
        """Generate the name of this AND clause, which is just a hash of the sorted atom names.
        Note: This only makes sense after normalization and pruning.
        :return: str
        """

        atoms = sorted(self, key=lambda a: (a.type, a.value))

        name_hash = sha1(self.HASH_KEY)
        for atom in atoms:
            name_hash.update(atom.output_filename.encode('utf-8'))

        return '_' + name_hash.hexdigest()


class AndNode(LogicNode):
    type = 'AND'
    _default_context = 'and'

    _AND_CMD = 'and_atoms'

    def prune(self):
        # Compare each item to each other item. Note that order does not matter, as any time we
        # change anything it is (or equates to) and intersection of the sets (which is all
        # commutative).
        atoms = list(self)
        for i in range(len(atoms)):
            for j in range(i + 1, len(atoms)):
                atom1 = atoms[i]
                atom2 = atoms[j]

                if not (atom1.is_prune_compat(atom2) and  # They must be the same type
                        atom1 in self and  # We might have already pruned this.
                        atom2 in self):
                    continue
                elif not (atom1.inverted or atom2.inverted):
                    # If neither of the atoms are inverted.
                    if atom1.superset(atom2):
                        # Only elements in atom2 can match.
                        self.remove(atom1)
                    elif atom2.superset(atom1):
                        # Only elements in atom1 can match.
                        self.remove(atom2)
                    elif atom1.intersects(atom2):
                        # Take the intersection of two by combining the ranges.
                        # Only items in both with match. Only works for port ranges.
                        atom1.intersect(atom2)
                        self.remove(atom2)
                    else:
                        # No intersection, so nothing in the entire AND clause can match.
                        self.prune_all()
                        return
                elif atom1.inverted and atom2.inverted:
                    if atom1.superset(atom2):
                        # Only elements outside atom1 can match
                        self.remove(atom2)
                    elif atom2.superset(atom1):
                        # Only elements outside atom2 can match
                        self.remove(atom1)
                    elif atom1.intersects(atom2):
                        # Take the union. Given that this is inverted only elements outside
                        # of both will match. Only works for port ranges.
                        atom1.union(atom2)
                        self.remove(atom2)
                    else:
                        # Leave as is; this expression must be resolved manually.
                        pass
                elif atom1.inverted ^ atom2.inverted:
                    if atom2.inverted:
                        # The first atom should always be the inverted one.
                        atom1, atom2 = atom2, atom1

                    if atom1.superset(atom2):
                        # Since atom1 contains all items in atom2, no items in atom2
                        # can be in !atom1, forcing an empty set. Prune the whole AND clause.
                        self.prune_all()
                        return
                    elif atom2.superset(atom1):
                        # This leaves a donut, which will have to be resolved manually.
                        # That's ok! This is the primary case that NOTing is for.
                        pass
                    elif atom1.intersects(atom2):
                        # The overall results
                        atom2.subtract(atom1)
                else:
                    raise RuntimeError("Impossible state in prune.")

        # We need to make certain that there is at least one positive assertion in each
        # AND clause.
        pos_assert = False
        for atom in self:
            if not atom.inverted:
                pos_assert = True
                break

        if not pos_assert:
            raise ParseError("At least one search must be non-negated.", self)

        if len(self) == 0:
            self.prune_all()
            return

    def prune_all(self):
        """Remove this entire AND clause from the search tree."""

        for atom in self:
            self.remove(atom)

        if self.parent is not None:
            self.parent.remove(self)


class OrNode(LogicNode):
    type = 'OR'
    _default_context = 'or'

    def make_search_description(self, indexes, start_dt, end_dt, proto):
        """Make a search description suitable for being read by the ./search program.
        :param [Index] indexes: A list/queryset of indexes to search.
        :param datetime start_dt: A timezone aware start datetime.
        :param datetime end_dt: A timezone aware end datetime.
        :param int proto: Find flows with this transport number. 0 means find all.
        :return:
        """

        config = []

        config.append("START {}".format(start_dt.timestamp()))
        config.append("END {}".format(end_dt.timestamp()))
        config.append("PROTO {}".format(proto))

        config_id = 0
        # Give all the atoms and id
        for and_node in self:
            atoms = []
            for atom in and_node:
                if atom.config_id is None:
                    atom.config_id = config_id
                    config_id += 1
                    config.append("{} {} {} {}".format(
                        atom.type,
                        atom.output_filename,
                        atom.first_value,
                        atom.last_value))

                if atom.inverted:
                    atoms.append('!{}'.format(atom.config_id))
                else:
                    atoms.append('{}'.format(atom.config_id))

            config.append('AND {} {}'.format(
                and_node.output_filename,
                ' '.join(atoms)
            ))

        full_flow_fn, partial_flow_fn = self.filtered_output_filenames(start_dt, end_dt, proto)
        full = []
        partial = []
        for index in indexes:
            if index.start_ts > start_dt and index.end_ts < end_dt:
                full.append(str(index.id))
            else:
                partial.append(str(index.id))

        config.append("OR {}".format(self.output_filename))
        config.append("PARTIAL {} {}".format(partial_flow_fn, " ".join(partial)))
        config.append("FULL {} {}".format(full_flow_fn, " ".join(full)))

        log.info('{}'.format(config))

        return '\n'.join(config)

    def prune(self):
        """Prune the tree of any atoms and AND clauses that must logically result in an
        empty set. Raises a ParseError if the entire parse tree ends up being empty."""

        for and_clause in self:
            and_clause.prune()

        if len(self) == 0:
            raise ParseError("Optimized search tree is empty.", self)

    _KNOWN_PROTOS = {
        'tcp': 6,
        'udp': 17
    }

    def filtered_output_filenames(self, start_dt, end_dt, proto):
        """Returns a predictable hash based on filtering for this search. T
        :param datetime start_dt: The start timestamp (in UTC) for the search.
        :param datetime end_dt: The end timestamp (in UTC) for the search.
        :param int proto: The protocol being searched.
        :return: (full_fn, partial_fn) The full_fn is the filename with only
                 the proto filter applied. The partial filename is for results
                 that depend on the time filters.
        :rtype: (str, str)
        """

        hash = sha1()
        for part in [self.output_filename, str(proto)]:
            hash.update(part.encode('ascii'))

        full_flow_fn = '_{}'.format(hash.hexdigest())

        hash.update(str(start_dt.timestamp()).encode('ascii'))
        hash.update(str(end_dt.timestamp()).encode('ascii'))

        part_flow_fn = '_{}'.format(hash.hexdigest())

        return full_flow_fn, part_flow_fn

class NotNode(LogicNode):
    type = 'NOT'
    _default_context = 'not'


class OpenParenNode(LogicNode):
    type = 'OPEN_PAREN'
    _default_context = '('


class CloseParenNode(LogicNode):
    type = 'CLOSE_PAREN'
    _default_context = ')'


class Atom(Node):
    atom = True
    # The Atom type name must be defined. It should matche the type names defined in the capture
    # system. For a list of these names, run bin/show_keys in the capture system directory.
    type = None
    _default_context = '<unknown_value>'
    _pretty_type = None

    _SUBINDEX_CMD = 'atom_search'

    def __init__(self, pos, value, context=None):
        super().__init__(pos, context)
        self.value = value

        self.config_id = None

    def intersects(self, other):
        """Return True of the search result sets can intersect."""
        raise NotImplementedError("This must be implemented in subclasses.")

    @property
    def output_filename(self):
        """The 'name' of this search atom, which should be unique for a given atom/index type
        and search parameters.
        return: str
        """

        return '_' + '_'.join((self.type, self.first_value, self.last_value))

    def superset(self, other):
        """Return True if this atom is a superset of the other atom."""
        raise NotImplementedError("This must be implemented in subclasses.")

    def is_prune_compat(self, other):
        return self.type == other.type


class IPAtom(Atom):
    def intersects(self, other):
        """Returns True if the two IP spaces can intersect. This can only occur if they are the
        same type."""

        return self.type == other.type and self.value.overlaps(other.value)

    def superset(self, other):
        """Return True if other.value is a subset of this ip space. Note that since
        IP atom types in the same direction are compatible, regardless of IP version, they will
        be compared with this method. They will never intersect, however."""

        return self.intersects(other) and (self.value.prefixlen <= other.value.prefixlen)

    def is_prune_compat(self, other):
        """For IP atoms the types are compatible as long as their both IP's and the
        directionality is the same. They will never have an intersection, though."""
        if ((self.type in (SrcIPv4Atom.type, SrcIPv6Atom.type) and
             other.type in (SrcIPv4Atom.type, SrcIPv6Atom.type)) or
            (self.type in (DstIPv4Atom.type, DstIPv6Atom.type) and
             other.type in (DstIPv4Atom.type, DstIPv6Atom.type))):
            return True
        else:
            return False

    @property
    def first_value(self):
        return str(self.value.network_address)

    @property
    def last_value(self):
        return str(self.value.broadcast_address)


class SrcIPv4Atom(IPAtom):
    type = 'SRCv4'
    _pretty_type = 'src'


class DstIPv4Atom(IPAtom):
    type = 'DSTv4'
    _pretty_type = 'dst'


class SrcIPv6Atom(IPAtom):
    type = 'SRCv6'
    _pretty_type = 'src'


class DstIPv6Atom(IPAtom):
    type = 'DSTv6'
    _pretty_type = 'dst'


class PortAtom(Atom):
    def intersects(self, other):
        return self.value == other.value

    def superset(self, other):
        return self.intersects(other)

    @property
    def first_value(self):
        return str(self.value)

    @property
    def last_value(self):
        return str(self.value)


class SrcPortAtom(PortAtom):
    type = 'SRCPORT'
    _pretty_type = 'src port'


class DstPortAtom(PortAtom):
    type = 'DSTPORT'
    _pretty_type = 'dst port'
