# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

"""
If there's a command line argument profile-jemalloc=</path/to/jemalloc>, change
the State's environment to use the jemalloc found at that root.
"""

import os

from beast.build.Build import Module

def setup(variant):
    state = variant.state
    profile_jemalloc = state.sconstruct.ARGUMENTS.get('profile-jemalloc')
    if profile_jemalloc:
        state.env.Append(
            CPPDEFINES={'PROFILE_JEMALLOC': profile_jemalloc},
            LIBS=['jemalloc'],
            LIBPATH=[os.path.join(profile_jemalloc, 'lib')],
            CPPPATH=[os.path.join(profile_jemalloc, 'include')],
            LINKFLAGS=['-Wl,-rpath,' + os.path.join(profile_jemalloc, 'lib')],
        )

MODULE = Module(setup=setup)
