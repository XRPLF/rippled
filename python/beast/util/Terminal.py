from __future__ import absolute_import, division, print_function, unicode_literals

import sys

from beast.platform.Platform import PLATFORM

# See https://stackoverflow.com/questions/7445658/how-to-detect-if-the-console-does-support-ansi-escape-codes-in-python
CAN_CHANGE_COLOR = (
    hasattr(sys.stderr, "isatty")
    and sys.stderr.isatty()
    and not PLATFORM.startswith('Windows'))


# See https://en.wikipedia.org/wiki/ANSI_escape_code
RED = 91
GREEN = 92
BLUE = 94

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

def warn(text, print=print):
    print('%s %s' % (red('WARNING:'), text))
