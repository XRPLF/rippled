from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util.Cache import NamedCache

from unittest import TestCase

class test_Cache(TestCase):
    def setUp(self):
        self.cache = NamedCache()

    def test_trivial(self):
        pass
