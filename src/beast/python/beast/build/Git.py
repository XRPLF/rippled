# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import subprocess


def _execute(args, include_errors=True, **kwds):
    """Execute a shell command and return the value.  If args is a string,
    it's split on spaces - if some of your arguments contain spaces, args should
    instead be a list of arguments."""
    def single_line(line, report_errors=True, joiner='+'):
        """Force a string to be a single line with no carriage returns, and
        report a warning if there was more than one line."""
        lines = line.strip().splitlines()
        if report_errors and len(lines) > 1:
            print('multiline result:', lines)
        return joiner.join(lines)
    def is_string(s):
        """Is s a string? - in either Python 2.x or 3.x."""
        return isinstance(s, (str, unicode))
    if is_string(args):
        args = args.split()
    stderr = subprocess.STDOUT if include_errors else None
    return single_line(subprocess.check_output(args, stderr=stderr, **kwds))


def git_tag():
    try:
        tags = _execute('git describe --tags')
        branch = _execute('git rev-parse --abbrev-ref HEAD')
        remote = _execute('git config remote.origin.url')
        user = remote.split(':')[1].split('/')[0]

        id = '%s+%s.%s' % (tags, user, branch)
        return {'CPPDEFINES': {'GIT_COMMIT_ID' : '\'"%s"\'' % id}}
    except:
        return {}
