from __future__ import absolute_import, division, print_function, unicode_literals

from unittest import TestCase

from beast.util import Dict

DICT = {
    '': {
        'foo': 'foo-default',
        'bar': 'bar-default',
      },

    'Darwin': {
        'foo': 'foo-darwin',
        'baz': 'baz-darwin',
      },

    'Darwin.10.8': {
        'foo': 'foo-darwin-10.8',
        'bing': 'bing-darwin-10.8',
      },
  }

class test_Dict(TestCase):
    def computeMapValue(self, config, key):
        return Dict.compose(*Dict.get_items_with_prefix(config, DICT))[key]

    def assertMapValue(self, config, key, result):
        self.assertEquals(self.computeMapValue(config, key), result)

    def testDefault1(self):
        self.assertMapValue('', 'foo', 'foo-default')

    def testDefault2(self):
        self.assertMapValue('Darwin.10.8', 'bar', 'bar-default')

    def testPrefix1(self):
        self.assertMapValue('Darwin', 'foo', 'foo-darwin')

    def testPrefix2(self):
        self.assertMapValue('Darwin.10.8', 'foo', 'foo-darwin-10.8')

    def testPrefix3(self):
        self.assertMapValue('Darwin', 'baz', 'baz-darwin')

    def testPrefix4(self):
        self.assertMapValue('Darwin.10.8', 'bing', 'bing-darwin-10.8')

    def testFailure1(self):
        self.assertRaises(KeyError, self.computeMapValue, '', 'baz')

    def testFailure2(self):
        self.assertRaises(KeyError, self.computeMapValue, '', 'bing')

    def testFailure2(self):
        self.assertRaises(KeyError, self.computeMapValue, 'Darwin', 'bing')
