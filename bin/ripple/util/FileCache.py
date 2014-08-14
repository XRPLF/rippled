from __future__ import absolute_import, division, print_function, unicode_literals

import json
import gzip
import os

_NONE = object()

def file_cache(filename_prefix, creator, open=gzip.open, suffix='.gz'):
    """A two-level cache, which stores expensive results in memory and on disk.
    """
    cached_data = {}
    if not os.path.exists(filename_prefix):
        os.makedirs(filename_prefix)

    def get_file_data(name):
        filename = os.path.join(filename_prefix, str(name)) + suffix
        if os.path.exists(filename):
            return json.load(open(filename))

        result = creator(name)
        json.dump(result, open(filename, 'w'))
        return result

    def get_data(name, use_file_cache=True):
        result = cached_data.get(name, _NONE)
        if result is _NONE:
            maker = get_file_data if use_file_cache else creator
            result = maker(name)
            cached_data[name] = result
        return result

    return get_data
