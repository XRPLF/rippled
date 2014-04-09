from __future__ import absolute_import, division, print_function, unicode_literals

import functools

from beast.util import Iter
from beast.util.Terminal import warn

def is_string(s):
    """Is s a string? - in either Python 2.x or 3.x."""
    return isinstance(s, (str, unicode))

def stringify(item, joiner=''):
    """If item is not a string, stringify its members and join them."""
    try:
        len(item)
    except:
        return str(item)
    if not item or is_string(item):
        return item or ''
    else:
        return joiner.join(str(i) for i in item)

def single_line(line, report_errors=True, joiner='+'):
    """Force a string to be a single line with no carriage returns, and report
    a warning if there was more than one line."""
    lines = line.strip().splitlines()
    if report_errors and len(lines) > 1:
      print('multiline result:', lines)
    return joiner.join(lines)

# Copied from
# https://github.com/lerugray/pickett/blob/master/pickett/ParseScript.py
def remove_comment(line):
    """Remove trailing comments from one line."""
    start = 0
    while True:
        loc = line.find('#', start)
        if loc == -1:
            return line.replace('\\#', '#')
        elif not (loc and line[loc - 1] == '\\'):
            return line[:loc].replace('\\#', '#')
        start = loc + 1

def remove_quotes(line, quote='"', print=print):
    if not line.startswith(quote):
        return line
    if line.endswith(quote):
        return line[1:-1]

    warn('line started with %s but didn\'t end with one:' % quote, print)
    print(line)
    return line[1:]

def fields_after_prefix(prefix, line):
    line = line.strip()
    return line.startswith(prefix) and line[len(prefix):].split()

def first_fields_after_prefix(prefix, sequence):
    condition = functools.partial(fields_after_prefix, prefix)
    return Iter.first(condition, sequence) or []
