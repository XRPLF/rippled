# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build import Module

def boost(scons, link_libraries):
    link_libraries = [i if i.startswith('boost_') else 'boost_' + i
                      for i in link_libraries]
    try:
        root = scons.environ['BOOST_ROOT']
    except KeyError:
        raise KeyError('The environment variable BOOST_ROOT must be set')
    root = os.path.normpath(root)

    # We prefer static libraries for boost
    static_libs = ['%s/stage/lib/lib%s.a' % (root, l) for l in link_libraries]
    if all(os.path.exists(f) for f in static_libs):
        link_libraries = [scons.sconstruct_globals.File(f) for f in static_libs]
    scons.env.Append(CPPPATH=[root],
                     LIBPATH=[os.path.join(root, 'stage', 'lib')],
                     LIBS=link_libraries + ['dl'])
    scons.env['BOOST_ROOT'] = root


def module(link_libraries=None):
    """Load Boost with the given precompiled link_libraries if any."""
    return Module.Module(
        lambda scons: boost(scons, link_libraries or []),
        ['clang',
              Module.Env.Append(CPPDEFINES=['BOOST_ASIO_HAS_STD_ARRAY'])],
)
