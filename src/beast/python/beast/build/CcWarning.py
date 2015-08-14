# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

WARNINGS = {
    'maybe-uninitialized': ['gcc'],
}

def disable_warnings(tags, *warnings):
    assert warnings
    result = []
    for warn in warnings:
        for toolkit in WARNINGS.get(warn, ['clang', 'gcc']):
            if toolkit in tags:
                result.append('-Wno-' + warn)
                break
    return result
