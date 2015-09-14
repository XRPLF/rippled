# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

from collections import OrderedDict

import os

from ..System import SYSTEM

# http://stackoverflow.com/a/377028/43839
def which(program, environ=os.environ):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in environ['PATH'].split(os.pathsep):
            path = path.strip('"')
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file

    return None


def where_is(env, filename, environ=os.environ):
    # Use State's WhereIs if available, otherwise the replacement.
    return getattr(env, 'WhereIs', which)(filename)


class Toolchain(object):
    """A Toolchain represents a single build toolchain: clang, gcc or msvc.

    """
    def __init__(self, name, env_prefix, cc_exe, cxx_exe, cc_match, cxx_match):
        """
          name:       name of the toolchain
          env_prefix: prefix for environment variables
          cc_exe:     path to the C compiler
          cxx_exe:    path to the C++ compiler
          cc_match:   required match for C compiler names
          cxx_match:  required match for C++ compiler names.

        """
        self.name = name
        self.env_prefix = env_prefix
        self.exes = {'CC': cc_exe, 'CXX': cxx_exe, 'LINK': cxx_exe}
        self.matches = {'CC': cc_match, 'CXX': cxx_match, 'LINK': cxx_match}
        self.variable_names = [self.env_name(t) for t in self.exes]

    def env_name(self, tool):
        return '%s_%s' % (self.env_prefix, tool)

    def detect(self, state):
        """Return a dictionary mapping CC, CXX, and LINK to the correct tool
           paths depending on the environment, or false-y if the environment
           doesn't support this toolchain.

        This is supporting legacy operation which we might want to change
        or clarify to end users - but here's how it goes:

          * Look for triplets for each compiler of toolchain specific
            environment variables: eg for clang, we're looking for
            CLANG_CC, CLANG_CXX, CLANG_LINK.

          * If there are any such triplets, then return a dictionary with all
            of them.

          * Otherwise, look at the "generic" environment variables CC, CXX, LINK
            and if all of them refer to a compiler in a toolchain, return
            that dictionary.

          * Otherwise, return the "default" a dictionary containing each valid
            compiler where we find all three tools on the default build bath.
        """
        return (self._specific(state) or
                self._generic(state) or
                self._default(state))

    def _specific(self, state):
        """Return a dictionary if the three toolchain specific environment
        variables are set - example CLANG_CC, CLANG_CXX, CLANG_LINK."""
        result = {}
        names = []
        for tool in self.exes:
            name = self.env_name(tool)
            names.append(name)
            value = state.get_environment_variable(name)
            if value:
                result[tool] = value

        if result and len(result) != 3:
            raise ValueError(
                'The environment variables %s must all be set or all unset: %s.'
                % (', '.join(names), result))
        return result

    def _generic(self, state):
        """Returns a dictionary containing mappings taken from the three generic
        environment variables CC, CXX, LINK *if* those values refer to tools
        from the current toolchain."""
        result = {}
        for tool, match in self.matches.items():
            value = state.get_environment_variable(tool)
            if value and match in value:
                result[tool] = value
            else:
                return
        return result

    def _default(self, state):
        """Return a dictionary containing mappings for the default tools
        for this toolchain - e.g. gcc, g++. """
        for exe in self.exes.values():
            if not where_is(state.env, exe, state.environ):
                return
        return self.exes

    def __str__(self):
        return self.name

    def __repr__(self):
        return 'Toolchain.' + self.name.upper()

    def __cmp__(self, other):
        return cmp(self.name, other.name)


class VisualStudio(Toolchain):
    """""VisualStudio's toolchain is much easier"""
    def __init__(self):
        self.name = 'msvc'
        self.variable_names = []

    def detect(self, state):
        if state.env.Detect('cl'):
            return {'CC': 'cl', 'CXX': 'cl', 'LINK': 'cl'}


CLANG = Toolchain('clang', 'CLANG', 'clang', 'clang++', 'clang', 'clang')
GCC = Toolchain('gcc', 'GNU', 'gcc', 'g++', 'gcc', 'g++')
MSVC = VisualStudio()

# Arranged in *decreasing* order of preference.
TOOLCHAINS = MSVC, GCC, CLANG


def detect(env):
    if SYSTEM.osx:
        # On OS/X, clang masquerades as gcc. Hard-coding this is easiest.
        result = [[CLANG, {'CC': 'clang', 'CXX': 'clang++', 'LINK': 'clang++'}]]
    else:
        result = [[t, t.detect(env)] for t in TOOLCHAINS]
    return OrderedDict((t.name, v) for t, v in result if v)
