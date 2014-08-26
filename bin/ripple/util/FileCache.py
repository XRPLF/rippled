from __future__ import absolute_import, division, print_function, unicode_literals

import gzip
import json
import os

_NONE = object()

class FileCache(object):
    """A two-level cache, which stores expensive results in memory and on disk.
    """
    def __init__(self, cache_directory, creator, open=gzip.open, suffix='.gz'):
        self.cache_directory = cache_directory
        self.creator = creator
        self.open = open
        self.suffix = suffix
        self.cached_data = {}
        if not os.path.exists(self.cache_directory):
            os.makedirs(self.cache_directory)

    def get_file_data(self, name):
        filename = os.path.join(self.cache_directory, str(name)) + self.suffix
        if os.path.exists(filename):
            return json.load(self.open(filename))

        result = self.creator(name)
        json.dump(result, self.open(filename, 'w'))
        return result

    def get_data(self, name, use_file_cache=True):
        result = self.cached_data.get(name, _NONE)
        if result is _NONE:
            maker = self.get_file_data if use_file_cache else self.creator
            result = maker(name)
            self.cached_data[name] = result
        return result

    def cache_list(self):
        for f in os.listdir(self.cache_directory):
            if f.endswith(self.suffix):
                yield f[:-len(self.suffix)]
