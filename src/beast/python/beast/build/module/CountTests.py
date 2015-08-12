# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os

from beast.build import Util
from beast.build.module import PhonyTargets


# Adds a phony target to the environment that always builds
# See: http://www.scons.org/wiki/PhonyTargets

# Build the list of source files that hold unit tests
def count_tests(root, target, source, env):
    suffixes = env.get('CPPSUFFIXES')
    count_lines = lambda f: sum(1 for i in open(f))
    is_test = lambda f: '.test.' in f and Util.match_suffix(suffixes, f)
    c = sum(Util.iterate_files(root, count_lines, is_test, os.walk))
    print('Total unit test lines: %d' % c)

def module(root):
    def _count_tests(target, source, env):
        return count_tests(root, target, source, env)
    return PhonyTargets.module(count_tests=_count_tests)
