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


class GitInfo(object):
    """Provides information about git and the repository we are called from."""
    def __init__(self, env, verbose=True):
        self.tags = self.branch = self.user = ''
        self.exists = env.Detect('git')
        if not self.exists:
            if verbose:
                print('ERROR: not in a git directory!')
            return
        try:
            self.tags = _execute('git describe --tags')
            self.branch = _execute('git rev-parse --abbrev-ref HEAD')
            remote = _execute('git config remote.origin.url')
            self.user = remote.split(':')[1].split('/')[0]
        except:
            if verbose:
                print('ERROR: No git tag found!')
            self.exists = False

    def to_dict(self):
        if self.exists:
            id = '%s+%s.%s' % (self.tags, self.user, self.branch)
            return {'CPPDEFINES': {'GIT_COMMIT_ID' : '\'"%s"\'' % id}}
        return {}


def git_tag(env):
    return GitInfo(env, verbose=False).to_dict()
