#!/usr/bin/env python

import binascii
import codecs
import pyasn1
import numpy as np
from pyasn1.type import univ
from pyasn1.codec.der import encoder


def to_array(hex_string):
    b = codecs.decode(hex_string.replace(' ',''), 'hex')
    return np.frombuffer(b,dtype=np.int8)


def to_bitstring(x):
    b = f'{x:b}'
    t = tuple(reversed([int(i) for i in b]))
    return univ.BitString(t)


def to_string(v):
    if type(v) == str:
        return v
    return v.decode('utf8')


def escape_hex(v):
    v = to_string(v)
    if len(v) % 2:
        raise ValueError('String must have an even number of characters')
    r = ''
    for i in range(0, len(v), 2):
        r += '\\x' + v[i] + v[i + 1]
    return r


def encode_bitstring(x):
    return escape_hex(binascii.hexlify(encoder.encode(to_bitstring(x))))


def bitstring_testcases(start, end, lshift=0, offset=0):
    mul = 2**lshift
    cases = [((i*mul+offset), encode_bitstring(i*mul+offset)) for i in range(start,end)]
    return cases


def write_bitstring_testcases(f, start, end, lshift=0, offset=0):
    ts = bitstring_testcases(start, end, lshift, offset)

    f.write('''
            {
            std::array<std::pair<unsigned long long, std::string>, 32>
                testCases = {{
    ''')
    to_write = [f'std::make_pair({bits}ull, "{encoding}"s)' for bits, encoding in ts]
    f.write(', '.join(to_write))
    f.write('}};')
    f.write('''
            doTest(testCases, std::integral_constant<std::size_t, 5>{});
            doTest(testCases, std::integral_constant<std::size_t, 16>{});
            }
            ''')


def save_all_test_cases(file_name):
    with open(file_name, 'w') as f:
        write_bitstring_testcases(f, 0, 32)
        write_bitstring_testcases(f, 0, 32, lshift = 14)
