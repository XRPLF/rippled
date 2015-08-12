# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections
import itertools

"""
A tag represents a property of a build like "debug" or "unity".
"""

class TagSetList(object):
    """An ordered list of sets of tags."""

    def __init__(self, *tags_list):
        self.tags_list = tags_list

        # Find and report any duplicates.
        seen, dupes = set(), set()
        for tags in self.tags_list:
            dupes.update(seen.intersection(tags))
            seen.update(tags)
        if dupes:
            raise ValueError('Duplicated tags: ' + ', '.join(dupes))

    def match_tags(self, tags):
        """From an incoming list of tags, enumerate all build targets
        that match these tags."""
        tags = set(tags)
        product = []
        for ts in self.tags_list:
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
        return 'TagSetList(%s)' % repr(self.tags_list)


class Target(object):
    def __init__(self, tags=None, command=None):
        self.tags_list = tags or ()
        self.tags = TagSetList(*self.tags_list)
        self.command = command

    def __call__(self, *args, **kwds):
        self.command and self.command(*args, **kwds)


class Targets(object):
    """Map targets to tag set lists. """

    def __init__(self, default, **targets):
        self.default = default
        self.targets = targets
        assert default in targets
        first = (t[0] for t in targets[default].tags_list)
        self.default_argument = '.'.join(first)

    def all_tags(self):
        results = collections.defaultdict(set)
        return results

    def targets_to_tags(self, arguments):
        results = collections.defaultdict(set)
        if 'all' in arguments:
            for target, tag_set_list in self.targets.items():
                results[target].update(tag_set_list.match_tags([]))
        else:
            for arg in arguments:
                tags = arg.split('.')
                for target, tag_set_list in self.targets.items():
                    if target in tags:
                        tags.remove(target)
                        break
                else:
                    target, tag_set_list = (
                        self.default, self.targets[self.default])
                results[target].update(tag_set_list.match_tags(tags))
        return results

    def __repr__(self):
        return 'Target(%s)' % repr(self.targets)
