# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from beast.build import Module

MODULE = Module.Module(
    Module.Env.Append(
        CPPPATH=['src', os.path.join('src', 'beast')]
    ),

    ['darwin',
         Module.Env.Append(
             CPPDEFINES=[
                 {'BEAST_COMPILE_OBJECTIVE_CPP': 1},
             ],
         ),
      ],
)
