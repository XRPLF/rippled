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
import shutil
import sys
import subprocess


def powerset(iterable):
    """powerset([1,2,3]) --> () (1,) (2,) (3,) (1,2) (1,3) (2,3) (1,2,3)"""
    s = list(iterable)
    return itertools.chain.from_iterable(itertools.combinations(s, r) for r in range(len(s) + 1))

IS_WINDOWS = platform.system().lower() == 'windows'
IS_OS_X = platform.system().lower() == 'darwin'

if IS_WINDOWS or IS_OS_X:
    ALL_TARGETS = [('debug',), ('release',)]
else:
    ALL_TARGETS = [(cc + "." + target,)
                   for cc in ['gcc', 'clang']
                   for target in ['debug', 'release', 'coverage', 'profile',
                                  'debug.nounity', 'release.nounity', 'coverage.nounity', 'profile.nounity']]

# list of tuples of all possible options
if IS_WINDOWS or IS_OS_X:
    ALL_OPTIONS = [tuple(x) for x in powerset(['--assert'])]
else:
    ALL_OPTIONS = list(set(
        [tuple(x) for x in powerset(['--ninja', '--static', '--assert', '--sanitize=address'])] +
        [tuple(x) for x in powerset(['--ninja', '--static', '--assert', '--sanitize=thread'])]))

# list of tuples of all possible options + all possible targets
ALL_BUILDS = [options + target
              for target in ALL_TARGETS
              for options in ALL_OPTIONS]

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
    '--clean', '-c',
    action='store_true',
    help='delete all build artifacts after testing',
)

parser.add_argument(
    '--quiet', '-q',
    action='store_true',
    help='Reduce output where possible (unit tests)',
)

parser.add_argument(
    'scons_args',
    default=(),
    nargs='*'
)

ARGS = parser.parse_args()


def shell(cmd, args=(), silent=False):
    """"Execute a shell command and return the output."""
    silent = ARGS.silent or silent
    verbose = not silent and ARGS.verbose
    if verbose:
        print('$' + cmd, *args)

    command = (cmd,) + args

    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=IS_WINDOWS)
    lines = []
    count = 0
    for line in process.stdout:
        # Python 2 vs. Python 3
        if isinstance(line, str):
            decoded = line
        else:
            decoded = line.decode()
        lines.append(decoded)
        if verbose:
            print(decoded, end='')
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


def run_tests(args):
    failed = []
    if IS_WINDOWS:
        binary_re = re.compile(r'build\\([^\\]+)\\rippled.exe')
    else:
        binary_re = re.compile(r'build/([^/]+)/rippled')
    _, lines = shell('scons', ('-n', '--tree=derived',) + args, silent=True)
    for line in lines:
        match = binary_re.search(line)
        if match:
            executable, target = match.group(0, 1)

            print('Unit tests for', target)
            testflag = '--unittest'
            quiet = ''
            if ARGS.test:
                testflag += ('=' + ARGS.test)
            if ARGS.quiet:
                quiet = '-q'
            resultcode, lines = shell(executable, (testflag, quiet,))

            if resultcode:
                if not ARGS.verbose:
                    print('ERROR:', *lines, sep='')
                failed.append([target, 'unittest'])
                if not ARGS.keep_going:
                    break

    return failed


def run_build(args=None):
    print('Building:', *args or ('(default)',))
    resultcode, lines = shell('scons', args)

    if resultcode:
        print('Build FAILED:')
        if not ARGS.verbose:
            print(*lines, sep='')
        sys.exit(1)
    if '--ninja' in args:
        resultcode, lines = shell('ninja')

        if resultcode:
            print('Ninja build FAILED:')
            if not ARGS.verbose:
                print(*lines, sep='')
            sys.exit(1)


def main():
    if ARGS.all:
        to_build = ALL_BUILDS
    else:
        to_build = [tuple(ARGS.scons_args)]

    all_failed = []

    for build in to_build:
        args = ()
        # additional arguments come first
        for arg in list(ARGS.scons_args):
            if arg not in build:
                args += (arg,)
        args += build

        run_build(args)
        failed = run_tests(args)

        if failed:
            print('FAILED:', *(':'.join(f) for f in failed))
            if not ARGS.keep_going:
                sys.exit(1)
            else:
                all_failed.extend([','.join(build), ':'.join(f)]
                    for f in failed)
        else:
            print('Success')

        if ARGS.clean:
            shutil.rmtree('build')
            if '--ninja' in args:
                os.remove('build.ninja')
                os.remove('.ninja_deps')
                os.remove('.ninja_log')

    if all_failed:
        if len(to_build) > 1:
            print()
            print('FAILED:', *(':'.join(f) for f in all_failed))
        sys.exit(1)

if __name__ == '__main__':
    main()
    sys.exit(0)
