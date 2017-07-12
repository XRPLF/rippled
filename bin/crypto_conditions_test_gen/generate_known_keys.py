#!/usr/bin/env python

import base64
import binascii
import codecs

import collections
from collections import defaultdict

import cryptoconditions_asn1

import cryptography
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization

import nacl
import nacl.encoding
import nacl.hash
import nacl.signing


def generate_rsa_keys(n):
    '''
    return a list of serializations of n rsa private keys
    This list will be used for known keys
    '''
    result = []
    for i in range(n):
        signing_key = rsa.generate_private_key(
            public_exponent=65537, key_size=2048, backend=default_backend())
        pem = signing_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption())
        result.append(pem)
    return result


def generate_ed25519_keys(n):
    '''
    return a list of serializations of n ed25519 private keys
    This list will be used for known keys
    '''
    result = []
    for i in range(n):
        signing_key = nacl.signing.SigningKey.generate()
        result.append(signing_key.encode(encoder=nacl.encoding.HexEncoder))
    return result


def write_known_keys(file_name, n=64):
    with open(file_name, 'w') as f:
        f.write('rsa_known_keys_serialized = {}\n\n'.format(
            generate_rsa_keys(n)))

        f.write('ed25519_known_keys_serialized = {}'.format(
            generate_ed25519_keys(n)))


def load_rsa_key(signing_key):
    return serialization.load_pem_private_key(
        signing_key, password=None, backend=default_backend())


def load_ed25519_key(signing_key):
    return nacl.signing.SigningKey(signing_key, nacl.encoding.HexEncoder)
