# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

"""
If there's a command line argument profile-jemalloc=</path/to/jemalloc>, change
the scons's environment to use the jemalloc found at that root.
"""

import os
from beast.build import Module

def jemalloc(scons):
    profile_jemalloc = scons.sconstruct_globals.ARGUMENTS.get(
        'profile-jemalloc')
    if profile_jemalloc:
        append = scons.env.Append
        append(CPPDEFINES={'PROFILE_JEMALLOC': profile_jemalloc})
        append(LIBS=['jemalloc'])
        append(LIBPATH=[os.path.join(profile_jemalloc, 'lib')])
        append(CPPPATH=[os.path.join(profile_jemalloc, 'include')])
        append(LINKFLAGS=['-Wl,-rpath,' +
                          os.path.join(profile_jemalloc, 'lib')])

MODULE = Module.Module(jemalloc)
