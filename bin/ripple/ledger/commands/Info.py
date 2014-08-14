from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS
from ripple.ledger import Log
from ripple.ledger.PrettyPrint import pretty_print
from ripple.util import Range

SAFE = True

HELP = 'info - return server_info'

def info(server):
    Log.out('closed =', server.closed)
    Log.out('current =', server.current)
    Log.out('validated =', server.validated)
    Log.out('complete =', Range.to_string(server.complete))

    if ARGS.full:
        Log.out(pretty_print(server.info()))
