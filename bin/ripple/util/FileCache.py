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
        if os.path.exists(filename):
            return json.load(self.open(filename))

        result = self.creator(name)
        return result

    def get_data(self, name, save_in_cache, can_create, default=None):
        name = str(name)
        result = self.cached_data.get(name, _NONE)
        if result is _NONE:
            filename = os.path.join(self.cache_directory, name) + self.suffix
            if os.path.exists(filename):
                result = json.load(self.open(filename)) or _NONE
            if result is _NONE and can_create:
                result = self.creator(name)
                if save_in_cache:
                    json.dump(result, self.open(filename, 'w'))
        return default if result is _NONE else result

    def _files(self):
        return os.listdir(self.cache_directory)

    def cache_list(self):
        for f in self._files():
            if f.endswith(self.suffix):
                yield f[:-len(self.suffix)]

    def file_count(self):
        return len(self._files())

    def clear(self):
        """Clears both local files and memory."""
        self.cached_data = {}
        for f in self._files():
            os.remove(os.path.join(self.cache_directory, f))
