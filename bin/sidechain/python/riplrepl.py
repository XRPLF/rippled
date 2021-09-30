#!/usr/bin/env python3
'''
Script to run an interactive shell to test sidechains.
'''

import sys

from common import disable_eprint, eprint
import interactive
import sidechain


def main():
    params = sidechain.Params()
    params.interactive = True

    interactive.set_hooks_dir(params.hooks_dir)

    if err_str := params.check_error():
        eprint(err_str)
        sys.exit(1)

    if params.verbose:
        print("eprint enabled")
    else:
        disable_eprint()

    if params.standalone:
        sidechain.standalone_interactive_repl(params)
    else:
        sidechain.multinode_interactive_repl(params)


if __name__ == '__main__':
    main()
