# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from beast.build.Scons import Scons

class MockSconsEnv(dict):
    def __init__(self, *binaries, **env):
        self.is_vs = bool(env.pop('is_vs', False))
        self.update(env)
        self.binaries = binaries;
        self.appends = []

    def WhereIs(self, b):
        return (b in self.binaries) and ('/usr/bin/' + b)

    def subst(self, value):
        return value  # TODO: doesn't handle $ variables.

    def Detect(self, name):
        return (name == 'cl') == self.is_vs

    def Append(self, **kwds):
        self.appends.append(kwds)


def MockSConstruct(**sconstruct_globals):
    def environment(*args, **kwds):
        sc = dict(sconstruct_globals)
        sc.update(kwds)
        return MockSconsEnv(*args, **sc)
    return {'Environment':  environment,
            'AddOption': lambda *arg, **kwds: None,
            'GetOption': lambda name: None}


def MockScons(environ=None, sconstruct_globals=None):
    scons = MockSConstruct(**(sconstruct_globals or {}))
    return Scons(scons, environ=environ or {})
