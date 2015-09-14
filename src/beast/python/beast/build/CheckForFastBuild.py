# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)


def check_for_fast_build(state):
    state.sconstruct.AddOption('--fast', action='store_true')
    if not state.sconstruct.GetOption('fast'):
        return

    print('Warning: turning on fast build mode.')

    # http://www.scons.org/wiki/GoFastButton

    state.env.Decider('MD5-timestamp')
    state.sconstruct.SetOption('implicit_cache', 1)
    state.sconstruct.SetOption('max_drift', 1)
    state.env.SourceCode('.', None)

    """By default, scons scans each file for include dependecies. The
    implicit_cache flag lets you cache these dependencies for later builds,
    and will only rescan files that change.

    Failure cases are:
    1) If the include search paths are changed (i.e. CPPPATH), then a file
       may not be rebuilt.
    2) If a same-named file has been added to a directory that is earlier in
       the search path than the directory in which the file was found.
    Turn on if this build is for a specific debug target (i.e. clang.debug)

    If one of the failure cases applies, you can force a rescan of
    dependencies using the command line option `--implicit-deps-changed`

    """
