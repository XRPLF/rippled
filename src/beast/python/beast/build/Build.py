# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

from .Files import files, directories
from .Function import compose, for_tags, not_tags
from .Module import Module
from .State import State, run_build
from .Target import Target
from .Variant import Variant


def pkg_config(name):
    """Return a function that runs pkg-config for the tool"""
    return Env.ParseConfig('pkg-config --static --cflags --libs ' + name)


class __EnvClass(object):
    def __getattr__(self, name):
        def f(*args, **kwds):
            # build can be anything with a .env member - i.e. either a
            # Variant or a State.
            return lambda build: getattr(build.env, name)(*args, **kwds)
        return f


Env = __EnvClass()

"""Calls that change scon's Environment are one of the primary tools to set up
a build.  But it's usually necessary to defer that call until the environment is
passed in later, after construction.

Env's attributes are functions which return a deferred function call, so the
next two statements have the same effect:

       Env.Append(CXXFLAGS='-g')(scons_environment)
       scons_environment.Append(CXXFLAGS='-g')

"""
