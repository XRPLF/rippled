from __future__ import absolute_import, division, print_function, unicode_literals

import sys

def out(*args, **kwds):
    kwds.get('print', print)(*args, file=sys.stdout, **kwds)

def info(*args, **kwds):
    from ripple.ledger.Args import ARGS
    if ARGS.verbose:
        out(*args, **kwds)

def warn(*args, **kwds):
    out('WARNING:', *args, **kwds)

def error(*args, **kwds):
    out('ERROR:', *args, **kwds)

def fatal(*args, **kwds):
    out('FATAL:', *args, **kwds)
    raise Exception('FATAL: ' + ' '.join(str(a) for a in args))
