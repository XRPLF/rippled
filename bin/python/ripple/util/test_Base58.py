from __future__ import absolute_import, division, print_function

from ripple.util import Base58

from unittest import TestCase

BINARY = 'nN9kfUnKTf7PpgLG'

class test_Base58(TestCase):
    def run_test(self, before, after):
        self.assertEquals(Base58.decode(before), after)
        self.assertEquals(Base58.encode(after), before)

    def test_trivial(self):
        self.run_test('', '')

    def test_zeroes(self):
        for before, after in (('', ''), ('abc', 'I\x8b')):
            for i in range(1, 257):
                self.run_test('r' * i + before, '\0' * i + after)

    def test_single_digits(self):
        for i, c in enumerate(Base58.ALPHABET):
            self.run_test(c, chr(i))

    def test_various(self):
        # Test three random numbers.
        self.run_test('88Mw', '\x88L\xed')
        self.run_test(
            'nN9kfUnKTf7PpgLG', '\x03\xdc\x9co\xdea\xefn\xd3\xb8\xe2\xc1')
        self.run_test(
            'zzWWb4C5p6kNrVa4fEBoZpZKd3XQLXch7QJbLCuLdoS1CWr8qdAZHEmwMiJy8Hwp',
            'xN\x82\xfcQ\x1f\xb3~\xdf\xc7\xb37#\xc6~A\xe9\xf6-\x1f\xcb"\xfab'
            '(\'\xccv\x9e\x85\xc3\xd1\x19\x941{\x8et\xfbS}\x86.k\x07\xb5\xb3')

    def test_check(self):
        self.assertEquals(Base58.checksum(BINARY), '\xaa\xaar\x9d')

    def test_encode(self):
        self.assertEquals(
            Base58.encode_version(Base58.VER_ACCOUNT_PUBLIC, BINARY),
            'sB49XwJgmdEZDo8LmYwki7FYkiaN7')

    def test_decode(self):
        ver, b = Base58.decode_version('sB49XwJgmdEZDo8LmYwki7FYkiaN7')
        self.assertEquals(ver, Base58.VER_ACCOUNT_PUBLIC)
        self.assertEquals(b, BINARY)
