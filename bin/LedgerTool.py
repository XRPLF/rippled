#!/usr/bin/env python

from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger import Server
from ripple.ledger.Args import ARGS
from ripple.util.CommandList import CommandList

from ripple.ledger.commands import Info, Print

_COMMANDS = CommandList(Info, Print)

if __name__ == '__main__':
    server = Server.Server()
    args = list(ARGS.command)
    _COMMANDS.run_safe(args.pop(0), server, *args)
