# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build.Build import Env, Module, compose, pkg_config, for_tags

def before(*source_directories):
    def function(state):
        proto_build = os.path.join(state.build_dir, 'proto')
        for source in source_directories:
            state.env.Protoc([],
                source,
                PROTOCPROTOPATH=[os.path.dirname(source)],
                PROTOCOUTDIR=proto_build,
                PROTOCPYTHONOUTDIR=None)
    return function


def _files(variant):
    proto_build = os.path.join(variant.state.build_dir, 'proto')
    variant.env.Append(CPPPATH=proto_build)
    variant.state.variant_tree['proto'] = os.path.join(
        variant.state.build_dir, 'proto')


files = compose(
    _files,

    for_tags('linux', pkg_config('protobuf')),

    for_tags(
        'msvc',
        Env.Append(
            CPPPATH=[
                os.path.join('src', 'protobuf', 'src'),
                os.path.join('src', 'protobuf', 'vsprojects'),
            ],
        ),
    ),
)

def module(*source_directories):
    return Module(
        before=before(*source_directories),
        files=files,
    )
