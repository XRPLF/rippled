from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util.Decimal import Decimal

from unittest import TestCase

class test_Decimal(TestCase):
    def test_construct(self):
        self.assertEquals(str(Decimal('')), '0')
        self.assertEquals(str(Decimal('0')), '0')
        self.assertEquals(str(Decimal('0.2')), '0.2')
        self.assertEquals(str(Decimal('-0.2')), '-0.2')
        self.assertEquals(str(Decimal('3.1416')), '3.1416')

    def test_accumulate(self):
        d = Decimal()
        d.accumulate('0.5')
        d.accumulate('3.1416')
        d.accumulate('-23.34234')
        self.assertEquals(str(d), '-19.70074')
