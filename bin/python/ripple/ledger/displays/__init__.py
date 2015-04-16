from __future__ import absolute_import, division, print_function, unicode_literals

from functools import wraps

import jsonpath_rw

from ripple.ledger.Args import ARGS
from ripple.util import Dict
from ripple.util import Log
from ripple.util import Range
from ripple.util.Decimal import Decimal
from ripple.util.PrettyPrint import pretty_print, Streamer

TRANSACT_FIELDS = (
    'accepted',
    'close_time_human',
    'closed',
    'ledger_index',
    'total_coins',
    'transactions',
)

LEDGER_FIELDS = (
    'accepted',
    'accountState',
    'close_time_human',
    'closed',
    'ledger_index',
    'total_coins',
    'transactions',
)

def _dict_filter(d, keys):
    return dict((k, v) for (k, v) in d.items() if k in keys)

def ledger_number(print, server, numbers):
    print(Range.to_string(numbers))

def display(f):
    @wraps(f)
    def wrapper(printer, server, numbers, *args):
        streamer = Streamer(printer=printer)
        for number in numbers:
            ledger = server.get_ledger(number, ARGS.full)
            if ledger:
                streamer.add(number, f(ledger, *args))
        streamer.finish()
    return wrapper

def extractor(f):
    @wraps(f)
    def wrapper(printer, server, numbers, *paths):
        try:
            find = jsonpath_rw.parse('|'.join(paths)).find
        except:
            raise ValueError("Can't understand jsonpath '%s'." % path)
        def fn(ledger, *args):
            return f(find(ledger), *args)
        display(fn)(printer, server, numbers)
    return wrapper

@display
def ledger(ledger, full=False):
    if ARGS.full:
        if full:
            return ledger

        ledger = Dict.prune(ledger, 1, False)

    return _dict_filter(ledger, LEDGER_FIELDS)

@display
def prune(ledger, level=1):
    return Dict.prune(ledger, level, False)

@display
def transact(ledger):
    return _dict_filter(ledger, TRANSACT_FIELDS)

@extractor
def extract(finds):
    return dict((str(f.full_path), str(f.value)) for f in finds)

@extractor
def sum(finds):
    d = Decimal()
    for f in finds:
        d.accumulate(f.value)
    return [str(d), len(finds)]
