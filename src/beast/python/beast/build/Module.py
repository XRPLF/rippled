# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import glob, os

"""
A *tag* is a string indicating properties of the build - examples include debug,
release, unity, nounity, ubuntu, windows, osx...

A tag name can never match the name of a platform.

A *tagset* is a collection of tags.  To negate a tag, use its name prefixed by
no_.

A *module command* is something that's executed when the module is used.

A module command is either:
   * A function, which is always called.

   * A list, where the first element is a tagset, and the remaining elements are
     functions which are only called if all the tags in the tagset match.

A *module* is a list of module commands.
"""


# TODO: get rid of no_.
def matching_commands(tags_to_match, module):
    """matching_commands() returns the commands from a module that match the
    tags.
    """
    for i, command in enumerate(module):
        command = [command] if callable(command) else list(command)
        tags = []
        while command and not callable(command[0]):
            tags.append(command.pop(0))
        assert command, 'Module rule contained no functions: ' + ', '.join(tags)
        matching = set(t for t in tags if not t.startswith('no_'))
        if matching.issubset(tags_to_match):
            negated = set(t[3:] for t in tags if t.startswith('no_'))
            if negated.isdisjoint(tags_to_match):
                for f in command:
                    yield f


def module(*commands):
    """Return a function that runs the module on tags, an environment and a
       scons context."""
    def run(tags, scons):
        for f in matching_commands(tags, commands):
            f(scons)
    return run


class Module(object):
    """The Module is the unit of code reuse.

    A module has public and private interfaces.  The private interface is used
    when you want to build the module from source; the public interface is used
    when you want to *refer* to that module (i.e. set the include path).

    In a typical module in a project, you'll use the public interface in the top
    level project to export settings like include paths that all compilation
    units will share, and use the private interface when you actually compile
    the code for this module.

    """

    def __init__(self, *run_private, **kwds):
        self.run_private = module(*run_private)
        self.run_public = module(*kwds.get('run_public', ()))
        self.run_once = kwds.get('run_once', lambda scons: None)


class EnvClass(object):
    """
    EnvClass has attributes that are functions that call methods on scons's
    Env class.

    Example: the next two statements have the same effect:
       EnvClass().Append(CXXFLAGS='-g')(env)
       env.Append(CXXFLAGS='-g')
    """
    def __getattr__(self, name):
        def f(*args, **kwds):
            return lambda scons: getattr(scons.env, name)(*args, **kwds)
        return f


# Env is the unique instance of EnvClass.  It's really useful for creating
# modules.
Env = EnvClass()


def applier(f, *args, **kwds):
    """Return a function that applies f to a Scons.
    """
    return lambda scons: f(scons, *args, **kwds)


def pkg_config(name):
    """Return a function that runs pkg-config for the tool"""
    return Env.ParseConfig('pkg-config --static --cflags --libs ' + name)


def _all_globs(globs):
    for g in globs:
        files = glob.glob(g)
        if files:
            for f in files:
                yield f
        else:
            print('ERROR: Glob %s didn\'t match any files.' % g)


def files(*globs, **kwds):
    """Return a function that adds source files.
    """
    def run(scons):
        scons.clone().add_source_files(*_all_globs(globs), **kwds)
    return run


def directories(*globs, **kwds):
    """Return a function that adds source files recursively contained in
    directories.
    """
    def run(scons):
        scons.clone().add_source_by_directory(*_all_globs(globs), **kwds)

    return run

def file_module(*globs, **kwds):
    return Module(files(*globs, **kwds))

def directory_module(*globs, **kwds):
    return Module(directories(*globs, **kwds))
