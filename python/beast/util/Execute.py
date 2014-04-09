from __future__ import absolute_import, division, print_function, unicode_literals

import subprocess

from beast.util import String

def execute(args, include_errors=True, **kwds):
    """Execute a shell command and return the value.  If args is a string,
    it's split on spaces - if some of your arguments contain spaces, args should
    instead be a list of arguments."""
    if String.is_string(args):
        args = args.split()
    stderr = subprocess.STDOUT if include_errors else None
    return subprocess.check_output(args, stderr=stderr, **kwds)
