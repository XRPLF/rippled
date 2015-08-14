# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build import Git, Module

"""Load a module with these source files - while adding environment files
definitions for the current git tag.

Since files in a GitTag module get compiled every time any git details change,
it's common to only have a single, very small file to reduce unnecessary
recompilation.
"""

def module(*source_files):
    def f(scons):
        scons.add_source_files(*source_files, **Git.git_tag(scons.env))
    return Module.Module(f)
