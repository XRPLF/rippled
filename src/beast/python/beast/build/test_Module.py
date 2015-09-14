# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build import State, Env, Function, Module
from beast.build.Mock import MockState

def applier(f, *args, **kwds):
    """Return a function that applies f to an item.
    """
    return lambda x: f(x, *args, **kwds)

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
        module = Module.matching_function()
        tags = []
        self.assertFalse(module(tags, MockState()))

    def test_module(self):
        result = []
        def function(variant, **kwds):
            result.append(kwds)

        module = Module.matching_function(
            function,
            ['debug', function, Env.Append(CXXFLAGS='-g')],
            ['ubuntu', applier(function, thing='ubub')],
            ['wombatu', applier(function, thing='FAIL')],
            ['no_debug', applier(function, thing='FAIL')],
            ['no_msvc', applier(function, something='not msvc')],
            ['debug', 'wombatu', Env.Append(CXXFLAGS='FAIL')],
            ['debug', 'ubuntu', Env.Append(CPPPATH='/var/foo')],
        )
        state = MockState(tags=['debug', 'ubuntu', 'unity'])

        module(state)
        self.assertEquals(state.env.appends,
                          [{'CXXFLAGS': u'-g'}, {'CPPPATH': u'/var/foo'}])
        self.assertEquals(
            result, [{}, {}, {'thing': u'ubub'}, {'something': u'not msvc'}])

if __name__ == "__main__":
    unittest.main()
