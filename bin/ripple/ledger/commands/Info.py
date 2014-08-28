from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS
from ripple.util import Log
from ripple.util import Range
from ripple.util.PrettyPrint import pretty_print

SAFE = True

HELP = 'info - return server_info'

def info(server):
    Log.out('first = ', server.first)
    Log.out('last = ', server.last)
    Log.out('closed =', server.closed)
    Log.out('current =', server.current)
    Log.out('validated =', server.validated)
    Log.out('complete =', Range.to_string(server.complete))

    if ARGS.full:
        Log.out(pretty_print(server.info()))
