from __future__ import absolute_import, division, print_function

from ripple.util import Sign
from ripple.util import Base58
from ripple.ledger import SField

from unittest import TestCase

BINARY = 'nN9kfUnKTf7PpgLG'

class test_Sign(TestCase):
    SEQUENCE = 23
    VALIDATOR_KEY_HUMAN = 'n9JijuoCv8ubEy5ag3LiX3hyq27GaLJsitZPbQ6APkwx2MkUXq8E'

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
        ssk, spk = Sign.make_ecdsa_keypair(self.urandom)
        sk, pk = Sign.make_ed25519_keypair(self.urandom)
        s = Sign.sign_manifest('manifest', ssk, sk, pk)
        self.assertEquals(
            s, 'manifestvF0D\x02 \x04\x85\x95p\x0f\xb8\x17\x7f\xdf\xdd\x04'
            '\xaa+\x16q1W\xf6\xfd\xe8X\xb12l\xd5\xc3\xf1\xd6\x05\x1b\x1c\x9a'
            '\x02 \x18\\.(o\xa8 \xeb\x87\xfa&~\xbd\xe6,\xfb\xa61\xd1\xcd\xcd'
            '\xc8\r\x16[\x81\x9a\x19\xda\x93i\xcdp\x12@\xe5\x84\xbe\xc4\x80N'
            '\xa0v"\xb2\x80A\x88\x06\xc0\xd2\xbae\x92\x89\xa8\'!\xdd\x00\x88'
            '\x06s\xe0\xf74\xe3Yg\xad{$\x17\xd3\x99\xaa\x16\xb0\xeaZ\xd7]\r'
            '\xb3\xdc\x1b\x8f\xc1Z\xdfHU\xb5\x92\xac\x82jI\x02')

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

        Sign.check_validation_public_key(*public)
        Sign.check_secret_key(*private)

        return (Base58.encode_version(*public), Base58.encode_version(*private))

    def test_get_signature(self):
        pk, sk = self.get_test_keypair()
        signature = Sign.get_signature(self.SEQUENCE, pk, sk, sk)
        self.assertEquals(
            signature,
            'JAAAABdxIe2DIKUZd9jDjKikknxnDfWCHkSXYZReFenvsmoVCdIw6nMhAnZ2dnZ2'
            'dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dkYwRAIgXyobHA8sDQxmDJNLE6HI'
            'aARlzvcd79/wT068e113gUkCIHkI540JQT2LHwAD7/y3wFE5X3lEXMfgZRkpLZTx'
            'kpticBJAzo5VrUEr0U47sHvuIjbrINLCTM6pAScA899G9kpMWexbXv1ceIbTP5JH'
            '1HyQmsZsROTeHR0irojvYgx7JLaiAA==')

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
        Sign.perform_sign(self.SEQUENCE, public, private, private, print=self.print)
        self.assertEquals(
            self.results,
            [[['[validator_token]'], {}],
             [['eyJ2YWxpZGF0aW9uX3ByaXZhdGVfa2V5IjoiNmI2YjZiNmI2YjZiNmI2YjZiNmI2Yj\n'
               'ZiNmI2YjZiNmI2YjZiNmI2YjZiNmI2YjZiNmI2YjZiNmI2YjZiNmI2YiIsIm1hbmlm\n'
               'ZXN0IjoiSkFBQUFCZHhJZTJESUtVWmQ5akRqS2lra254bkRmV0NIa1NYWVpSZUZlbn\n'
               'ZzbW9WQ2RJdzZuTWhBbloyZG5aMmRuWjJkbloyZG5aMmRuWjJkbloyZG5aMmRuWjJk\n'
               'bloyZG5aMmRrWXdSQUlnWHlvYkhBOHNEUXhtREpOTEU2SElhQVJsenZjZDc5L3dUMD\n'
               'Y4ZTExM2dVa0NJSGtJNTQwSlFUMkxId0FENy95M3dGRTVYM2xFWE1mZ1pSa3BMWlR4\n'
               'a3B0aWNCSkF6bzVWclVFcjBVNDdzSHZ1SWpicklOTENUTTZwQVNjQTg5OUc5a3BNV2\n'
               'V4Ylh2MWNlSWJUUDVKSDFIeVFtc1pzUk9UZUhSMGlyb2p2WWd4N0pMYWlBQT09In0='],
              {}]])
