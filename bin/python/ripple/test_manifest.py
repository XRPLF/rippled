from __future__ import absolute_import, division, print_function

from ripple import manifest

from unittest import TestCase

BINARY = 'nN9kfUnKTf7PpgLG'

class test_manifest(TestCase):
    def test_check(self):
        self.assertEquals(manifest.ra_check(BINARY), '\xaa\xaar\x9d')

    def test_encode(self):
        self.assertEquals(
            manifest.ra_encode(manifest.VER_ACCOUNT_PUBLIC, BINARY),
            'sB49XwJgmdEZDo8LmYwki7FYkiaN7')

    def test_decode(self):
        ver, b = manifest.ra_decode('sB49XwJgmdEZDo8LmYwki7FYkiaN7')
        self.assertEquals(ver, manifest.VER_ACCOUNT_PUBLIC)
        self.assertEquals(b, BINARY)

    def test_field_code(self):
        self.assertEquals(manifest.field_code(manifest.STI_UINT32, 4), '$')
        self.assertEquals(manifest.field_code(manifest.STI_VL, 1), 'q')
        self.assertEquals(manifest.field_code(manifest.STI_VL, 3), 's')
        self.assertEquals(manifest.field_code(manifest.STI_VL, 6), 'v')

    def test_to_bytes(self):
        self.assertEquals(
            manifest.to_bytes(12345, 16, endianess='big'),
            '\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0009')

        self.assertEquals(
            manifest.to_bytes(12345, 16, endianess='not big'),
            '90\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00')

    def test_strvl(self):
        self.assertEquals(manifest.strvl(BINARY), '\x10nN9kfUnKTf7PpgLG')

    def test_to_str32(self):
        self.assertEquals(manifest.str32(12345), '\x00\x0009')

    def urandom(self, bytes):
        return '\5' * bytes

    def test_gen_seed(self):
        self.assertEquals(manifest.gen_seed(self.urandom),
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5')

    def test_gen_ed(self):
        private, public = manifest.gen_ed(self.urandom)
        self.assertEquals(private,
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5'
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5')
        self.assertEquals(public,
                          'nz\x1c\xdd)\xb0\xb7\x8f\xd1:\xf4\xc5Y\x8f\xef\xf4'
                          '\xef*\x97\x16n<\xa6\xf2\xe4\xfb\xfc\xcd\x80P[\xf1')

    def test_gen_manifest(self):
        _, pk = manifest.gen_ed(self.urandom)
        m = manifest.gen_manifest(pk, 'verify', 12345)
        self.assertEquals(
            m, '$\x00\x0009q nz\x1c\xdd)\xb0\xb7\x8f\xd1:\xf4\xc5Y\x8f\xef\xf4'
            '\xef*\x97\x16n<\xa6\xf2\xe4\xfb\xfc\xcd\x80P[\xf1s\x06verify')

    def test_sign_manifest(self):
        sk, pk = manifest.gen_ed(self.urandom)
        s = manifest.sign_manifest('manifest', sk, pk)
        self.assertEquals(
            s, 'manifestv@\xe5\x84\xbe\xc4\x80N\xa0v"\xb2\x80A\x88\x06\xc0'
            '\xd2\xbae\x92\x89\xa8\'!\xdd\x00\x88\x06s\xe0\xf74\xe3Yg\xad{$'
            '\x17\xd3\x99\xaa\x16\xb0\xeaZ\xd7]\r\xb3\xdc\x1b\x8f\xc1Z\xdfHU'
            '\xb5\x92\xac\x82jI\x02')

    def test_wrap(self):
        wrap = lambda s: manifest.wrap(s, 5)
        self.assertEquals(wrap(''), '')
        self.assertEquals(wrap('12345'), '12345')
        self.assertEquals(wrap('123456'), '123\n456\n')
        self.assertEquals(wrap('12345678'), '1234\n5678\n')
        self.assertEquals(wrap('1234567890'), '12345\n67890\n')
        self.assertEquals(wrap('12345678901'), '123\n456\n789\n01')
        # TOD: there seems to be a carriage return added randomly
        # to the last character.
