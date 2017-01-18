#!/usr/bin/env python

from __future__ import print_function

import base64, binascii, json, os, random, struct, sys
import ed25519
import ecdsa
import hashlib
from ripple.util import Base58
from ripple.ledger import SField

ED25519_BYTE = chr(0xed)
WRAP_COLUMNS = 72

USAGE = """\
Usage:
    create
        Create a new master public/secret key pair.

    create <master-secret>
        Generate master key pair using provided secret.

    check <key>
        Check an existing key for validity.

    sign <sequence> <validation-public-key> <validation-private-key> <master-secret>
        Create a new signed manifest with the given sequence
        number and keys.
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
    sk = urandom(32)
    return sk, ed25519.publickey(sk)

def make_ecdsa_keypair(urandom=None):
    # This is not used.
    sk = ecdsa.SigningKey.generate(curve=ecdsa.SECP256k1, entropy=urandom)
    # Can't be unit tested easily - need a mock for ecdsa.
    vk = sk.get_verifying_key()
    sig = sk.sign('message')
    assert vk.verify(sig, 'message')
    return sk, vk

def make_seed_from_passphrase(passphrase):
    # For convenience, like say testing against rippled we can hash a passphrase
    # to get the seed. validation_create (Josh may have killed it by now) takes
    # an optional arg, which can be a base58 encoded seed, or a passphrase.
    return hashlib.sha512(passphrase).digest()[:16]

def make_manifest(master_pk, validation_pk, seq):
    """create a manifest

    Parameters
    ----------
    master_pk : string
        validator's master public key (binary, _not_ BASE58 encoded)
    validation_pk : string
        validator's validation public key (binary, _not_ BASE58 encoded)
    seq : int
        manifest sequence number

    Returns
    ----------
    string
        String with fields for seq, master_pk, validation_pk
    """
    return ''.join([
        SField.sfSequence,
        to_int32(seq),
        SField.sfPublicKey,
        prepend_length_byte(master_pk),
        SField.sfSigningPubKey,
        prepend_length_byte(validation_pk)])

def sign_manifest(manifest, validation_sk, master_sk, master_pk):
    """sign a validator manifest

    Parameters
    ----------
    manifest : string
        manifest to sign
    validation_sk : string
        validator's validation secret key (binary, _not_ BASE58 encoded)
        This is one of the keys that will sign the manifest.
    master_sk : string
        validator's master secret key (binary, _not_ BASE58 encoded)
        This is one of the keys that will sign the manifest.
    master_pk : string
        validator's master public key (binary, _not_ BASE58 encoded)

    Returns
    ----------
    string
        manifest signed by both the validation and master keys
    """
    man_hash = hashlib.sha512('MAN\0' + manifest).digest()[:32]
    validation_sig = validation_sk.sign_digest_deterministic(
        man_hash, hashfunc=hashlib.sha256, sigencode=ecdsa.util.sigencode_der_canonize)
    master_sig = ed25519.signature('MAN\0' + manifest, master_sk, master_pk)
    return manifest + SField.sfSignature + prepend_length_byte(validation_sig) + \
        SField.sfMasterSignature + prepend_length_byte(master_sig)

def wrap(s, cols=WRAP_COLUMNS):
    if s:
        size = max((len(s) + cols - 1) / cols, 1)
        w = len(s) / size
        s = '\n'.join(s[i:i + w] for i in range(0, len(s), w))
    return s

def create_ed_keys(urandom=os.urandom):
    sk, pk = make_ed25519_keypair(urandom)
    pk_human = Base58.encode_version(
        Base58.VER_NODE_PUBLIC, ED25519_BYTE + pk)
    sk_human = Base58.encode_version(
        Base58.VER_NODE_PRIVATE, sk)
    return pk_human, sk_human

def create_ed_public_key(sk_human):
    v, sk = Base58.decode_version(sk_human)
    check_secret_key(v, sk)

    pk = ed25519.publickey(sk)
    pk_human = Base58.encode_version(
        Base58.VER_NODE_PUBLIC, ED25519_BYTE + pk)
    return pk_human

def check_validation_public_key(v, pk):
    Base58.check_version(v, Base58.VER_NODE_PUBLIC)
    if len(pk) != 33:
        raise ValueError('Validation public key should be length 33, is %s' %
                         len(pk))
    b = ord(pk[0])
    if b not in (2, 3):
        raise ValueError('First validation public key byte must be 2 or 3, is %d' % b)

def check_secret_key(v, sk):
    Base58.check_version(v, Base58.VER_NODE_PRIVATE)
    if len(sk) != 32:
        raise ValueError('Length of master secret should be 32, is %s' %
                         len(sk))

def get_signature(seq, validation_pk_human, validation_sk_human, master_sk_human):
    v, validation_pk = Base58.decode_version(validation_pk_human)
    check_validation_public_key(v, validation_pk)

    v, validation_sk_str = Base58.decode_version(validation_sk_human)
    check_secret_key(v, validation_sk_str)
    validation_sk = ecdsa.SigningKey.from_string(validation_sk_str, curve=ecdsa.SECP256k1)

    v, master_sk = Base58.decode_version(master_sk_human)
    check_secret_key(v, master_sk)

    pk = ed25519.publickey(master_sk)
    apk = ED25519_BYTE + pk
    m = make_manifest(apk, validation_pk, seq)
    m1 = sign_manifest(m, validation_sk, master_sk, pk)
    return base64.b64encode(m1)

# Testable versions of functions.
def perform_create(urandom=os.urandom, print=print):
    pk, sk = create_ed_keys(urandom)
    print('[validator_keys]', pk, '', '[master_secret]', sk, sep='\n')

def perform_create_public(sk_human, print=print):
    pk_human = create_ed_public_key(sk_human)
    print(
        '[validator_keys]',pk_human, '',
        '[master_secret]', sk_human, sep='\n')

def perform_check(s, print=print):
    version, b = Base58.decode_version(s)
    print('version        = ' + Base58.version_name(version))
    print('decoded length = ' + str(len(b)))
    assert Base58.encode_version(version, b) == s

def perform_sign(
        seq, validation_pk_human, validation_sk_human, master_sk_human, print=print):
    manifest = get_signature(
            int(seq), validation_pk_human, validation_sk_human, master_sk_human)

    print('[validator_token]')
    print(wrap(base64.b64encode(json.dumps({
        "validation_secret_key": binascii.b2a_hex(Base58.decode_version(validation_sk_human)[1]),
        "manifest": manifest},
        separators=(',', ':')))))

def perform_verify(
        seq, validation_pk_human, master_pk_human, signature, print=print):
    verify_signature(
        int(seq), validation_pk_human, master_pk_human, signature)
    print('Signature valid for', master_pk_human)

# Externally visible versions of functions.
def create(sk_human=None):
    if sk_human:
        perform_create_public(sk_human)
    else:
        perform_create()

def check(s):
    perform_check(s)

def sign(seq, validation_pk_human, validation_sk_human, master_sk_human):
    perform_sign(seq, validation_pk_human, validation_sk_human, master_sk_human)

def usage(*errors):
    if errors:
        print(*errors)
    print(USAGE)
    return not errors

_COMMANDS = dict((f.__name__, f) for f in (create, check, sign))

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
