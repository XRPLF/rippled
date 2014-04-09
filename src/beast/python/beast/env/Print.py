from __future__ import absolute_import, division, print_function, unicode_literals

import textwrap

from beast.util import String
from beast.util import Terminal

FIELD_WIDTH = 10
LINE_WIDTH = 69

EMPTY_NAME = ' ' * FIELD_WIDTH

TEXT_WRAPPER = textwrap.TextWrapper(
    break_long_words=False,
    break_on_hyphens=False,
    width=LINE_WIDTH,
)

DISPLAY_EMPTY_ENVS = True

def print_build_vars(name, value, same, print=print):
    """Pretty-print values as a build configuration."""
    name = '%s' % name.rjust(FIELD_WIDTH)
    color = Terminal.blue if same else Terminal.green

    for line in TEXT_WRAPPER.wrap(String.stringify(value, ' ')):
        print(' '.join([name, color(line)]))
        name = EMPTY_NAME

def print_cmd_line(s, target, source, env):
    print(EMPTY_NAME + Terminal.blue(String.stringify(target)))

def print_build_config(env, original, print=print):
    print('\nConfiguration:')
    for name, value in env.items():
        if value or DISPLAY_EMPTY_ENVS:
            same = (value == original[name])
            if not same:
                print('"%s" != "%s"' % (value, original[name]))
            print_build_vars(name, value, same, print=print)
    print()
