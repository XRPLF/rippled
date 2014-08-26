from __future__ import absolute_import, division, print_function, unicode_literals

from functools import wraps

import jsonpath_rw

from ripple.ledger.Args import ARGS
from ripple.ledger.PrettyPrint import pretty_print
from ripple.util import Dict
from ripple.util import Log
from ripple.util import Range
from ripple.util.Decimal import Decimal

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

def ledger_number(server, numbers):
    yield Range.to_string(numbers)

def display(f):
    """A decorator for displays that just print JSON"""
    @wraps(f)
    def wrapper(server, numbers, *args, **kwds):
        for number in numbers:
            ledger = server.get_ledger(number, ARGS.full)
            if ledger:
                yield pretty_print(f(ledger, *args, **kwds))
    return wrapper

def json(f):
    """A decorator for displays that print JSON, extracted by a path"""
    @wraps(f)
    def wrapper(server, numbers, path, *args, **kwds):
        try:
            path_expr = jsonpath_rw.parse(path)
        except:
            raise ValueError("Can't understand jsonpath '%s'." % path)

        for number in numbers:
            ledger = server.get_ledger(number, ARGS.full)
            if ledger:
                finds = path_expr.find(ledger)
                yield pretty_print(f(finds, *args, **kwds))
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

@json
def extract(finds):
    return dict((str(f.full_path), str(f.value)) for f in finds)

@json
def sum(finds):
    d = Decimal()
    for f in finds:
        d.accumulate(f.value)
    return [str(d), len(finds)]
