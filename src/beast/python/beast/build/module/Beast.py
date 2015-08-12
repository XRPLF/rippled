# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os

from beast.build.Build import Env, Module, compose, files, for_tags

MODULE = Module(
    setup=compose(
        Env.Append(
            CPPPATH=['src', os.path.join('src', 'beast')],
        ),
    ),

    files=compose(
        for_tags(
            'darwin',
            Env.Append(CPPDEFINES=[{'BEAST_COMPILE_OBJECTIVE_CPP': 1}]),
        ),

        files(
            'src/beast/beast/unity/hash_unity.cpp',
            'src/beast/beast/unity/beast.cpp',
        ),

        for_tags(
            'darwin',
            files(
                'src/beast/beast/unity/beastobjc.mm',
            ),
        ),
    ),
)
