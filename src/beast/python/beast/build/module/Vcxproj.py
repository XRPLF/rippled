# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)


import os
from beast.build.Build import Module

def after(target_directory, **kwds):
    def function(state):
        vcxproj = state.env.VSProject(
            target_directory,
            VSPROJECT_CONFIGS=state.msvc_configs,
            **kwds)
        state.env.Alias('vcxproj', vcxproj)
    return function

def module(target_directory, **kwds):
    return Module(after=after(target_directory, **kwds))
