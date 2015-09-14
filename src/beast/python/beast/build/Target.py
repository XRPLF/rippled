# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections
import itertools

from . TagGroups import TagGroups
from .. System import SYSTEM


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
        self.default_arguments = ['.'.join(t[0] for t in groups)]

    def all_tags(self):
        return collections.defaultdict(set)

    def targets_to_tags(self, arguments):
        arguments = arguments or self.default_arguments
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


# Our preferred compiler is gcc, except on OS/X.
# TODO: figure out a way to get this rule into data!!
TOOLCHAINS = ('clang', 'gcc', 'msvc') if SYSTEM.osx else (
    'gcc', 'clang', 'msvc')


class Target(object):
    CPP_GROUPS = (
        TOOLCHAINS,
        ('release', 'debug', 'profile'),
        ('unity', 'nounity'),
    )

    """
      name:         the name of the Target.
      tag_groups:   an optional list of possible build tags for this target.
      result_name:  the name of the resulting binary file.
      requires:     an optional list of another targets that this one requires.
    """
    def __init__(self, name, tag_groups=None, result_name=None, requires=None):
        self.name = name
        self.tag_groups = TagGroups(*(tag_groups or ()))
        self.result_name = result_name
        self.requires = requires or []

    def __repr__(self):
        return 'Target(%s)' % self.name
