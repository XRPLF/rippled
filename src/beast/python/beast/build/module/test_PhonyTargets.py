# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build.module import PhonyTargets

class test_PhonyTargets(unittest.TestCase):
    def setUp(self):
        self.printed = []

    def print(self, line):
        self.printed.append(line)

    def open(self, fname):
        line = 'line\n'
        return {
            '/foo/a.test.cpp': [line],
            '/foo/b.cpp': [line, line],
            '/bar/c.test.cpp': [line, line, line],
        }[fname]

    def run_test(self, *walk_results):
        walk = lambda _: walk_results
        PhonyTargets.test_counter(
            None, None, {'CPPSUFFIXES': '.cpp'}, '/',
            walk, print=self.print, open=self.open)

    def test_trivial(self):
        self.run_test()
        self.assertEquals(self.printed, ['Total unit test lines: 0'])

    def test_more(self):
        self.run_test(('/foo', ['X'], ['a.test.cpp']),
                      ('/bar', ['X'], ['b.cpp']),
                      ('/bar', ['X'], ['c.test.cpp']))
        self.assertEquals(self.printed, ['Total unit test lines: 4'])

if __name__ == "__main__":
    unittest.main()
