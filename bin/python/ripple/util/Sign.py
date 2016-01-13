#!/usr/bin/env python

from __future__ import print_function

import base64, os, random, struct, sys
import ed25519
import ecdsa
from ripple.util import Base58
from ripple.ledger import SField

ED25519_BYTE = chr(0xed)
WRAP_COLUMNS = 60

USAGE = """\
Usage:
    create
        Create a new master public/secret key pair.

    check <key>
        Check an existing key for validity.

    sign <sequence> <validator-public> <master-secret>
        Create a new signed manifest with the given sequence
        number, validator public key, and master secret key.

    verify <sequence> <validator-public> <signature> <master-public>
        Verify hex-encoded manifest signature with master public key.
    """

def prepend_length_byte(b):
    assert len(b) <= 192, 'Too long'
    return chr(len(b)) + b

def to_int32(i):
    return struct.pack('>I', i)

#-----------------------------------------------------------

def make_seed(urandom=os.urandom):
    # This is not used.
    return urandom(16)

def make_ed25519_keypair(urandom=os.urandom):
    private_key = urandom(32)
    return private_key, ed25519.publickey(private_key)

def make_ecdsa_keypair():
    # This is not used.
    private_key = ecdsa.SigningKey.generate(curve=ecdsa.SECP256k1)
    # Can't be unit tested easily - need a mock for ecdsa.
    vk = private_key.get_verifying_key()
    sig = private_key.sign('message')
    assert vk.verify(sig, 'message')
    return private_key, vk

def make_seed_from_passphrase(passphrase):
    # For convenience, like say testing against rippled we can hash a passphrase
    # to get the seed. validation_create (Josh may have killed it by now) takes
    # an optional arg, which can be a base58 encoded seed, or a passphrase.
    return hashlib.sha512(passphrase).digest()[:16]

def make_manifest(public_key, validator_public_key, seq):
    return ''.join([
        SField.sfSequence,
        to_int32(seq),
        SField.sfPublicKey,      # Master public key.
        prepend_length_byte(public_key),
        SField.sfSigningPubKey,  # Ephemeral public key.
        prepend_length_byte(validator_public_key)])

def sign_manifest(manifest, private_key, public_key):
    sig = ed25519.signature('MAN\0' + manifest, private_key, public_key)
    return manifest + SField.sfSignature + prepend_length_byte(sig)

def wrap(s, cols=WRAP_COLUMNS):
    if s:
        size = max((len(s) + cols - 1) / cols, 1)
        w = len(s) / size
        s = '\n'.join(s[i:i + w] for i in range(0, len(s), w))
    return s

def create_ed_keys(urandom=os.urandom):
    private_key, public_key = make_ed25519_keypair(urandom)
    public_key_human = Base58.encode_version(
        Base58.VER_NODE_PUBLIC, ED25519_BYTE + public_key)
    private_key_human = Base58.encode_version(
        Base58.VER_NODE_PRIVATE, private_key)
    return public_key_human, private_key_human

def check_validator_public(v, validator_public_key):
    Base58.check_version(v, Base58.VER_NODE_PUBLIC)
    if len(validator_public_key) != 33:
        raise ValueError('Validator key should be length 33, is %s' %
                         len(validator_public_key))
    b = ord(validator_public_key[0])
    if b not in (2, 3):
        raise ValueError('First validator key byte must be 2 or 3, is %d' % b)

def check_master_secret(v, private_key):
    Base58.check_version(v, Base58.VER_NODE_PRIVATE)
    if len(private_key) != 32:
        raise ValueError('Length of master secret should be 32, is %s' %
                         len(private_key))


def get_signature(seq, validator_public_key_human, private_key_human):
    v, validator_public_key = Base58.decode_version(validator_public_key_human)
    check_validator_public(v, validator_public_key)

    v, private_key = Base58.decode_version(private_key_human)
    check_master_secret(v, private_key)

    pk = ed25519.publickey(private_key)
    apk = ED25519_BYTE + pk
    m = make_manifest(apk, validator_public_key, seq)
    m1 = sign_manifest(m, private_key, pk)
    return base64.b64encode(m1)

def verify_signature(seq, validator_public_key_human, public_key_human, signature):
    v, validator_public_key = Base58.decode_version(validator_public_key_human)
    check_validator_public(v, validator_public_key)

    v, public_key = Base58.decode_version(public_key_human)

    m = make_manifest(public_key, validator_public_key, seq)
    public_key = public_key[1:]     # Remove ED25519_BYTE
    sig = signature.decode('hex')
    ed25519.checkvalid(sig, 'MAN\0' + m, public_key)

# Testable versions of functions.
def perform_create(urandom=os.urandom, print=print):
    public, private = create_ed_keys(urandom)
    print('[validator_keys]', public, '', '[master_secret]', private, sep='\n')

def perform_check(s, print=print):
    version, b = Base58.decode_version(s)
    print('version        = ' + Base58.version_name(version))
    print('decoded length = ' + str(len(b)))
    assert Base58.encode_version(version, b) == s

def perform_sign(
        seq, validator_public_key_human, private_key_human, print=print):
    print('[validation_manifest]')
    print(wrap(get_signature(
        int(seq), validator_public_key_human, private_key_human)))

def perform_verify(
        seq, validator_public_key_human, public_key_human, signature, print=print):
    verify_signature(
        int(seq), validator_public_key_human, public_key_human, signature)
    print('Signature valid for', public_key_human)

# Externally visible versions of functions.
def create():
    perform_create()

def check(s):
    perform_check(s)

def sign(seq, validator_public_key_human, private_key_human):
    perform_sign(seq, validator_public_key_human, private_key_human)

def verify(seq, validator_public_key_human, public_key_human, signature):
    perform_verify(seq, validator_public_key_human, public_key_human, signature)

def usage(*errors):
    if errors:
        print(*errors)
    print(USAGE)
    return not errors

_COMMANDS = dict((f.__name__, f) for f in (create, check, sign, verify))

def run_command(args):
    if not args:
        return usage()
    name = args[0]
    command = _COMMANDS.get(name)
    if not command:
        return usage('No such command:', command)
    try:
        command(*args[1:])
    except TypeError:
        return usage('Wrong number of arguments for:', command)
    return True
