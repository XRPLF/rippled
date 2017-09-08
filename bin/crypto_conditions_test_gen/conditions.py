#!/usr/bin/env python

# This script generates two related sets of output:
#
# 1) c++ code used to test cryptocondtions. To generate this code, run with the
# `--prefix <FILE_NAME_PREFIX>` switch. The results of the script will be put
# in a set of files with names for the individual cryptoconditions. For
# example: `--prefix Conditions_generated_test_` will result in cpp files for
# Conditions_generated_test_ed.cpp, Conditions_generated_test_thresh, ect. Note
# these generated files are not well formatted. It is useful to run them
# through a formatter (such as clang-format). This is done outside of this
# script.
#
# 2) A corpus used for fuzz testing. To generate the corpus, run with the
# `--fuzz <OUTPUT_DIR>` switch. The results of the script will be put in the
# <OUTPUT_DIR>. It will create two subdirectories under <OUTPUT_DIR>, one for
# conditions and one for fulfillments. This is used as a corpus for fuzz
# testing.
#
# This script was run using python 3.6
# When using anaconda python, the following additional packages were installed (using conda install XXX):
# pyasn1 (conda install pyasn1)
# cryptography (conda install cryptography)
# nacl (conda install -c conda-forge pynacl)

import argparse
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

import itertools

import known_signing_keys

import nacl
import nacl.encoding
import nacl.hash
import nacl.signing


import json_test_cases
import logging
from pathlib import Path

import pyasn1
from pyasn1.type import univ
from pyasn1.codec.der.decoder import decode
from pyasn1.codec.der.encoder import encode

import os
import string

from IPython.core.debugger import Tracer

logging.basicConfig(filename='conditions.log', level=logging.DEBUG)
condition_logger = logging.getLogger('condition')

condition_test_template_prefix = \
'''
//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

// THIS FILE WAS AUTOMATICALLY GENERATED -- DO NOT EDIT

//==============================================================================

#include <test/conditions/ConditionsTestBase.h>

namespace ripple {{
namespace cryptoconditions {{

class Conditions_{RootTestName}_test : public ConditionsTestBase
{{
'''


condition_test_template_suffix = \
'''
}};

BEAST_DEFINE_TESTSUITE(Conditions_{RootTestName}, conditions, ripple);
}} // cryptoconditions
}} // ripple
'''


def to_bytes(v):
    if type(v) == bytes:
        return v
    return v.encode()


def to_string(v):
    if type(v) == str:
        return v
    return v.decode('utf8')


def case_and_type_insensitive_cmp(s1, s2):
    return to_bytes(s1).lower() == to_bytes(s2).lower()


def urlsafe_base64_to_hex(data):
    '''The sample cryptocondition json base 64 encoding is not correctly padded.
       This function adds the correct padding before decoding
    '''
    padding = len(data) % 4
    if padding != 0:
        data += b'=' * (4 - padding)
    return binascii.hexlify(base64.urlsafe_b64decode(data))


def set_with_tags(parent, name, value):
    c = parent.componentType
    idx = c.getPositionByName(name)
    proto = c[idx][1]
    t = type(value)
    if t in [
            pyasn1.type.univ.OctetString, pyasn1.type.univ.Integer,
            pyasn1.type.univ.BitString
    ]:
        value_with_tags = value.clone(
            tagSet=proto.getEffectiveTagSet(),
            subtypeSpec=proto.getSubtypeSpec())
    else:
        value_with_tags = value.clone(
            tagSet=proto.getEffectiveTagSet(),
            subtypeSpec=proto.getSubtypeSpec(),
            cloneValueFlag=True)
    parent[name] = value_with_tags


def encode_it(a):
    return to_string(binascii.hexlify(encode(a)))


def escape_hex(v):
    v = to_string(v)
    if len(v) % 2:
        raise ValueError('String must have an even number of characters')
    r = ''
    for i in range(0, len(v), 2):
        r += '\\x' + v[i] + v[i + 1]
    return r


def array_data_hex(v):
    v = to_string(v)
    if len(v) % 2:
        raise ValueError('String must have an even number of characters')
    r = []
    for i in range(0, len(v), 2):
        r.append('0x' + v[i] + v[i + 1])
    return ', '.join(r)


def write_array(f, var_name, str_data):
    if len(str_data) % 2:
        raise ValueError('String must have an even number of characters')
    f.write('std::array<std::uint8_t, {len}> const {v_name}{{{{{data}}}}};\n'.
            format(
                v_name=var_name,
                len=len(str_data) // 2,
                data=array_data_hex(str_data)))


def write_cpp_string_literal(f, s, col_width=60):
    if col_width % 3:
        raise ValueError(
            'Col width must be a multiple of 3 to handle hex data')
    cur_col = 0
    if not len(s):
        f.write('""')
    while cur_col < len(s):
        f.write('"{}"'.format(s[cur_col:cur_col + col_width]))
        cur_col += col_width
    f.write('s;\n')


def bitset_to_tuple(seq):
    if seq:
        result = [0] * (1 + max(seq))
    else:
        result = [0]
    for b in seq:
        if b >= 32 or b < 0:
            raise ValueError('Can only set bits between 0 and 31')
        result[b] = 1
    return tuple(result)


def bitset_to_int(seq):
    result = 0
    for b in seq:
        if b >= 32 or b < 0:
            raise ValueError('Can only set bits between 0 and 31')
        result |= (1 << b)
    return result


preimageSha256TypeId = 0
prefixSha256TypeId = 1
thresholdSha256TypeId = 2
rsaSha256TypeId = 3
ed25519Sha256TypeId = 4


def asn1_type(id):
    d = {
        preimageSha256TypeId: 'preimageSha256',
        prefixSha256TypeId: 'prefixSha256',
        thresholdSha256TypeId: 'thresholdSha256',
        rsaSha256TypeId: 'rsaSha256',
        ed25519Sha256TypeId: 'ed25519Sha256'
    }
    return d[id]


def cpp_type(id):
    return 'Type::' + asn1_type(id)


def short_type(id):
    d = {
        preimageSha256TypeId: 'preim',
        prefixSha256TypeId: 'prefix',
        thresholdSha256TypeId: 'thresh',
        rsaSha256TypeId: 'rsa',
        ed25519Sha256TypeId: 'ed'
    }
    return d[id]


def json_type_string_to_type_id(id):
    d = {
        'preimage-sha-256': preimageSha256TypeId,
        'prefix-sha-256': prefixSha256TypeId,
        'threshold-sha-256': thresholdSha256TypeId,
        'rsa-sha-256': rsaSha256TypeId,
        'ed25519-sha-256': ed25519Sha256TypeId
    }
    return d[id]


class Condition:
    def __init__(self, fulfillment):
        self.fulfillment = fulfillment

    def type_id(self):
        return self.fulfillment.type_id()

    def type_name(self):
        return asn1_type(self.type_id())

    def cost(self):
        return self.fulfillment.cost()

    def fingerprint(self):
        if self.type_id() == preimageSha256TypeId:
            encoding = self.fulfillment.preimage
        else:
            encoding = self.fulfillment.encoded_fingerprint()
        digest = nacl.hash.sha256(encoding, encoder=nacl.encoding.HexEncoder)
        return digest

    def to_asn1(self):
        digest = self.fingerprint()

        c = cryptoconditions_asn1.Condition().componentType
        idx = c.getPositionByName(self.type_name())
        asn1_condition = c[idx][1].clone()
        set_with_tags(
            asn1_condition,
            'fingerprint',
            univ.OctetString(hexValue=to_string(digest)))
        set_with_tags(asn1_condition, 'cost', univ.Integer(self.cost()))

        subtypes_set = self.fulfillment.subtype_ids()
        if subtypes_set:
            subtypes = bitset_to_tuple(subtypes_set)
            set_with_tags(asn1_condition, 'subtypes', univ.BitString(subtypes))

        result = cryptoconditions_asn1.Condition()
        result[self.type_name()] = asn1_condition
        return result

    def decl_var_name(self):
        return '{}{}Cond'.format(
            short_type(self.type_id()).capitalize(), self.fulfillment.id)

    def write_test_structure(self, f, level=0):
        f.write(' // {} {}\n'.format('*' * (level + 1), self.decl_var_name()))

    def write_decl(self, f):
        '''
        Condition(Type t, std::uint32_t cost, std::array<std::uint8_t, 32> const& fingerprint, std::bitset<5> const& subtypes)
        '''
        var_name = self.decl_var_name()
        t = cpp_type(self.type_id())
        digest = self.fingerprint()
        fingerprint_var = '{}ConditionFingerprint'.format(var_name)
        f.write('std::array<std::uint8_t, 32> const {var_name}={{{{ {array_data} }}}};'.
                format(var_name=fingerprint_var, array_data=array_data_hex(digest)))
        cost = str(self.cost())
        subtypes_as_int = 0
        for i in self.fulfillment.subtype_ids():
            subtypes_as_int += 1 << i
        params = ', '.join(
            [t, cost, fingerprint_var, 'std::bitset<5>{{{}}}'.format(subtypes_as_int)])

        f.write('Condition const {var_name}{{{params}}};\n'.format(
            var_name=var_name, params=params))

    def write_test(self, f):
        var_name = self.decl_var_name()
        f.write('{\n')
        f.write('auto const {var_name}EncodedCondition="{encoded}"s;\n'.format(
            var_name=var_name, encoded=escape_hex(encode_it(self.to_asn1()))))

        f.write(
            'auto const {var_name}EncodedFingerprint="{encoded}";\n'.format(
                var_name=var_name,
                encoded=escape_hex(
                    binascii.hexlify(self.fulfillment.encoded_fingerprint()))))
        self.write_decl(f, var_name)
        f.write('}\n')


class Fulfillment:
    next_id = 0

    def __init__(self):
        self.msg = b''
        self.id = Fulfillment.next_id
        Fulfillment.next_id += 1

    @classmethod
    def reset_id(cls):
        cls.next_id = 0

    @classmethod
    def create_from_json(cls, json_init, json_check=None):
        prototypes = {
            'preimage-sha-256': Preimage,
            'prefix-sha-256': Prefix,
            'threshold-sha-256': Threshold,
            'rsa-sha-256': RsaSha256,
            'ed25519-sha-256': Ed25519
        }
        return prototypes[json_init['type']](json_init=json_init,
                                             json_check=json_check)

    def check_json(self, json):
        if not case_and_type_insensitive_cmp(
                encode_it(self.to_asn1()), json['fulfillment']):
            raise ValueError('Expected fulfillment mismatch')
        if not case_and_type_insensitive_cmp(
                encode_it(self.condition().to_asn1()),
                json['conditionBinary']):
            raise ValueError('Expected condition mismatch')
        if not case_and_type_insensitive_cmp(
                binascii.hexlify(self.encoded_fingerprint()),
                to_bytes(json['fingerprintContents'])):
            raise ValueError('Fingerprint contents mismatch')
        if self.cost() != json['cost']:
            raise ValueError('Cost mismatch')
        st_from_json = set(
            [json_type_string_to_type_id(tid) for tid in json['subtypes']])
        if self.subtype_ids() != st_from_json:
            raise ValueError('Subtypes mismatch')
        self.msg = escape_hex(to_bytes(json['message']))

    @classmethod
    def create_from_pylist(cls, pylist):
        prototypes = {
            'preim': Preimage,
            'prefix': Prefix,
            'thresh': Threshold,
            'rsa': RsaSha256,
            'ed': Ed25519
        }
        t = type(pylist)
        if t == str:
            return prototypes[pylist](pylist=pylist)
        if t != list:
            raise ValueError('Bad pylist')
        if len(pylist) == 0:
            return []
        if type(pylist[0]) == str:
            if pylist[0] == 'thresh':
                return Threshold(pylist=pylist)
            elif pylist[0] == 'prefix':
                return Prefix(pylist=pylist)
        return [Fulfillment.create_from_pylist(f) for f in pylist]

    def set_msg_self_and_children(self, msg):
        '''
        Set the message of a fulfillment so it succeeds.
        This will set the message of child fulfillments so they
        will succeeds as well, in particular, this means prefix subcondition
        will change their prefixes and set its message so
        prefix+new_message==set_msg
        '''
        self.set_msg(msg)

    def self_and_subtype_ids(self):
        return set([self.type_id()])

    def subtype_ids(self):
        return set()

    def condition(self):
        return Condition(self)

    def decl_var_name(self):
        return '{}{}'.format(short_type(self.type_id()), self.id)

    def depth_first_fulfillments(self, result):
        result.append(self)

    def write_test_structure(self, f, level=0):
        if level == 0:
            f.write(' // Fulfillment structure\n')
        f.write(' // {} {}\n'.format('*' * (level + 1), self.decl_var_name()))

    def write_test(self, f):
        self.write_test_structure(f)
        f.write('\n\n')
        allfulfillments = []
        self.depth_first_fulfillments(allfulfillments)
        for s in allfulfillments:
            s.write_init_data(f)
        f.write('\n\n')
        for s in allfulfillments[:-1]:
            s.write_decl(f, is_unique_ptr=True)
        allfulfillments[-1].write_decl(f, is_unique_ptr=True)

        var_name = self.decl_var_name()
        f.write('{\n')
        f.write('auto {var_name}EncodedFulfillment='.format(
            var_name=var_name))
        write_cpp_string_literal(f, escape_hex(encode_it(self.to_asn1())))
        f.write('auto const {var_name}EncodedCondition='.format(
            var_name=var_name))
        write_cpp_string_literal(
            f, escape_hex(encode_it(self.condition().to_asn1())))
        f.write('auto const {var_name}EncodedFingerprint='.format(
            var_name=var_name))
        write_cpp_string_literal(
            f, escape_hex(binascii.hexlify(self.encoded_fingerprint())))
        f.write('check(')
        f.write('''
        std::move({var_name}),
        {var_name}Msg,
        std::move({var_name}EncodedFulfillment),
        {var_name}EncodedCondition,
        {var_name}EncodedFingerprint);
        '''.format(var_name=var_name))
        f.write('}\n')


def load_ed25519_key(signing_key):
    return nacl.signing.SigningKey(signing_key, nacl.encoding.HexEncoder)


class Ed25519(Fulfillment):

    known_keys = [load_ed25519_key(k) for k in known_signing_keys.ed25519_known_keys_serialized]

    def __init__(self,
                 msg=b'Attack at Dawn',
                 signing_key=None,
                 json_init=None,
                 json_check=None,
                 pylist=None):
        super().__init__()
        self.cached_verify_key_hex = None
        self.cached_signature_hex = None
        self.signing_key = None
        if json_init:
            self.from_json(json_init)
            if json_check:
                self.check_json(json_check)
            return
        if pylist is not None and pylist != short_type(self.type_id()):
            raise ValueError('Ill formed pylist spec')
        if signing_key is None:
            signing_key = Ed25519.known_keys[self.id % len(Ed25519.known_keys)]

        self.signing_key = signing_key
        self.set_msg(msg)

    def from_json(self, json):
        self.cached_verify_key_hex = to_bytes(
            urlsafe_base64_to_hex(to_bytes(json['publicKey'])))
        self.cached_signature_hex = to_bytes(
            urlsafe_base64_to_hex(to_bytes(json['signature'])))

    def set_msg(self, msg):
        msg = to_bytes(msg)
        self.msg = msg
        self.signature = self.signing_key.sign(msg)[0:-len(msg)]

    def signature_hex(self):
        if self.cached_signature_hex:
            return self.cached_signature_hex
        return binascii.hexlify(self.signature)

    def signing_key_hex(self):
        return self.signing_key.encode(encoder=nacl.encoding.HexEncoder)

    def verify_key_hex(self):
        if self.cached_verify_key_hex:
            return self.cached_verify_key_hex
        return self.signing_key.verify_key.encode(
            encoder=nacl.encoding.HexEncoder)

    def cost(self):
        # see crypto-conditions spec:
        # https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#page-27
        return 131072

    def type_id(self):
        return ed25519Sha256TypeId

    def to_asn1(self):
        result = cryptoconditions_asn1.Fulfillment()
        idx = result.componentType.getPositionByName('ed25519Sha256')
        # tag types must match - creating a choice independent of the tags (i.e. not cloning here)
        # and then setting f['ed25519Sha256'] = my_fulfillment_without_correct_tags will not work
        choice_ed_f = result.componentType[idx][1].clone()

        vk_hex = to_string(self.verify_key_hex())
        sig_hex = to_string(self.signature_hex())
        set_with_tags(
            choice_ed_f, 'publicKey', univ.OctetString(hexValue=vk_hex))
        set_with_tags(
            choice_ed_f, 'signature', univ.OctetString(hexValue=sig_hex))
        result['ed25519Sha256'] = choice_ed_f
        return result

    def encoded_fingerprint(self):
        fingerprint = cryptoconditions_asn1.Ed25519FingerprintContents()
        set_with_tags(
            fingerprint,
            'publicKey',
            univ.OctetString(hexValue=to_string(self.verify_key_hex())))
        return encode(fingerprint)

    def write_init_data(self, f):
        var_name = self.decl_var_name()
        f.write('auto const {}Msg="{}"s;\n'.format(var_name,
                                                   to_string(self.msg)))
        write_array(f, '{}PublicKey'.format(var_name), self.verify_key_hex())
        write_array(f, '{}Sig'.format(var_name), self.signature_hex())
        if self.signing_key:
            write_array(f, '{}SigningKey'.format(var_name),
                        self.signing_key_hex())
            f.write('(void){}SigningKey;\n'.format(var_name))

    def write_decl(self, f, is_unique_ptr=True, inc_sub_test=False):
        var_name = self.decl_var_name()
        if not is_unique_ptr:
            f.write(
                'Ed25519 const {var_name}({var_name}PublicKey, {var_name}Sig);\n'.
                format(var_name=var_name))
        else:
            f.write(
                'auto {var_name}=std::make_unique<Ed25519>({var_name}PublicKey, {var_name}Sig);\n'.
                format(var_name=var_name))


def load_rsa_key(signing_key):
    return serialization.load_pem_private_key(
        signing_key, password=None, backend=default_backend())


class RsaSha256(Fulfillment):
    known_keys = [load_rsa_key(k) for k in known_signing_keys.rsa_known_keys_serialized]

    def __init__(self,
                 msg=b'Attack at Dawn',
                 signing_key=None,
                 json_init=None,
                 json_check=None,
                 pylist=None):
        super().__init__()
        self.cached_verify_key_hex = None
        self.cached_signature_hex = None
        self.signing_key = None
        if json_init:
            self.from_json(json_init)
            if json_check:
                self.check_json(json_check)
            return
        if pylist is not None and pylist != short_type(self.type_id()):
            raise ValueError('Ill formed pylist spec')
        if signing_key is None:
            signing_key = RsaSha256.known_keys[self.id % len(RsaSha256.known_keys)]

        self.signing_key = signing_key
        self.set_msg(msg)

    def from_json(self, json):
        self.cached_verify_key_hex = to_bytes(
            urlsafe_base64_to_hex(to_bytes(json['modulus'])))
        self.cached_signature_hex = to_bytes(
            urlsafe_base64_to_hex(to_bytes(json['signature'])))

    def set_msg(self, msg):
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import padding
        msg = to_bytes(msg)
        self.msg = msg

        self.signature = self.signing_key.sign(
            self.msg,
            padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                        salt_length=256 // 8),
            hashes.SHA256())
        # dummy check. Will raise an exception if it does not verify
        self.signing_key.public_key().verify(
            self.signature,
            self.msg,
            padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                        salt_length=256 // 8),
            hashes.SHA256())

    def signature_hex(self):
        if self.cached_signature_hex:
            return self.cached_signature_hex
        return binascii.hexlify(self.signature)

    def verify_key_hex(self):
        if self.cached_verify_key_hex:
            return self.cached_verify_key_hex
        pk = self.signing_key.public_key()
        return '{:x}'.format(pk.public_numbers().n)

    def cost(self):
        if self.cached_signature_hex:
            key_bytes = len(self.cached_verify_key_hex) // 2
        else:
            key_bytes = self.signing_key.key_size // 8
        return key_bytes * key_bytes

    def type_id(self):
        return rsaSha256TypeId

    def to_asn1(self):
        result = cryptoconditions_asn1.Fulfillment()
        idx = result.componentType.getPositionByName('rsaSha256')
        # tag types must match - creating a choice independent of the tags (i.e. not cloning here)
        # and then setting f['rsaSha256'] = my_fulfillment_without_correct_tags will not work
        choice_rsa_f = result.componentType[idx][1].clone()

        vk_hex = to_string(self.verify_key_hex())
        sig_hex = to_string(self.signature_hex())
        set_with_tags(
            choice_rsa_f, 'modulus', univ.OctetString(hexValue=vk_hex))
        set_with_tags(
            choice_rsa_f, 'signature', univ.OctetString(hexValue=sig_hex))
        result['rsaSha256'] = choice_rsa_f
        return result

    def encoded_fingerprint(self):
        fingerprint = cryptoconditions_asn1.RsaFingerprintContents()
        set_with_tags(
            fingerprint,
            'modulus',
            univ.OctetString(hexValue=to_string(self.verify_key_hex())))
        return encode(fingerprint)

    def write_init_data(self, f):
        var_name = self.decl_var_name()
        f.write('auto const {}Msg="{}"s;\n'.format(var_name,
                                                   to_string(self.msg)))
        write_array(f, '{}PublicKey'.format(var_name), self.verify_key_hex())
        write_array(f, '{}Sig'.format(var_name), self.signature_hex())

    def write_decl(self, f, is_unique_ptr=True, inc_sub_test=False):
        var_name = self.decl_var_name()
        if not is_unique_ptr:
            f.write('''RsaSha256 const {var_name}(
                          makeSlice({var_name}PublicKey),
                          makeSlice({var_name}Sig));\n'''
                    .format(var_name=var_name))
        else:
            f.write('''auto {var_name}=std::make_unique<RsaSha256>(
                      makeSlice({var_name}PublicKey),
                      makeSlice({var_name}Sig));\n'''
                    .format(var_name=var_name))


class Prefix(Fulfillment):
    def __init__(self,
                 subfulfillment=None,
                 prefix=b'Attack ',
                 msg=b'at Dawn',
                 max_msg_length=None,
                 json_init=None,
                 json_check=None,
                 pylist=None):
        super().__init__()
        if json_init:
            self.from_json(json_init)
            if json_check:
                self.check_json(json_check)
            return
        if max_msg_length is None:
            max_msg_length = len(msg)
        if pylist != None:
            if len(pylist) < 2 or pylist[0] != short_type(self.type_id()):
                raise ValueError('Ill formed pylist spec')
            sub_init = pylist[1] if len(pylist) == 2 else pylist[1:]
            subfulfillment = Fulfillment.create_from_pylist(sub_init)
        if subfulfillment is None:
            raise ValueError('Must specify either json or subfulfillment')
        self.subfulfillment = subfulfillment
        self.prefix = to_bytes(prefix)
        self.msg = to_bytes(msg)
        self.max_msg_length = max_msg_length
        self.set_msg(msg)

    def from_json(self, json):
        self.max_msg_length = json['maxMessageLength']
        self.prefix = binascii.unhexlify(
            to_bytes(urlsafe_base64_to_hex(to_bytes(json['prefix']))))
        self.subfulfillment = Fulfillment.create_from_json(json[
            'subfulfillment'])

    def set_msg_self_and_children(self, msg):
        self.msg = to_bytes(msg)
        self.max_msg_length = len(msg)
        self.prefix = to_bytes('P{}'.format(self.id))
        self.subfulfillment.set_msg_self_and_children(self.prefix + self.msg)

    def set_msg(self, msg):
        self.msg = to_bytes(msg)
        self.max_msg_length = len(msg)
        self.subfulfillment.set_msg(self.prefix + self.msg)

    def cost(self):
        return len(self.prefix
                   ) + self.max_msg_length + self.subfulfillment.cost() + 1024

    def type_id(self):
        return prefixSha256TypeId

    def self_and_subtype_ids(self):
        r = self.subfulfillment.self_and_subtype_ids()
        r.add(self.type_id())
        return r

    def subtype_ids(self):
        r = self.subfulfillment.self_and_subtype_ids()
        r.discard(self.type_id())
        return r

    def depth_first_fulfillments(self, result):
        self.subfulfillment.depth_first_fulfillments(result)
        result.append(self)

    def write_test_structure(self, f, level=0):
        super().write_test_structure(f, level)
        self.subfulfillment.write_test_structure(f, level + 1)

    def to_asn1(self):
        result = cryptoconditions_asn1.Fulfillment()
        idx = result.componentType.getPositionByName('prefixSha256')
        # tag types must match - creating a choice independent of the tags (i.e. not cloning here)
        # and then setting f['ed25519Sha256'] = my_fulfillment_without_correct_tags will not work
        choice_f = result.componentType[idx][1].clone()

        set_with_tags(choice_f, 'prefix', univ.OctetString(self.prefix))
        set_with_tags(choice_f, 'maxMessageLength',
                      univ.Integer(self.max_msg_length))

        set_with_tags(choice_f, 'subfulfillment',
                      self.subfulfillment.to_asn1())

        result['prefixSha256'] = choice_f
        return result

    def encoded_fingerprint(self):
        fingerprint = cryptoconditions_asn1.PrefixFingerprintContents()
        set_with_tags(fingerprint, 'prefix', univ.OctetString(self.prefix))
        set_with_tags(fingerprint, 'maxMessageLength',
                      univ.Integer(self.max_msg_length))
        set_with_tags(fingerprint, 'subcondition',
                      self.subfulfillment.condition().to_asn1())
        return encode(fingerprint)

    def write_init_data(self, f):
        var_name = self.decl_var_name()
        f.write('auto const {}Prefix="{}"s;\n'.format(var_name,
                                                      to_string(self.prefix)))
        f.write('auto const {}Msg="{}"s;\n'.format(var_name,
                                                   to_string(self.msg)))
        f.write('auto const {}MaxMsgLength={};\n'.format(var_name,
                                                         self.max_msg_length))

    def write_decl(self, f, is_unique_ptr=True, inc_sub_test=False):
        var_name = self.decl_var_name()
        if not is_unique_ptr:
            decl = '''PrefixSha256 const {var_name}(makeSlice({var_name}Prefix),
                          {var_name}MaxMsgLength,
                          std::move({sub_var}));\n'''
        else:
            decl = '''auto {var_name} = std::make_unique<PrefixSha256>(makeSlice({var_name}Prefix),
                          {var_name}MaxMsgLength,
                          std::move({sub_var}));\n'''
        f.write(
            decl.format(
                var_name=var_name, sub_var=self.subfulfillment.decl_var_name(
                )))


class Preimage(Fulfillment):
    def __init__(self,
                 preimage=b'I am root',
                 msg=b'Attack at Dawn',
                 json_init=None,
                 json_check=None,
                 pylist=None):
        super().__init__()
        if json_init:
            self.from_json(json_init)
            if json_check:
                self.check_json(json_check)
            return
        if pylist is not None and pylist != short_type(self.type_id()):
            raise ValueError('Ill formed pylist spec')
        self.preimage = to_bytes(preimage)
        self.msg = to_bytes(msg)
        self.set_msg(msg)

    def from_json(self, json):
        self.preimage = binascii.unhexlify(
            to_bytes(urlsafe_base64_to_hex(to_bytes(json['preimage']))))

    def set_msg(self, msg):
        self.msg = to_bytes(msg)

    def cost(self):
        return len(self.preimage)

    def type_id(self):
        return preimageSha256TypeId

    def to_asn1(self):
        result = cryptoconditions_asn1.Fulfillment()
        idx = result.componentType.getPositionByName('preimageSha256')
        # tag types must match - creating a choice independent of the tags (i.e. not cloning here)
        # and then setting f['ed25519Sha256'] = my_fulfillment_without_correct_tags will not work
        choice_f = result.componentType[idx][1].clone()
        set_with_tags(choice_f, 'preimage', univ.OctetString(self.preimage))
        result['preimageSha256'] = choice_f
        return result

    def encoded_fingerprint(self):
        # Unlike other cc (that use der encoding), the fingerprint is a hash of the preimage
        return self.preimage

    def write_init_data(self, f):
        var_name = self.decl_var_name()
        f.write('auto const {}Preimage="{}"s;\n'.format(
            var_name, to_string(self.preimage)))
        f.write('auto const {}Msg="{}"s;\n'.format(var_name,
                                                   to_string(self.msg)))

    def write_decl(self, f, is_unique_ptr=True, inc_sub_test=False):
        var_name = self.decl_var_name()
        if not is_unique_ptr:
            decl = '''PreimageSha256 const {var_name}(makeSlice({var_name}Preimage));\n'''
        else:
            decl = '''auto {var_name} = std::make_unique<PreimageSha256>(makeSlice({var_name}Preimage));\n'''
        f.write(decl.format(var_name=var_name))


class Threshold(Fulfillment):
    def __init__(self,
                 subfulfillments=None,
                 subconditions=None,
                 msg=b'Attack at Dawn',
                 json_init=None,
                 json_check=None,
                 pylist=None):
        super().__init__()
        if json_init:
            self.from_json(json_init)
            if json_check:
                self.check_json(json_check)
            return
        self.subfulfillments = subfulfillments
        self.subconditions = subconditions
        if pylist != None:
            if len(pylist) != 3 or pylist[0] != short_type(self.type_id()):
                raise ValueError('Ill formed pylist spec')
            self.subfulfillments = Fulfillment.create_from_pylist(pylist[1])
            self.subconditions = [f.condition() for f in Fulfillment.create_from_pylist(pylist[2])]
        self.msg = to_bytes(msg)
        self.set_msg(msg)

    def from_json(self, json):
        t = json['threshold']
        if t != len(json['subfulfillments']):
            if 'conditionIndexes' not in json:
                raise ValueError(
                    'Must specify conditionIndexes to show which fulfillments are actually used.'
                )
            else:
                condIndexes = json['conditionIndexes']
        else:
            condIndexes = []
        self.subfulfillments = [
            Fulfillment.create_from_json(sf)
            for i, sf in enumerate(json['subfulfillments'])
            if i not in condIndexes
        ]
        self.subconditions = [
            Fulfillment.create_from_json(sf).condition()
            for i, sf in enumerate(json['subfulfillments']) if i in condIndexes
        ]

    def set_msg(self, msg):
        self.msg = to_bytes(msg)
        for f in self.subfulfillments:
            f.set_msg(msg)

    def set_msg_self_and_children(self, msg):
        self.msg = to_bytes(msg)
        for f in self.subfulfillments:
            f.set_msg_self_and_children(msg)

    def cost(self):
        costs = [c.cost() for c in self.subconditions
                 ] + [f.cost() for f in self.subfulfillments]
        costs.sort()
        n_highests = costs[-len(self.subfulfillments):]
        return 1024 * (len(self.subfulfillments) + len(self.subconditions)
                       ) + sum(n_highests)

    def type_id(self):
        return thresholdSha256TypeId

    def self_and_subtype_ids(self):
        r = set()
        for s in self.subfulfillments:
            r |= s.self_and_subtype_ids()
        for s in self.subconditions:
            r |= s.fulfillment.self_and_subtype_ids()
        r.add(self.type_id())
        return r

    def subtype_ids(self):
        r = self.self_and_subtype_ids()
        r.discard(self.type_id())
        return r

    def depth_first_fulfillments(self, result):
        for f in self.subfulfillments:
            f.depth_first_fulfillments(result)
        result.append(self)

    def write_test_structure(self, f, level=0):
        super().write_test_structure(f, level)
        for c in self.subconditions:
            c.write_test_structure(f, level + 1)
        for ful in self.subfulfillments:
            ful.write_test_structure(f, level + 1)

    def to_asn1(self):
        result = cryptoconditions_asn1.Fulfillment()
        idx = result.componentType.getPositionByName('thresholdSha256')
        # tag types must match - creating a choice independent of the tags (i.e. not cloning here)
        # and then setting f['ed25519Sha256'] = my_fulfillment_without_correct_tags will not work
        choice_f = result.componentType[idx][1].clone()

        a_f = univ.SetOf(componentType=cryptoconditions_asn1.Fulfillment())
        for s in self.subfulfillments:
            a_f.append(s.to_asn1())
        set_with_tags(choice_f, 'subfulfillments', a_f)
        a_c = univ.SetOf(componentType=cryptoconditions_asn1.Condition())
        for s in self.subconditions:
            a_c.append(s.to_asn1())
        set_with_tags(choice_f, 'subconditions', a_c)

        result['thresholdSha256'] = choice_f
        return result

    def encoded_fingerprint(self):
        fingerprint = cryptoconditions_asn1.ThresholdFingerprintContents()
        set_with_tags(fingerprint, 'threshold',
                      univ.Integer(len(self.subfulfillments)))
        a_c = univ.SetOf(componentType=cryptoconditions_asn1.Condition())
        for s in self.subfulfillments:
            a_c.append(s.condition().to_asn1())
        for s in self.subconditions:
            a_c.append(s.to_asn1())
        set_with_tags(fingerprint, 'subconditions', a_c)
        return encode(fingerprint)

    def subfulfill_var(self):
        return '{}Subfulfillments'.format(self.decl_var_name())

    def subcond_var(self):
        return '{}Subconditions'.format(self.decl_var_name())

    def write_init_data(self, f):
        var_name = self.decl_var_name()
        f.write('auto const {}Msg="{}"s;\n'.format(var_name,
                                                   to_string(self.msg)))
        for sc in self.subconditions:
            sc.write_decl(f)

    def write_decl(self, f, is_unique_ptr=True, inc_sub_test=False):
        var_name = self.decl_var_name()
        f.write('std::vector<std::unique_ptr<Fulfillment>> {var_name};'.format(
            var_name=self.subfulfill_var()))
        for sf in self.subfulfillments:
            f.write('{var_name}.emplace_back(std::move({sf_var}));'.format(
                var_name=self.subfulfill_var(), sf_var=sf.decl_var_name()))

        subconditions_init = ', '.join(
            ['{}'.format(sc.decl_var_name()) for sc in self.subconditions])
        if subconditions_init:
            subconditions_init = '{' + subconditions_init + '}'
        subcond_var = '{}Subconditions'.format(var_name)
        f.write('std::vector<Condition> {var_name}{{{subconditions_init}}};'.
                format(
                    var_name=self.subcond_var(),
                    subconditions_init=subconditions_init))
        if not is_unique_ptr:
            decl = '''ThresholdSha256 const {var_name}(std::move({subfulfil_var}),
                          std::move({subcond_var}));\n'''
        else:
            decl = '''auto {var_name} = std::make_unique<ThresholdSha256>(std::move({subfulfil_var}),
                          std::move({subcond_var}));\n'''
        f.write(
            decl.format(
                var_name=var_name,
                subfulfil_var=self.subfulfill_var(),
                subcond_var=self.subcond_var()))


class TestWriter:
    def __init__(self, result_file):
        self.result_file = result_file
        # function names to tests to run
        self.test_names = []
        self.test_ids = defaultdict(lambda: 0)

    def checkout_test_name(self, fulfillment):
        n = short_type(fulfillment.type_id())
        id = self.test_ids[n]
        self.test_ids[n] += 1
        return '{}{}'.format(n.capitalize(), id)

    def write_test(self, fulfillment):
        test_name = self.checkout_test_name(fulfillment)
        full_test_name = 'test' + test_name
        self.result_file.write('''
            void
            {full_test_name}(){{
            testcase("{test_name}");

            using namespace std::string_literals;
            using namespace ripple::cryptoconditions;

        '''.format(
            full_test_name=full_test_name, test_name=test_name))
        self.test_names.append(full_test_name)
        fulfillment.write_test(self.result_file)
        Fulfillment.reset_id()
        self.result_file.write('}\n')

    def write_test_case_ed(self):
        ed = Ed25519()
        self.write_test(ed)

    def write_test_case_rsa(self):
        rsa = RsaSha256()
        self.write_test(rsa)

    def write_test_case_preimage(self):
        p = Preimage()
        self.write_test(p)

    def write_test_case_prefix(self):
        ed = Ed25519()
        p = Prefix(ed)
        self.write_test(p)

    def write_test_case_threshold(self):
        prefix = b'Attack '
        msg = b'at Dawn'
        p = Prefix(Ed25519(msg=prefix + msg), msg=msg)
        ed = Ed25519(msg=msg)
        rsa = RsaSha256(msg=msg)
        subfulfillments = [p, ed, rsa]
        subconditions = []
        t = Threshold(subfulfillments, subconditions)
        self.write_test(t)

    def write_test_case(self, test_type):
        if test_type == ed25519Sha256TypeId:
            self.write_test_case_ed()
        elif test_type == prefixSha256TypeId:
            self.write_test_case_prefix()
        elif test_type == rsaSha256TypeId:
            self.write_test_case_rsa()
        elif test_type == thresholdSha256TypeId:
            self.write_test_case_threshold()
        elif test_type == preimageSha256TypeId:
            self.write_test_case_preimage()

    def save_pylist_test_case(self, pylist):
        f = Fulfillment.create_from_pylist(pylist)
        f.set_msg_self_and_children(string.ascii_lowercase)
        self.write_test(f)

    def save_json_test_case(self, tc):
        test_type = tc['json']['type']
        f = Fulfillment.create_from_json(json_init=tc['json'], json_check=tc)
        self.write_test(f)

    def write_run(self):
        self.result_file.write('''
            void
            run(){''')
        for t in self.test_names:
            self.result_file.write('{}();\n'.format(t))
        self.result_file.write('}\n')


def test_case_str(test_type=prefixSha256TypeId):
    import io
    f = io.StringIO()
    tw = TestWriter(f)
    tw.write_test_case(test_type)
    return f.getvalue()


def save_test_case(file_name, test_type=prefixSha256TypeId):
    with open(file_name, 'w') as f:
        tw = TestWriter(f)
        tw.write_test_case(test_type)


def save_json_test_cases(test_writer):
    for tc in json_test_cases.test_cases:
        test_writer.save_json_test_case(tc)
        test_type = tc['json']['type']


def partitioned_test_cases():
    '''return a dictionary of list of test cases. The key will be the top level
    cryptocondition type. The modivation for partitioning the tests is otherwise
    the single file is very large and hard for editors to deal with.
    '''
    result = defaultdict(list)
    keys = ['thresh', 'prefix', 'preim', 'rsa', 'ed']
    def add_to_result(l):
        k = l[0] if isinstance(l, list) else l
        if k not in keys:
            raise ValueError('Unknown test type: {}'.format(k))
        result[k].append(l)
    for tc in ['preim', 'rsa', 'ed']:
        add_to_result(tc)
        pre0 = ['prefix', tc]
        add_to_result(pre0)
        thresh0 = ['thresh', [tc], []]
        thresh1 = ['thresh', [tc], ['preim', 'rsa', 'ed']]
        thresh2 = ['thresh', [tc, thresh1], ['preim', 'rsa', 'ed']]
        thresh3 = ['thresh', [tc, thresh1], ['preim', 'rsa', 'ed', thresh1]]
        all_thresh = [thresh0, thresh1, thresh2, thresh3]
        for i in all_thresh:
            add_to_result(i)
        prepre0 = ['prefix', 'prefix', tc]
        prepre1 = ['prefix', 'prefix', pre0]
        prepre2 = ['prefix', 'prefix', thresh0]
        prepre3 = ['prefix', 'prefix', thresh1]
        prepre4 = ['prefix', 'prefix', thresh2]
        prepre5 = ['prefix', 'prefix', thresh3]
        all_prepre = [prepre0, prepre1, prepre2, prepre3, prepre4, prepre5]
        for i in all_prepre:
            add_to_result(i)
        for a,b,c in zip(all_prepre + all_thresh, itertools.cycle(all_prepre), itertools.cycle(all_thresh)):
            add_to_result(['thresh', [a, b, c], ['preim', 'rsa', 'ed']])
            add_to_result(['thresh', [a, 'preim', 'rsa', 'ed'], ['preim', 'rsa', 'ed', b, c]])
    return result


def save_all_test_cases(file_name_prefix, inc_json=True):
    test_cases = partitioned_test_cases()
    for root_condition_name, test_list in test_cases.items():
        file_name = file_name_prefix+root_condition_name+'.cpp'
        with open(file_name, 'w') as f:
            f.write(condition_test_template_prefix.format(RootTestName=root_condition_name))
            tw = TestWriter(f)
            for tc in test_list:
                tw.save_pylist_test_case(tc)
            tw.write_run()
            f.write(condition_test_template_suffix.format(RootTestName=root_condition_name))

    if inc_json:
        root_condition_name = 'json'
        file_name = file_name_prefix+root_condition_name+'.cpp'
        with open(file_name, 'w') as f:
            f.write(condition_test_template_prefix.format(RootTestName=root_condition_name))
            tw = TestWriter(f)
            save_json_test_cases(tw)
            tw.write_run()
            f.write(condition_test_template_suffix.format(RootTestName=root_condition_name))


def save_fuzz_corpus(corpus_dir_path):
    cdp = Path(corpus_dir_path)
    fulfillments_dir = cdp / 'fulfillments'
    conditions_dir = cdp / 'conditions'
    if not fulfillments_dir.is_dir():
        os.makedirs(fulfillments_dir)
    if not conditions_dir.is_dir():
        os.makedirs(conditions_dir)

    test_cases = partitioned_test_cases()

    fulfillments = []
    for root_condition_name, test_list in test_cases.items():
        for tc in test_list:
            fulfillments.append(Fulfillment.create_from_pylist(tc))

    for tc in json_test_cases.test_cases:
        fulfillments.append(Fulfillment.create_from_json(json_init=tc['json'], json_check=tc))

    for i,f in enumerate(fulfillments):
        file_name = '{}_{}.bin'.format(short_type(f.type_id()), i)
        with open(fulfillments_dir / file_name, 'wb') as out:
            out.write(encode(f.to_asn1()))
        with open(conditions_dir / file_name, 'wb') as out:
            out.write(encode(f.condition().to_asn1()))

def parse_args():
    parser = argparse.ArgumentParser(
        description=('Generate test files for cryptocondtions'))
    parser.add_argument(
        '--fuzz',
        '-f',
        help=('fuzz corpus directory'), )
    parser.add_argument(
        '--prefix',
        '-p',
        help=('c++ test cases file name prefix'), )
    return parser.parse_args()

def run_main():
    args = parse_args()
    if not args.fuzz and not args.prefix:
        print('Must specify at least one of --fuzz or --prefix')
        return

    if args.fuzz:
        save_fuzz_corpus(args.fuzz)
    if args.prefix:
        save_all_test_cases(args.prefix)

if __name__ == '__main__':
    run_main()
