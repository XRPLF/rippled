from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util import Range

from unittest import TestCase

class test_Range(TestCase):
    def round_trip(self, s, *items):
        self.assertEquals(Range.from_string(s), set(items))
        self.assertEquals(Range.to_string(items), s)

    def test_complete(self):
        self.round_trip('10,19', 10, 19)
        self.round_trip('10', 10)
        self.round_trip('10-12', 10, 11, 12)
        self.round_trip('10,19,42-45', 10, 19, 42, 43, 44, 45)

    def test_names(self):
        self.assertEquals(
            Range.from_string('first,last,current', first=1, last=3, current=5),
            set([1, 3, 5]))

    def test_is_range(self):
        self.assertTrue(Range.is_range(''))
        self.assertTrue(Range.is_range('10'))
        self.assertTrue(Range.is_range('10,12'))
        self.assertFalse(Range.is_range('10,12,fred'))
        self.assertTrue(Range.is_range('10,12,fred', 'fred'))
