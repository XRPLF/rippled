# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build import Module

def proto_once(scons, source_directories):
    proto_build = os.path.join(scons.build_dir, 'proto')
    for source in source_directories:
        scons.env.Protoc([],
            source,
            PROTOCPROTOPATH=[os.path.dirname(source)],
            PROTOCOUTDIR=proto_build,
            PROTOCPYTHONOUTDIR=None)

def proto(scons):
    proto_build = os.path.join(scons.build_dir, 'proto')
    scons.env.Append(CPPPATH=proto_build)
    scons.variant_tree['proto'] = os.path.join(
        scons.build_dir, 'proto')

def module(*source_directories):
    base = os.path.join('src', 'protobuf')
    return Module.Module(
        proto,
        ['linux', Module.pkg_config('protobuf')],
        ['msvc', Module.Env.Append(
            CPPPATH=[os.path.join(base, 'src'),
                     os.path.join(base, 'vsprojects')])],
        run_once=lambda scons: proto_once(scons, source_directories),
    )
