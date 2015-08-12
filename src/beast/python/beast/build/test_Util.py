# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build import Util

class test_Util(unittest.TestCase):
    def setUp(self):
        self.printed = []

    def test_empty_list_sources(self):
        s = Util.list_sources('/root', '.cpp', lambda x: [])
        self.assertEquals(list(s), [])

    def test_list_sources(self):
        walk = (('/foo', [], ['a.test.cpp']),
                ('/bar', [], ['b.cpp']),
                ('/bar', [], ['.c.test.cpp']))
        s = Util.list_sources('/root', '.cpp', lambda x: walk)
        self.assertEquals(list(s), ['/foo/a.test.cpp', '/bar/b.cpp'])

if __name__ == "__main__":
    unittest.main()
