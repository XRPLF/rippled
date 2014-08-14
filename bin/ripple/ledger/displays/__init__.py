from __future__ import absolute_import, division, print_function, unicode_literals

from functools import wraps

import jsonpath_rw

from ripple.ledger.Args import ARGS
from ripple.ledger.PrettyPrint import pretty_print
from ripple.util.Decimal import Decimal
from ripple.util import Dict
from ripple.util import Range

def ledger_number(server, numbers):
    yield Range.to_string(numbers)

def display(f):
    """A decorator for displays that just print JSON"""
    @wraps(f)
    def wrapper(server, numbers, *args, **kwds):
        for number in numbers:
            ledger = server.get_ledger(number, ARGS.full)
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
            finds = path_expr.find(ledger)
            yield pretty_print(f(finds, *args, **kwds))
    return wrapper


@display
def ledger(led):
    return led

@display
def prune(ledger, level=2):
    return Dict.prune(ledger, level, False)

TRANSACT_FIELDS = (
    'accepted',
    'close_time_human',
    'closed',
    'ledger_index',
    'total_coins',
    'transactions',
)

@display
def transact(ledger):
    return dict((f, ledger[f]) for f in TRANSACT_FIELDS)

@json
def extract(finds):
    return dict((str(f.full_path), str(f.value)) for f in finds)

@json
def sum(finds):
    d = Decimal()
    for f in finds:
        d.accumulate(f.value)
    return [str(d), len(finds)]
