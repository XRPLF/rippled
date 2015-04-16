from __future__ import absolute_import, division, print_function, unicode_literals

import json
import os

from ripple.ledger import DatabaseReader, RippledReader
from ripple.ledger.Args import ARGS
from ripple.util.FileCache import FileCache
from ripple.util import ConfigFile
from ripple.util import File
from ripple.util import Range

class Server(object):
    def __init__(self):
        cfg_file = File.normalize(ARGS.config or 'rippled.cfg')
        self.config = ConfigFile.read(open(cfg_file))
        if ARGS.database != ARGS.NONE:
            reader = DatabaseReader.DatabaseReader(self.config)
        else:
            reader = RippledReader.RippledReader(self.config)

        self.reader = reader
        self.complete = reader.complete

        names = {
            'closed': reader.name_to_ledger_index('closed'),
            'current': reader.name_to_ledger_index('current'),
            'validated': reader.name_to_ledger_index('validated'),
            'first': self.complete[0] if self.complete else None,
            'last': self.complete[-1] if self.complete else None,
        }
        self.__dict__.update(names)
        self.ledgers = sorted(Range.join_ranges(*ARGS.ledgers, **names))

        def make_cache(is_full):
            name = 'full' if is_full else 'summary'
            filepath = os.path.join(ARGS.cache, name)
            creator = lambda n: reader.get_ledger(n, is_full)
            return FileCache(filepath, creator)
        self._caches = [make_cache(False), make_cache(True)]

    def info(self):
        return self.reader.info

    def cache(self, is_full):
        return self._caches[is_full]

    def get_ledger(self, number, is_full=False):
        num = int(number)
        save_in_cache = num in self.complete
        can_create = (not ARGS.offline and
                      self.complete and
                      self.complete[0] <= num - 1)
        cache = self.cache(is_full)
        return cache.get_data(number, save_in_cache, can_create)
