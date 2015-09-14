# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

# Adds a phony target to the environment that always builds
# See: http://www.scons.org/wiki/PhonyTargets

from beast.build.Build import Module

def module(**kwds):
    def f(state):
        for key, value in kwds.items():
            state.env.AlwaysBuild(state.env.Alias(key, [], value))
    return Module(after=f)
