from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS

from functools import wraps
import json

def pretty_print(item):
    return json.dumps(item,
                      sort_keys=True,
                      indent=ARGS.indent,
                      separators=(',', ': '))

def pretty(f):
    """"A decorator on a function that makes its results pretty """
    @wraps(f)
    def wrapper(*args, **kwds):
        result = list(f(*args, **kwds))
        return pretty_print(result)

    return wrapper
