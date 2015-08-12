# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build import Toolchain
from beast.build.Mock import MockState, MockSConstruct

class test_detect_toolchain(unittest.TestCase):
    def detect(self, is_vs=False, **environ):
        tools = Toolchain.detect(
            MockState(environ=environ, sconstruct={'is_vs': is_vs}))
        return sorted(tools)

    def test_trivial(self):
        self.assertEquals(self.detect(), [])

    def test_clang(self):
        self.assertEquals(
            self.detect(CLANG_CC='testcc', CLANG_CXX='testcp', CLANG_LINK='cl'),
            ['clang'])

    def test_gcc(self):
        self.assertEquals(
            self.detect(GNU_CC='testcc', GNU_CXX='testcp', GNU_LINK='cl'),
            ['gcc'])

    def test_all(self):
        self.assertEquals(
            self.detect(CLANG_CC='testcc', CLANG_CXX='testcp', CLANG_LINK='cl',
                        GNU_CC='testcc', GNU_CXX='testcp', GNU_LINK='cl'),
            ['clang', 'gcc'])

    def test_vs(self):
        self.assertEquals(self.detect(is_vs=True), ['msvc'])

    def test_failures(self):
        with self.assertRaises(ValueError):
            self.detect(CLANG_CC='testcc')
        with self.assertRaises(ValueError):
            self.detect(CLANG_CC='testcc', CLANG_CXX='testc++')


if __name__ == '__main__':
    unittest.main()
