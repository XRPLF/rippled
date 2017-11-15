# Beast.py
# Copyright 2014 by:
#   Vinnie Falco <vinnie.falco@gmail.com>
#   Tom Ritchford <?>
#   Nik Bougalis <?>
# This file is part of Beast: http://github.com/vinniefalco/Beast

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import platform
import subprocess
import sys
import SCons.Node
import SCons.Util

#-------------------------------------------------------------------------------
#
# Environment
#
#-------------------------------------------------------------------------------

def _execute(args, include_errors=True, **kwds):
    """Execute a shell command and return the value.  If args is a string,
    it's split on spaces - if some of your arguments contain spaces, args should
    instead be a list of arguments."""
    def single_line(line, report_errors=True, joiner='+'):
        """Force a string to be a single line with no carriage returns, and report
        a warning if there was more than one line."""
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

class __System(object):
    """Provides information about the host platform"""
    def __init__(self):
        self.name = platform.system()
        self.linux = self.name == 'Linux'
        self.osx = self.name == 'Darwin'
        self.windows = self.name == 'Windows'
        self.distro = None
        self.version = None

        # True if building under the Travis CI (http://travis-ci.org)
        self.travis = (
            os.environ.get('TRAVIS', '0') == 'true') and (
            os.environ.get('CI', '0') == 'true')

        if self.linux:
            self.distro, self.version, _ = platform.linux_distribution()
            self.__display = '%s %s (%s)' % (self.distro, self.version, self.name)

        elif self.osx:
            parts = platform.mac_ver()[0].split('.')
            while len(parts) < 3:
                parts.append('0')
            self.__display = '%s %s' % (self.name, '.'.join(parts))
        elif self.windows:
            release, version, csd, ptype = platform.win32_ver()
            self.__display = '%s %s %s (%s)' % (self.name, release, version, ptype)

        else:
            raise Exception('Unknown system platform "' + self.name + '"')

        self.platform = self.distro or self.name

    def __str__(self):
        return self.__display

class Git(object):
    """Provides information about git and the repository we are called from"""
    def __init__(self, env):
        self.tags = self.branch = self.user = ''
        self.exists = env.Detect('git')
        if self.exists:
            try:
                self.tags = _execute('git describe --tags')
                self.branch = _execute('git rev-parse --abbrev-ref HEAD')
                remote = _execute('git config remote.origin.url')
                self.user = remote.split(':')[1].split('/')[0]
            except:
                self.exists = False

system = __System()

#-------------------------------------------------------------------------------

def printChildren(target):
    def doPrint(tgt, level, found):
        for item in tgt:
            if SCons.Util.is_List(item):
                doPrint(item, level, found)
            else:
                if item.abspath in found:
                    continue
                found[item.abspath] = False
                print('\t'*level + item.path)
                #DoPrint(item.children(scan=1), level+1, found)
                item.scan()
                doPrint(item.all_children(), level+1, found)
    doPrint(target, 0, {})

def variantFile(path, variant_dirs):
    '''Returns the path to the corresponding dict entry in variant_dirs'''
    path = str(path)
    for dest, source in variant_dirs.items():
        common = os.path.commonprefix([path, source])
        if common == source:
            return os.path.join(dest, path[len(common)+1:])
    return path

def variantFiles(files, variant_dirs):
    '''Returns a list of files remapped to their variant directories'''
    result = []
    for path in files:
        result.append(variantFile(path, variant_dirs))
    return result

def printEnv(env, keys):
    if type(keys) != list:
        keys = list(keys)
    s = ''
    for key in keys:
        if key in env:
            value = env[key]
        else:
            value = ''
        s+=('%s=%s, ' % (key, value))
    print('[' + s + ']')

#-------------------------------------------------------------------------------
#
# Output
#
#-------------------------------------------------------------------------------

# See https://stackoverflow.com/questions/7445658/how-to-detect-if-the-console-does-support-ansi-escape-codes-in-python
CAN_CHANGE_COLOR = (
  hasattr(sys.stderr, "isatty")
  and sys.stderr.isatty()
  and not system.windows
  and not os.environ.get('INSIDE_EMACS')
  )

# See https://en.wikipedia.org/wiki/ANSI_escape_code
BLUE = 94
GREEN = 92
RED = 91
YELLOW = 93

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

def yellow(text):
    return add_mode(text, YELLOW)

def warn(text, print=print):
    print('%s %s' % (red('WARNING:'), text))

# Prints command lines using environment substitutions
def print_coms(coms, env):
    if type(coms) is str:
        coms=list(coms)
    for key in coms:
        cmdline = env.subst(env[key], 0,
            env.File('<target>'), env.File('<sources>'))
        print (green(cmdline))
