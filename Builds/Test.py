#!/usr/bin/env python

#    This file is part of rippled: https://github.com/ripple/rippled
#    Copyright (c) 2012 - 2015 Ripple Labs Inc.
#
#    Permission to use, copy, modify, and/or distribute this software for any
#    purpose  with  or without fee is hereby granted, provided that the above
#    copyright notice and this permission notice appear in all copies.
#
#    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
#    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
#    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

"""
Invocation:

    ./Builds/Test.py - builds and tests all configurations

The build must succeed without shell aliases for this to work.

To pass flags to scons, put them at the very end of the command line, after
the -- flag - like this:

    ./Builds/Test.py -- -j4   # Pass -j4 to scons.


Common problems:

1) Boost not found. Solution: export BOOST_ROOT=[path to boost folder]

2) OpenSSL not found. Solution: export OPENSSL_ROOT=[path to OpenSSL folder]

3) scons is an alias. Solution: Create a script named "scons" somewhere in
   your $PATH (eg. ~/bin/scons will often work).

      #!/bin/sh
      python /C/Python27/Scripts/scons.py "${@}"

"""
from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import itertools
import os
import platform
import re
import subprocess
import sys

IS_WINDOWS = platform.system().lower() == 'windows'

if IS_WINDOWS:
    BINARY_RE = re.compile(r'build\\([^\\]+)\\rippled.exe')

else:
    BINARY_RE = re.compile(r'build/([^/]+)/rippled')

ALL_TARGETS = ['debug', 'release']

parser = argparse.ArgumentParser(
    description='Test.py - run ripple tests'
)

parser.add_argument(
    '--all', '-a',
    action='store_true',
    help='Build all configurations.',
)

parser.add_argument(
    '--keep_going', '-k',
    action='store_true',
    help='Keep going after one configuration has failed.',
 )

parser.add_argument(
    '--silent', '-s',
    action='store_true',
    help='Silence all messages except errors',
 )

parser.add_argument(
    '--verbose', '-v',
    action='store_true',
    help=('Report more information about which commands are executed and the '
          'results.'),
 )

parser.add_argument(
    '--test', '-t',
    default='',
    help='Add a prefix for unit tests',
 )

parser.add_argument(
    'scons_args',
    default=(),
    nargs='*'
    )

ARGS = parser.parse_args()

def shell(*cmd, **kwds):
    "Execute a shell command and return the output."
    silent = kwds.pop('silent', ARGS.silent)
    verbose = not silent and kwds.pop('verbose', ARGS.verbose)
    if verbose:
        print('$', ' '.join(cmd))
    kwds['shell'] = IS_WINDOWS

    process = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        **kwds)
    lines = []
    count = 0
    for line in process.stdout:
        lines.append(line)
        if verbose:
            print(line, end='')
        elif not silent:
            count += 1
            if count >= 80:
                print()
                count = 0
            else:
                print('.', end='')

    if not verbose and count:
        print()
    process.wait()
    return process.returncode, lines

if __name__ == '__main__':
    args = list(ARGS.scons_args)
    if ARGS.all:
        for a in ALL_TARGETS:
            if a not in args:
                args.append(a)
    print('Building:', *(args or ['(default)']))

    # Build everything.
    resultcode, lines = shell('scons', *args)
    if resultcode:
        print('Build FAILED:')
        if not ARGS.verbose:
            print(*lines, sep='')
        exit(1)

    # Now extract the executable names and corresponding targets.
    failed = []
    _, lines = shell('scons', '-n', '--tree=derived', *args, silent=True)
    for line in lines:
        match = BINARY_RE.search(line)
        if match:
            executable, target = match.group(0, 1)

            print('Unit tests for', target)
            testflag = '--unittest'
            if ARGS.test:
                testflag += ('=' + ARGS.test)

            resultcode, lines = shell(executable, testflag)
            if resultcode:
                print('ERROR:', *lines, sep='')
                failed.append([target, 'unittest'])
                if not ARGS.keep_going:
                    break
            ARGS.verbose and print(*lines, sep='')

            print('npm tests for', target)
            resultcode, lines = shell('npm', 'test', '--rippled=' + executable)
            if resultcode:
                print('ERROR:\n', *lines, sep='')
                failed.append([target, 'npm'])
                if not ARGS.keep_going:
                    break
            else:
                ARGS.verbose and print(*lines, sep='')

    if failed:
        print('FAILED:', *(':'.join(f) for f in failed))
        exit(1)
    else:
        print('Success')
