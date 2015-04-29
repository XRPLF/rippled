from __future__ import division

from . import der, ecdsa

class UnknownCurveError(Exception):
    pass

def orderlen(order):
    return (1+len("%x"%order))//2 # bytes

# the NIST curves
class Curve:
    def __init__(self, name, openssl_name,
                 curve, generator, oid):
        self.name = name
        self.openssl_name = openssl_name # maybe None
        self.curve = curve
        self.generator = generator
        self.order = generator.order()
        self.baselen = orderlen(self.order)
        self.verifying_key_length = 2*self.baselen
        self.signature_length = 2*self.baselen
        self.oid = oid
        self.encoded_oid = der.encode_oid(*oid)

NIST192p = Curve("NIST192p", "prime192v1",
                 ecdsa.curve_192, ecdsa.generator_192,
                 (1, 2, 840, 10045, 3, 1, 1))
NIST224p = Curve("NIST224p", "secp224r1",
                 ecdsa.curve_224, ecdsa.generator_224,
                 (1, 3, 132, 0, 33))
NIST256p = Curve("NIST256p", "prime256v1",
                 ecdsa.curve_256, ecdsa.generator_256,
                 (1, 2, 840, 10045, 3, 1, 7))
NIST384p = Curve("NIST384p", "secp384r1",
                 ecdsa.curve_384, ecdsa.generator_384,
                 (1, 3, 132, 0, 34))
NIST521p = Curve("NIST521p", "secp521r1",
                 ecdsa.curve_521, ecdsa.generator_521,
                 (1, 3, 132, 0, 35))
SECP256k1 = Curve("SECP256k1", "secp256k1",
                  ecdsa.curve_secp256k1, ecdsa.generator_secp256k1,
                  (1, 3, 132, 0, 10))

curves = [NIST192p, NIST224p, NIST256p, NIST384p, NIST521p, SECP256k1]

def find_curve(oid_curve):
    for c in curves:
        if c.oid == oid_curve:
            return c
    raise UnknownCurveError("I don't know about the curve with oid %s."
                            "I only know about these: %s" %
                            (oid_curve, [c.name for c in curves]))
