# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build import Module
from beast.build.CcWarning import disable_warnings

class BeastObjectiveC(object):
    MODULE = Module.Module(
        ['clang', 'osx', Module.files('src/ripple/unity/beastobjc.mm')])

if False:
    class Secp256k1(object):
        MODULE = Module.file_module(
            'src/ripple/unity/secp256k1.cpp',
            CPPPATH=['src/secp256k1'],
            CCFLAGS=disable_warnings(['tags'], 'unused-function'))

class Ed25519(object):
    MODULE = Module.file_module('src/ripple/unity/ed25519.c',
                                CPPPATH=['src/ed25519-donna'])

class WebSockets(object):
    MODULE = Module.Module(
        Module.files('src/ripple/unity/websocket02.cpp'),
        Module.files('src/ripple/unity/websocket04.cpp',
                     CPPPATH='src/websocketpp')
    )
