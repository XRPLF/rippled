from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS
from ripple.ledger.PrettyPrint import pretty_print
from ripple.util import Log
from ripple.util import Range

SAFE = True

HELP = """cache
return server_info"""

def cache(server):
    caches = (int(c) for c in server.cache_list(ARGS.full))
    Log.out(Range.to_string(caches))
