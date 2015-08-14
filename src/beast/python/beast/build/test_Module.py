# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build import Module, Scons
from beast.build.MockScons import MockScons

class test_Module(unittest.TestCase):
    def test_matching_commands(self):
        func = lambda env: None
        tags = ['debug', 'ubuntu']
        fdebug = lambda: None
        frelease = lambda: None
        module = [
            func,
            ['release', frelease],
            ['debug', fdebug],
            ['OSX', frelease]]
        lines = Module.matching_commands(tags, module)
        self.assertEquals(list(lines), [func, fdebug])

    def test_trivial_module(self):
        module = Module.module()
        tags = []
        self.assertFalse(module(tags, MockScons()))

    def test_module(self):
        result = []
        def function(scons, **kwds):
            result.append(kwds)

        module = Module.module(
            function,
            ['debug', function, Module.Env.Append(CXXFLAGS='-g')],
            ['ubuntu', Module.applier(function, thing='ubub')],
            ['wombatu', Module.applier(function, thing='FAIL')],
            ['no_debug', Module.applier(function, thing='FAIL')],
            ['no_msvc', Module.applier(function, something='not msvc')],
            ['debug', 'wombatu', Module.Env.Append(CXXFLAGS='FAIL')],
            ['debug', 'ubuntu', Module.Env.Append(CPPPATH='/var/foo')],
        )
        scons = MockScons()

        module(['debug', 'ubuntu', 'unity'], scons)
        self.assertEquals(scons.env.appends,
                          [{'CXXFLAGS': u'-g'}, {'CPPPATH': u'/var/foo'}])
        self.assertEquals(
            result, [{}, {}, {'thing': u'ubub'}, {'something': u'not msvc'}])

if __name__ == "__main__":
    unittest.main()
