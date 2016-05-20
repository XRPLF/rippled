from __future__ import absolute_import, division, print_function

from ripple.util import Sign
from ripple.util import Base58
from ripple.ledger import SField

from unittest import TestCase

BINARY = 'nN9kfUnKTf7PpgLG'

class test_Sign(TestCase):
    SEQUENCE = 23
    SIGNATURE = (
        'JAAAABdxIe2DIKUZd9jDjKikknxnDfWCHkSXYZReFenvsmoVCdIw6nMhAnZ2dnZ2'
        'dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dkDOjlWtQSvRTjuwe+4iNusg0sJM'
        'zqkBJwDz30b2SkxZ7Fte/Vx4htM/kkfUfJCaxmxE5N4dHSKuiO9iDHsktqIA')
    VALIDATOR_KEY_HUMAN = 'n9JijuoCv8ubEy5ag3LiX3hyq27GaLJsitZPbQ6APkwx2MkUXq8E'
    SIGNATURE_HEX = (
        '0a1546caa29c887f9fcb5e6143ea101b31fb5895a5cdfa24939301c66ff51794'
        'a0b729e0ebbf576f2cc7cdb9f68c2366324a53b8e1ecf16f3c17bebbdb8d7102')

    def setUp(self):
        self.results = []

    def print(self, *args, **kwds):
        self.results.append([list(args), kwds])

    def test_field_code(self):
        self.assertEquals(SField.field_code(SField.STI_UINT32, 4), '$')
        self.assertEquals(SField.field_code(SField.STI_VL, 1), 'q')
        self.assertEquals(SField.field_code(SField.STI_VL, 3), 's')
        self.assertEquals(SField.field_code(SField.STI_VL, 6), 'v')

    def test_strvl(self):
        self.assertEquals(Sign.prepend_length_byte(BINARY),
                          '\x10nN9kfUnKTf7PpgLG')

    def urandom(self, bytes):
        return '\5' * bytes

    def test_make_seed(self):
        self.assertEquals(Sign.make_seed(self.urandom),
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5')

    def test_make_ed(self):
        private, public = Sign.make_ed25519_keypair(self.urandom)
        self.assertEquals(private,
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5'
                          '\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5')
        self.assertEquals(public,
                          'nz\x1c\xdd)\xb0\xb7\x8f\xd1:\xf4\xc5Y\x8f\xef\xf4'
                          '\xef*\x97\x16n<\xa6\xf2\xe4\xfb\xfc\xcd\x80P[\xf1')

    def test_make_manifest(self):
        _, pk = Sign.make_ed25519_keypair(self.urandom)
        m = Sign.make_manifest(pk, 'verify', 12345)
        self.assertEquals(
            m, '$\x00\x0009q nz\x1c\xdd)\xb0\xb7\x8f\xd1:\xf4\xc5Y\x8f\xef\xf4'
            '\xef*\x97\x16n<\xa6\xf2\xe4\xfb\xfc\xcd\x80P[\xf1s\x06verify')

    def test_sign_manifest(self):
        sk, pk = Sign.make_ed25519_keypair(self.urandom)
        s = Sign.sign_manifest('manifest', sk, pk)
        self.assertEquals(
            s, 'manifestv@\xe5\x84\xbe\xc4\x80N\xa0v"\xb2\x80A\x88\x06\xc0'
            '\xd2\xbae\x92\x89\xa8\'!\xdd\x00\x88\x06s\xe0\xf74\xe3Yg\xad{$'
            '\x17\xd3\x99\xaa\x16\xb0\xeaZ\xd7]\r\xb3\xdc\x1b\x8f\xc1Z\xdfHU'
            '\xb5\x92\xac\x82jI\x02')

    def test_wrap(self):
        wrap = lambda s: Sign.wrap(s, 5)
        self.assertEquals(wrap(''), '')
        self.assertEquals(wrap('12345'), '12345')
        self.assertEquals(wrap('123456'), '123\n456')
        self.assertEquals(wrap('12345678'), '1234\n5678')
        self.assertEquals(wrap('1234567890'), '12345\n67890')
        self.assertEquals(wrap('12345678901'), '123\n456\n789\n01')

    def test_create_ed_keys(self):
        pkh, skh = Sign.create_ed_keys(self.urandom)
        self.assertEquals(
            pkh, 'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY')
        self.assertEquals(
            skh, 'pnEp13Zu7xTeKQVQ2RZVaUraE9GXKqFtnXQVUFKXbTE6wsP4wne')

    def test_create_ed_public_key(self):
        pkh = Sign.create_ed_public_key(
            'pnEp13Zu7xTeKQVQ2RZVaUraE9GXKqFtnXQVUFKXbTE6wsP4wne')
        self.assertEquals(
            pkh, 'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY')

    def get_test_keypair(self):
        public = (Base58.VER_NODE_PUBLIC, '\x02' + (32 * 'v'))
        private = (Base58.VER_NODE_PRIVATE, 32 * 'k')

        Sign.check_validator_public(*public)
        Sign.check_master_secret(*private)

        return (Base58.encode_version(*public), Base58.encode_version(*private))

    def test_get_signature(self):
        signature = Sign.get_signature(self.SEQUENCE, *self.get_test_keypair())
        self.assertEquals(
            signature,
            'JAAAABdxIe2DIKUZd9jDjKikknxnDfWCHkSXYZReFenvsmoVCdIw6nMhAnZ2dnZ2'
            'dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dkDOjlWtQSvRTjuwe+4iNusg0sJM'
            'zqkBJwDz30b2SkxZ7Fte/Vx4htM/kkfUfJCaxmxE5N4dHSKuiO9iDHsktqIA')

    def test_verify_signature(self):
        Sign.verify_signature(self.SEQUENCE, self.VALIDATOR_KEY_HUMAN,
            'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY',
            self.SIGNATURE_HEX)

    def test_check(self):
        public = Base58.encode_version(Base58.VER_NODE_PRIVATE, 32 * 'k')
        Sign.perform_check(public, self.print)
        self.assertEquals(self.results,
                          [[['version        = VER_NODE_PRIVATE'], {}],
                           [['decoded length = 32'], {}]])

    def test_create(self):
        Sign.perform_create(self.urandom, self.print)
        self.assertEquals(
            self.results,
            [[['[validator_keys]',
               'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY',
               '',
               '[master_secret]',
               'pnEp13Zu7xTeKQVQ2RZVaUraE9GXKqFtnXQVUFKXbTE6wsP4wne'],
              {'sep': '\n'}]])

    def test_create_public(self):
        Sign.perform_create_public(
            'pnEp13Zu7xTeKQVQ2RZVaUraE9GXKqFtnXQVUFKXbTE6wsP4wne', self.print)
        self.assertEquals(
            self.results,
            [[['[validator_keys]',
               'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY',
               '',
               '[master_secret]',
               'pnEp13Zu7xTeKQVQ2RZVaUraE9GXKqFtnXQVUFKXbTE6wsP4wne'],
              {'sep': '\n'}]])

    def test_sign(self):
        public, private = self.get_test_keypair()
        Sign.perform_sign(self.SEQUENCE, public, private, print=self.print)
        self.assertEquals(
            self.results,
            [[['[validation_manifest]'], {}],
             [['JAAAABdxIe2DIKUZd9jDjKikknxnDfWCHkSXYZReFenvsmo\n'
               'VCdIw6nMhAnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dn\n'
               'Z2dnZ2dkDOjlWtQSvRTjuwe+4iNusg0sJMzqkBJwDz30b2S\n'
               'kxZ7Fte/Vx4htM/kkfUfJCaxmxE5N4dHSKuiO9iDHsktqIA'],
              {}]])

    def test_verify(self):
        Sign.perform_verify(self.SEQUENCE, self.VALIDATOR_KEY_HUMAN,
            'nHUUaKHpxyRP4TZZ79tTpXuTpoM8pRNs5crZpGVA5jdrjib5easY',
            self.SIGNATURE_HEX, print=self.print)
