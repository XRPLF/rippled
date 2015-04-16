from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util.Search import binary_search, linear_search, FIRST, LAST

from unittest import TestCase

class test_Search(TestCase):
    def condition(self, i):
        return 10 <= i < 15;

    def test_linear_full(self):
        self.assertEquals(list(linear_search(range(21), self.condition)),
                          [10, 11, 12, 13, 14])

    def test_linear_partial(self):
        self.assertEquals(list(linear_search(range(8, 14), self.condition)),
                          [10, 11, 12, 13])
        self.assertEquals(list(linear_search(range(11, 14), self.condition)),
                          [11, 12, 13])
        self.assertEquals(list(linear_search(range(12, 18), self.condition)),
                          [12, 13, 14])

    def test_linear_empty(self):
        self.assertEquals(list(linear_search(range(1, 4), self.condition)), [])

    def test_binary_first(self):
        self.assertEquals(binary_search(0, 14, self.condition, FIRST), 10)
        self.assertEquals(binary_search(10, 19, self.condition, FIRST), 10)
        self.assertEquals(binary_search(14, 14, self.condition, FIRST), 14)
        self.assertEquals(binary_search(14, 15, self.condition, FIRST), 14)
        self.assertEquals(binary_search(13, 15, self.condition, FIRST), 13)

    def test_binary_last(self):
        self.assertEquals(binary_search(10, 20, self.condition, LAST), 14)
        self.assertEquals(binary_search(0, 14, self.condition, LAST), 14)
        self.assertEquals(binary_search(14, 14, self.condition, LAST), 14)
        self.assertEquals(binary_search(14, 15, self.condition, LAST), 14)
        self.assertEquals(binary_search(13, 15, self.condition, LAST), 14)

    def test_binary_throws(self):
        self.assertRaises(
            ValueError, binary_search, 0, 20, self.condition, LAST)
        self.assertRaises(
            ValueError, binary_search, 0, 20, self.condition, FIRST)
