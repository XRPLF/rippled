from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util.Function import Function, MATCHER

from unittest import TestCase

def FN(*args, **kwds):
    return args, kwds

class test_Function(TestCase):
    def match_test(self, item, *results):
        self.assertEquals(MATCHER.match(item).groups(), results)

    def test_simple(self):
        self.match_test('function', 'function', '')
        self.match_test('f(x)', 'f', '(x)')

    def test_empty_function(self):
        self.assertEquals(Function()(), None)

    def test_function(self):
        f = Function('ripple.util.test_Function.FN(True, {1: 2}, None)')
        self.assertEquals(f(), ((True, {1: 2}, None), {}))
        self.assertEquals(f('hello', foo='bar'),
                          (('hello', True, {1: 2}, None), {'foo':'bar'}))
