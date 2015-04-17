#!/usr/bin/env python

from hashlib import sha256

#
# Human strings are base-58 with a
# version prefix and a checksum suffix.
#
# Copied from ripple/protocol/RippleAddress.h
#

VER_NONE              = 1
VER_NODE_PUBLIC       = 28
VER_NODE_PRIVATE      = 32
VER_ACCOUNT_ID        = 0
VER_ACCOUNT_PUBLIC    = 35
VER_ACCOUNT_PRIVATE   = 34
VER_FAMILY_GENERATOR  = 41
VER_FAMILY_SEED       = 33

ALPHABET = 'rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz'

VERSION_NAME = {
    VER_NONE: 'VER_NONE',
    VER_NODE_PUBLIC: 'VER_NODE_PUBLIC',
    VER_NODE_PRIVATE: 'VER_NODE_PRIVATE',
    VER_ACCOUNT_ID: 'VER_ACCOUNT_ID',
    VER_ACCOUNT_PUBLIC: 'VER_ACCOUNT_PUBLIC',
    VER_ACCOUNT_PRIVATE: 'VER_ACCOUNT_PRIVATE',
    VER_FAMILY_GENERATOR: 'VER_FAMILY_GENERATOR',
    VER_FAMILY_SEED: 'VER_FAMILY_SEED'
}

class Alphabet(object):
    def __init__(self, radix, digit_to_char, char_to_digit):
        self.radix = radix
        self.digit_to_char = digit_to_char
        self.char_to_digit = char_to_digit

    def transcode_from(self, s, source_alphabet):
        n, zero_count = source_alphabet._digits_to_number(s)
        digits = []
        while n > 0:
            n, digit = divmod(n, self.radix)
            digits.append(self.digit_to_char(digit))

        s = ''.join(digits)
        return self.digit_to_char(0) * zero_count + s[::-1]

    def _digits_to_number(self, digits):
        stripped = digits.lstrip(self.digit_to_char(0))
        n = 0
        for d in stripped:
            n *= self.radix
            n += self.char_to_digit(d)
        return n, len(digits) - len(stripped)

_INVERSE_INDEX = dict((c, i) for (i, c) in enumerate(ALPHABET))

# In base 58 encoding, the digits come from the ALPHABET string.
BASE58 = Alphabet(len(ALPHABET), ALPHABET.__getitem__, _INVERSE_INDEX.get)

# In base 256 encoding, each digit is just a character between 0 and 255.
BASE256 = Alphabet(256, chr, ord)

def encode(b):
    return BASE58.transcode_from(b, BASE256)

def decode(b):
    return BASE256.transcode_from(b, BASE58)

def checksum(b):
    """Returns a 4-byte checksum of a binary."""
    return sha256(sha256(b).digest()).digest()[:4]

def encode_version(ver, b):
    """Encodes a version encoding and a binary as human string."""
    b = chr(ver) + b
    return encode(b + checksum(b))

def decode_version(s):
    """Decodes a human base-58 string into its version encoding and binary."""
    b = decode(s)
    body, check = b[:-4], b[-4:]
    assert check == checksum(body), ('Bad checksum for', s)
    return ord(body[0]), body[1:]

def version_name(ver):
    return VERSION_NAME.get(ver) or ('(unknown version %s)' % ver)

def check_version(version, expected):
    if version != expected:
        raise ValueError('Expected version %s but got %s' % (
            version_name(version), version_name(expected)))
