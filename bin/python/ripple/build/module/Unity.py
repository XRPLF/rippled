# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os

from beast.build import Module


def module(directories,
           source_root='src/ripple',
           unity_root='src/ripple/unity',
           **kwds):
    def _join(root, suffix=''):
        return (os.path.join(root, d) + suffix for d in directories)

    def _unity(scons):
        scons.add_source_files(*_join(unity_root, '.cpp'), **kwds)

    def _nounity(scons):
        scons.add_source_by_directory(*_join(source_root), **kwds)

    return Module.Module(['unity', _unity], ['nounity', _nounity])
