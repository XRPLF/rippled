# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

from .Variant import Variant

from glob import glob

def _files(add_files, globs, kwds):
    file_list = []
    for g in globs:
        files = glob(g)
        assert files, 'Glob %s didn\'t match any files.' % g
        file_list += files

    def run(variant):
        add_files(variant, *file_list, **kwds)
    return run


def files(*globs, **kwds):
    return _files(Variant.add_source_files, globs, kwds)


def directories(*globs, **kwds):
    return _files(Variant.add_source_directories, globs, kwds)
