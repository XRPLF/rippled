from __future__ import absolute_import, division, print_function, unicode_literals

import json
import os

from ripple.ledger import RippledReader, ServerReader
from ripple.ledger.Args import ARGS
from ripple.util.FileCache import file_cache
from ripple.util import Range

class Server(object):
    def __init__(self):
        if ARGS.rippled:
            reader = RippledReader.RippledReader()
        else:
            reader = ServerReader.ServerReader()

        self.reader = reader

        self.complete = reader.complete

        names = {
            'closed': reader.name_to_ledger_index('closed'),
            'current': reader.name_to_ledger_index('current'),
            'validated': reader.name_to_ledger_index('validated'),
            'first': self.complete[0],
            'last': self.complete[-1],
        }
        self.__dict__.update(names)
        self.ledgers = sorted(Range.join_ranges(*ARGS.ledgers, **names))

        def make_cache(is_full):
            name = 'full' if is_full else 'summary'
            filepath = os.path.join(ARGS.cache, name)
            creator = lambda n: reader.get_ledger(n, is_full)
            return file_cache(filepath, creator)
        self.caches = [make_cache(False), make_cache(True)]

    def info(self):
        return self.reader.info

    def get_ledger(self, number, is_full=False):
        return self.caches[is_full](number, int(number) in self.complete)
