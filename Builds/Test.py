#!/usr/bin/env python

#    This file is part of rippled: https://github.com/ripple/rippled
#    Copyright (c) 2012 - 2017 Ripple Labs Inc.
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

To pass flags to cmake, put them at the very end of the command line, after
the -- flag - like this:

    ./Builds/Test.py -- -j4  # Pass -j4 to cmake --build


Common problems:

1) Boost not found. Solution: export BOOST_ROOT=[path to boost folder]

2) OpenSSL not found. Solution: export OPENSSL_ROOT=[path to OpenSSL folder]

3) cmake is not found. Solution: Be sure cmake directory is on your $PATH

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

# CMake
if IS_WINDOWS:
    CMAKE_UNITY_CONFIGS = ['Debug', 'Release']
    CMAKE_NONUNITY_CONFIGS = ['Debug', 'Release']
else:
    CMAKE_UNITY_CONFIGS = []
    CMAKE_NONUNITY_CONFIGS = []
CMAKE_UNITY_COMBOS = { '' : [['rippled'], CMAKE_UNITY_CONFIGS],
    '.nounity' : [['rippled'], CMAKE_NONUNITY_CONFIGS] }

if IS_WINDOWS:
    CMAKE_DIR_TARGETS = { ('msvc' + unity,) : targets for unity, targets in
        CMAKE_UNITY_COMBOS.items() }
elif IS_OS_X:
    CMAKE_DIR_TARGETS = { (build + unity,) : targets
                   for build in ['debug', 'release']
                   for unity, targets in CMAKE_UNITY_COMBOS.items() }
else:
    CMAKE_DIR_TARGETS = { (cc + "." + build + unity,) : targets
                   for cc in ['gcc', 'clang']
                   for build in ['debug', 'release', 'coverage', 'profile']
                   for unity, targets in CMAKE_UNITY_COMBOS.items() }

# list of tuples of all possible options
if IS_WINDOWS or IS_OS_X:
    CMAKE_ALL_GENERATE_OPTIONS = [tuple(x) for x in powerset(['-GNinja', '-Dassert=true'])]
else:
    CMAKE_ALL_GENERATE_OPTIONS = list(set(
        [tuple(x) for x in powerset(['-GNinja', '-Dstatic=true', '-Dassert=true', '-Dsan=address'])] +
        [tuple(x) for x in powerset(['-GNinja', '-Dstatic=true', '-Dassert=true', '-Dsan=thread'])]))

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
    '--testjobs',
    default='0',
    type=int,
    help='Run tests in parallel'
)

parser.add_argument(
    '--ipv6',
    action='store_true',
    help='Use IPv6 localhost when running unit tests.',
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
    '--dir', '-d',
    default=(),
    nargs='*',
    help='Specify one or more CMake dir names. '
        'Will also be used as -Dtarget=<dir> running cmake.'
)

parser.add_argument(
    '--target',
    default=(),
    nargs='*',
    help='Specify one or more CMake build targets. '
        'Will be used as --target <target> running cmake --build.'
    )

parser.add_argument(
    '--config',
    default=(),
    nargs='*',
    help='Specify one or more CMake build configs. '
        'Will be used as --config <config> running cmake --build.'
    )

parser.add_argument(
    '--generator_option',
    action='append',
    help='Specify a CMake generator option. Repeat for multiple options. '
        'Will be passed to the cmake generator. '
        'Due to limits of the argument parser, arguments starting with \'-\' '
        'must be attached to this option. e.g. --generator_option=-GNinja.')

parser.add_argument(
    '--build_option',
    action='append',
    help='Specify a build option. Repeat for multiple options. '
        'Will be passed to the build tool via cmake --build. '
        'Due to limits of the argument parser, arguments starting with \'-\' '
        'must be attached to this option. e.g. --build_option=-j8.')

parser.add_argument(
    'extra_args',
    default=(),
    nargs='*',
    help='Extra arguments are passed through to the tools'
)

ARGS = parser.parse_args()

def decodeString(line):
    # Python 2 vs. Python 3
    if isinstance(line, str):
        return line
    else:
        return line.decode()

def shell(cmd, args=(), silent=False, cust_env=None):
    """"Execute a shell command and return the output."""
    silent = ARGS.silent or silent
    verbose = not silent and ARGS.verbose
    if verbose:
        print('$' + cmd, *args)

    command = (cmd,) + args

    # shell is needed in Windows to find executable in the path
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=cust_env,
        shell=IS_WINDOWS)
    lines = []
    count = 0
    # readline returns '' at EOF
    for line in iter(process.stdout.readline, ''):
        if process.poll() is None:
            decoded = decodeString(line)
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
        else:
            break

    if not verbose and count:
        print()
    process.wait()
    return process.returncode, lines

def get_cmake_dir(cmake_dir):
    return os.path.join('build' , 'cmake' , cmake_dir)

def run_cmake(directory, cmake_dir, args):
    print('Generating build in', directory, 'with', *args or ('default options',))
    old_dir = os.getcwd()
    if not os.path.exists(directory):
        os.makedirs(directory)
    os.chdir(directory)
    if IS_WINDOWS and not any(arg.startswith("-G") for arg in args) and not os.path.exists("CMakeCache.txt"):
        if '--ninja' in args:
            args += ( '-GNinja', )
        else:
            args += ( '-GVisual Studio 14 2015 Win64', )
    # hack to extract cmake options/args from the legacy target format
    if re.search('\.unity', cmake_dir):
        args += ( '-Dunity=ON', )
    if re.search('\.nounity', cmake_dir):
        args += ( '-Dunity=OFF', )
    if re.search('coverage', cmake_dir):
        args += ( '-Dcoverage=ON', )
    if re.search('profile', cmake_dir):
        args += ( '-Dprofile=ON', )
    if re.search('debug', cmake_dir):
        args += ( '-DCMAKE_BUILD_TYPE=Debug', )
    if re.search('release', cmake_dir):
        args += ( '-DCMAKE_BUILD_TYPE=Release', )
    m = re.search('gcc(-[^.]*)', cmake_dir)
    if m:
        args += ( '-DCMAKE_C_COMPILER=' + m.group(0),
          '-DCMAKE_CXX_COMPILER=g++' + m.group(1), )
    elif re.search('gcc', cmake_dir):
        args += ( '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++', )
    m = re.search('clang(-[^.]*)', cmake_dir)
    if m:
        args += ( '-DCMAKE_C_COMPILER=' + m.group(0),
          '-DCMAKE_CXX_COMPILER=clang++' + m.group(1), )
    elif re.search('clang', cmake_dir):
        args += ( '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', )

    args += ( os.path.join('..', '..', '..'), )
    resultcode, lines = shell('cmake', args)

    if resultcode:
        print('Generating FAILED:')
        if not ARGS.verbose:
            print(*lines, sep='')
        sys.exit(1)

    os.chdir(old_dir)

def run_cmake_build(directory, target, config, args):
    print('Building', target, config, 'in', directory, 'with', *args or ('default options',))
    build_args=('--build', directory)
    if target:
      build_args += ('--target', target)
    if config:
      build_args += ('--config', config)
    if args:
        build_args += ('--',)
        build_args += tuple(args)
    resultcode, lines = shell('cmake', build_args)

    if resultcode:
        print('Build FAILED:')
        if not ARGS.verbose:
            print(*lines, sep='')
        sys.exit(1)

def run_cmake_tests(directory, target, config):
    failed = []
    if IS_WINDOWS:
        target += '.exe'
    executable = os.path.join(directory, config if config else 'Debug', target)
    if(not os.path.exists(executable)):
        executable = os.path.join(directory, target)
    print('Unit tests for', executable)
    testflag = '--unittest'
    quiet = ''
    testjobs = ''
    ipv6 = ''
    if ARGS.test:
        testflag += ('=' + ARGS.test)
    if ARGS.quiet:
        quiet = '-q'
    if ARGS.ipv6:
        ipv6 = '--unittest-ipv6'
    if ARGS.testjobs:
        testjobs = ('--unittest-jobs=' + str(ARGS.testjobs))
    resultcode, lines = shell(executable, (testflag, quiet, testjobs, ipv6))

    if resultcode:
        if not ARGS.verbose:
            print('ERROR:', *lines, sep='')
        failed.append([target, 'unittest'])

    return failed

def main():
    all_failed = []
    if ARGS.all:
        build_dir_targets = CMAKE_DIR_TARGETS
        generator_options = CMAKE_ALL_GENERATE_OPTIONS
    else:
        build_dir_targets = { tuple(ARGS.dir) : [ARGS.target, ARGS.config] }
        if ARGS.generator_option:
            generator_options = [tuple(ARGS.generator_option)]
        else:
            generator_options = [tuple()]

    if not build_dir_targets:
        # Let CMake choose the build tool.
        build_dir_targets = { () : [] }

    if ARGS.build_option:
        ARGS.build_option = ARGS.build_option + list(ARGS.extra_args)
    else:
        ARGS.build_option = list(ARGS.extra_args)

    for args in generator_options:
        for build_dirs, (build_targets, build_configs) in build_dir_targets.items():
            if not build_dirs:
                build_dirs = ('default',)
            if not build_targets:
                build_targets = ('rippled',)
            if not build_configs:
                build_configs = ('',)
            for cmake_dir in build_dirs:
                cmake_full_dir = get_cmake_dir(cmake_dir)
                run_cmake(cmake_full_dir, cmake_dir, args)

                for target in build_targets:
                    for config in build_configs:
                        run_cmake_build(cmake_full_dir, target, config, ARGS.build_option)
                        failed = run_cmake_tests(cmake_full_dir, target, config)

                        if failed:
                            print('FAILED:', *(':'.join(f) for f in failed))
                            if not ARGS.keep_going:
                                sys.exit(1)
                            else:
                                all_failed.extend([decodeString(cmake_dir +
                                        "." + target + "." + config), ':'.join(f)]
                                    for f in failed)
                        else:
                            print('Success')
                if ARGS.clean:
                    shutil.rmtree(cmake_full_dir)

    if all_failed:
        if len(all_failed) > 1:
            print()
            print('FAILED:', *(':'.join(f) for f in all_failed))
        sys.exit(1)

if __name__ == '__main__':
    main()
    sys.exit(0)
