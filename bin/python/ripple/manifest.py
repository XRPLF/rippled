#!/usr/bin/env python

import base64, os, random, sys
import ed25519
import ecdsa
from hashlib import sha256
from ripple.util.Encode import base58encode, base58decode

#-----------------------------------------------------------

#
# RippleAddress library
#
# Human strings are base-58 with a
# version prefix and a checksum suffix.
#

VER_NONE              = 1
VER_NODE_PUBLIC       = 28
VER_NODE_PRIVATE      = 32
VER_ACCOUNT_ID        = 0
VER_ACCOUNT_PUBLIC    = 35
VER_ACCOUNT_PRIVATE   = 34
VER_FAMILY_GENERATOR  = 41
VER_FAMILY_SEED       = 33

def ra_check(b):
    """.Returns a 4-byte checksum of a binary."""
    return sha256(sha256(b).digest()).digest()[:4]

def ra_encode(ver, b):
    """Encodes a binary as human string."""
    b = chr(ver) + b
    return base58encode(b + ra_check(b))

def ra_decode(s):
    """Decodes a human base-58 string into its version and binary."""
    b = base58decode(s)
    check = b[-4:]
    if (check != ra_check(b[:-4])):
        raise ValueError('bad checksum')
    ver = ord(b[0])
    b = b[1:-4]
    return ver, b

def field_code(kind, name):
    s = ""
    if (kind < 16):
        if (name < 16):
            s += chr((kind << 4) + name)
        else:
            s += chr(kind << 4)
            s += chr(name)
    elif (name < 16):
        s += chr(name)
        s += chr(kind)
    else:
        s += '\0'
        s += chr(kind)
        s += chr(name)
    return s

#-----------------------------------------------------------

STI_UINT32          = 2
STI_VL              = 7

sfSequence          = field_code(STI_UINT32, 4)
sfPublicKey         = field_code(STI_VL, 1)
sfSigningPubKey     = field_code(STI_VL, 3)
sfSignature         = field_code(STI_VL, 6)

def to_bytes(n, length, endianess='big'):
    h = '%x' % n
    s = ('0'*(len(h) % 2) + h).zfill(length*2).decode('hex')
    return s if endianess == 'big' else s[::-1]

def lenvl(b):
    s = ''
    n = len(b)
    if (n < 192):
        s += chr(n)
        return s
    raise Exception('too long')

def strvl(b):
    return lenvl(b) + b

def str32(n):
    return to_bytes(n, 4)

#-----------------------------------------------------------

def gen_seed(urandom=os.urandom):
    seed = urandom(16)
    return seed

def gen_ed(urandom=os.urandom):
    sk = urandom(32)
    pk = ed25519.publickey(sk)
    return sk, pk

def gen_ec():
    # Can't be unit tested easily.
    sk = ecdsa.SigningKey.generate(curve=ecdsa.SECP256k1)
    vk = sk.get_verifying_key()
    sig = sk.sign("message")
    assert vk.verify(sig, "message")
    return sk, vk

def gen_manifest(pk, vpk, seq):
    s = ""
    s += sfSequence
    s += str32(seq)
    s += sfPublicKey
    s += strvl(pk)
    s += sfSigningPubKey
    s += strvl(vpk)
    return s

def sign_manifest(m, sk, pk):
    s = "MAN\0"
    s += m
    sig = ed25519.signature(s, sk, pk)
    m += sfSignature + strvl(sig)
    return m

#-----------------------------------------------------------

def wrap(s, cols = 60):
    n = len(s)
    if (n <= cols):
        return s
    l = (n + cols - 1) / cols
    w = n / l
    while(l > 0):
        s = s[:w * l] + '\n' + s[w*l:]
        l -= 1
    return s

#-----------------------------------------------------------

if __name__ == '__main__':
    if (len(sys.argv) == 2 and sys.argv[1] == 'create'):
        sk, pk = gen_ed()
        apk = chr(0xed) + pk
        pkh = ra_encode(VER_NODE_PUBLIC, apk)
        skh = ra_encode(VER_NODE_PRIVATE, sk)
        v0, apk0 = ra_decode(pkh)
        assert v0 == VER_NODE_PUBLIC
        assert apk0 == apk
        print ("[validators]")
        print (pkh)
        print
        print ("[master_secret]")
        print (skh)
        exit()

    if (len(sys.argv) == 3 and sys.argv[1] == 'check'):
        ver, b = ra_decode(sys.argv[2])
        print ('ver = ' + str(ver))
        print ('len = ' + str(len(b)))
        h = ra_encode(ver, b)
        print (h)
        assert h == sys.argv[2]
        exit()

    if (len(sys.argv) == 5 and sys.argv[1] == 'sign'):
        seq = int(sys.argv[2])
        vpkh = sys.argv[3]
        skh = sys.argv[4]
        try:
            v, avpk = ra_decode(vpkh)
            if (v != VER_NODE_PUBLIC or
                len(avpk) != 33 or
                (ord(avpk[0]) != 2 and ord(avpk[0]) != 3)):
                raise ValueError()
        except ValueError:
            print ("Bad validator-public: " + vpkh)
            exit()
        try:
            v, sk = ra_decode(skh)
            if (v != VER_NODE_PRIVATE or
                len(sk) != 32):
                raise ValueError()
        except ValueError:
            print ("Bad master-secret: " + skh)
        pk = ed25519.publickey(sk)
        apk = chr(0xed) + pk
        m = gen_manifest(apk, avpk, seq)
        m1 = sign_manifest(m, sk, pk)
        print ('[validation_manifest]')
        print wrap(base64.b64encode(m1))
        exit()

    print("""\
    Usage:
        create
            Create a new master public/secret key pair.

        sign <sequence> <validator-public> <master-secret>
            Create a new signed manifest with the given sequence
            number, validator public key, and master secret key.
    """)
