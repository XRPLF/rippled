# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import glob, os

"""
This module is concerned with functions of a single variable, which
will be either a State or a Variant.
"""

def compose(*functions):
    def wrapped(x):
        for f in functions:
            f(x)
    return wrapped


def _tagger(items, matcher):
    tags, functions = set(), []

    for i in items:
        (functions.append if callable(i) else tags.add)(i)

    def wrapped(build):
        t = set(getattr(build, 'tags', ()))
        if matcher(tags, t):
            for f in functions:
                f(build)

    return wrapped

def for_tags(*items):
    return _tagger(items, lambda tags, t: not (tags - t))

def not_tags(*items):
    return _tagger(items, lambda tags, t: not (tags & t))
