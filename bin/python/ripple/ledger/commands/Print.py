from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS
from ripple.ledger import SearchLedgers

import json

SAFE = True

HELP = """print

Print the ledgers to stdout.  The default command."""

def run_print(server):
    ARGS.display(print, server, SearchLedgers.search(server))
