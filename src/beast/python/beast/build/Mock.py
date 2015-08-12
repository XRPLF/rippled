# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from beast.build.Build import State

class MockSConstructEnvironment(dict):
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


def MockSConstruct(**sconstruct):
    def environment(*args, **kwds):
        sc = dict(sconstruct)
        sc.update(kwds)
        return MockSConstructEnvironment(*args, **sc)
    return {'Environment':  environment,
            'AddOption': lambda *arg, **kwds: None,
            'GetOption': lambda name: None}


def MockState(environ=None, sconstruct=None, tags=None):
    mock_sconstruct = MockSConstruct(**(sconstruct or {}))
    return State(mock_sconstruct, environ=environ or {}, tags=tags or [])
