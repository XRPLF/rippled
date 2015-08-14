# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import platform
import sys

from beast.System import SYSTEM

# See https://stackoverflow.com/questions/7445658
CAN_CHANGE_COLOR = (
    hasattr(sys.stderr, 'isatty')
    and sys.stderr.isatty()
    and not SYSTEM.windows
    and not os.environ.get('INSIDE_EMACS')
)

# See https://en.wikipedia.org/wiki/ANSI_escape_code
BLUE = 94
GREEN = 92
RED = 91
YELLOW = 93

def add_mode(text, *modes):
    if CAN_CHANGE_COLOR:
        modes = ';'.join(str(m) for m in modes)
        return '\033[%sm%s\033[0m' % (modes, text)
    else:
        return text

def blue(text):
    return add_mode(text, BLUE)

def green(text):
    return add_mode(text, GREEN)

def red(text):
    return add_mode(text, RED)

def yellow(text):
    return add_mode(text, YELLOW)

def warn(text, print=print):
    print('%s %s' % (red('WARNING:'), text))

# Prints command lines using environment substitutions
def print_coms(coms, env):
    if type(coms) is str:
        coms=list(coms)
    for key in coms:
        cmdline = env.subst(env[key], 0,
            env.File('<target>'), env.File('<sources>'))
        print (green(cmdline))
