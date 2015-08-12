# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections
import itertools

"""A tag represents a property of a build like "debug" or "unity".

A TagSet is an ordered set of tags where you must select exactly one: an example
is set(['release', 'debug']).

The first element in a TagSet is always the default choice from that TagSet
which will be used if no choice is specified.

A TagGroups is an ordered list of TagSets.

It only exists for its method `match_tags(partial_tags)` - if you give a partial
set of tags like the ones you'd specify on a commandline (gdb.debug) it returns
all possible filled-in tag choices from the TagSets.

It's this function that translates your command line targets into a full list of
tags.

"""

class TagGroups(object):
    def __init__(self, *groups):
        self.groups = groups

        # Find and report any duplicates.
        seen, dupes = set(), set()
        for tags in self.groups:
            dupes.update(seen.intersection(tags))
            seen.update(tags)
        if dupes:
            raise ValueError('Duplicated tags: ' + ', '.join(dupes))

    def match_tags(self, tags):
        """From an incoming list of tags, enumerate all build targets
        that match these tags."""
        tags = set(tags)
        product = []
        for ts in self.groups:
            intersection = tags.intersection(ts)
            if len(intersection) > 1:
                raise ValueError(
                    'Mutually exclusive tags: ' + ', '.join(intersection))
            if intersection:
                tag = intersection.pop()
                product.append([tag])
                tags.remove(tag)
            else:
                product.append(ts)

        if tags:
            plural = '' if len(tags) == 1 else 's'
            tags = ', '.join(tags)
            raise ValueError('Unknown tag%s: %s.' % (plural, tags))
        return itertools.product(*product)

    def __repr__(self):
        return 'TagGroups(%s)' % repr(self.groups)


class Targets(object):
    """Map a command line to targets and TagGroups.

    A target is what is actually being built - examples in rippled are "install"
    and "tests".  The target is read from the command line entry, or if there is
    none there, there's a default target ("install" in the case of rippled).
    """

    ALL = 'all'

    def __init__(self, *targets):
        self.default = targets[0].name
        self.targets = targets

        # The first tag is always the default tag.
        groups = targets[0].tag_groups.groups
        self.default_argument = '.'.join(t[0] for t in groups)

    def all_tags(self):
        return collections.defaultdict(set)

    def targets_to_tags(self, arguments):
        results = collections.defaultdict(set)
        if Targets.ALL in arguments:
            for target in self.targets:
                match = target.tag_groups.match_tags([])
                results[target].update(match)
            return results

        for arg in arguments:
            tags = arg.split('.')
            for target in self.targets:
                if target.name in tags:
                    tags.remove(target.name)
                    break
            else:
                target = self.targets[0]
            results[target].update(target.tag_groups.match_tags(tags))
        return results

    def __repr__(self):
        return 'Target(%s)' % repr(self.targets)
