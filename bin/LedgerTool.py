#!/usr/bin/env python

from __future__ import absolute_import, division, print_function, unicode_literals

import sys
import traceback

from ripple.ledger import Server
from ripple.ledger.commands import Cache, Info, Print
from ripple.ledger.Args import ARGS
from ripple.util import Log
from ripple.util.CommandList import CommandList

_COMMANDS = CommandList(Cache, Info, Print)

if __name__ == '__main__':
    try:
        server = Server.Server()
        args = list(ARGS.command)
        _COMMANDS.run_safe(args.pop(0), server, *args)
    except Exception as e:
        if ARGS.verbose:
            print(traceback.format_exc(), sys.stderr)
        Log.error(e)
