#!/usr/bin/env python3
'''
Script to test and debug sidechains.

The mainchain exe location can be set through the command line or
the environment variable RIPPLED_MAINCHAIN_EXE

The sidechain exe location can be set through the command line or
the environment variable RIPPLED_SIDECHAIN_EXE

The configs_dir (generated with create_config_files.py) can be set through the command line
or the environment variable RIPPLED_SIDECHAIN_CFG_DIR
'''

import argparse
import json
from multiprocessing import Process, Value
import os
import sys
import time
from typing import Callable, Dict, List, Optional

from app import App, single_client_app, testnet_app, configs_for_testnet
from command import AccountInfo, AccountTx, LedgerAccept, LogLevel, Subscribe
from common import Account, Asset, eprint, disable_eprint, XRP
from config_file import ConfigFile
import interactive
from log_analyzer import convert_log
from test_utils import mc_wait_for_payment_detect, sc_wait_for_payment_detect, mc_connect_subscription, sc_connect_subscription
from transaction import AccountSet, Payment, SignerListSet, SetRegularKey, Ticket, Trust


def parse_args_helper(parser: argparse.ArgumentParser):

    parser.add_argument(
        '--debug_sidechain',
        '-ds',
        action='store_true',
        help=('Mode to debug sidechain (prompt to run sidechain in gdb)'),
    )

    parser.add_argument(
        '--debug_mainchain',
        '-dm',
        action='store_true',
        help=('Mode to debug mainchain (prompt to run sidechain in gdb)'),
    )

    parser.add_argument(
        '--exe_mainchain',
        '-em',
        help=('path to mainchain rippled executable'),
    )

    parser.add_argument(
        '--exe_sidechain',
        '-es',
        help=('path to mainchain rippled executable'),
    )

    parser.add_argument(
        '--cfgs_dir',
        '-c',
        help=
        ('path to configuration file dir (generated with create_config_files.py)'
         ),
    )

    parser.add_argument(
        '--standalone',
        '-a',
        action='store_true',
        help=('run standalone tests'),
    )

    parser.add_argument(
        '--interactive',
        '-i',
        action='store_true',
        help=('run interactive repl'),
    )

    parser.add_argument(
        '--quiet',
        '-q',
        action='store_true',
        help=('Disable printing errors (eprint disabled)'),
    )

    parser.add_argument(
        '--verbose',
        '-v',
        action='store_true',
        help=('Enable printing errors (eprint enabled)'),
    )

    # Pauses are use for attaching debuggers and looking at logs are know checkpoints
    parser.add_argument(
        '--with_pauses',
        '-p',
        action='store_true',
        help=
        ('Add pauses at certain checkpoints in tests until "enter" key is hit'
         ),
    )

    parser.add_argument(
        '--hooks_dir',
        help=('path to hooks dir'),
    )


def parse_args():
    parser = argparse.ArgumentParser(description=('Test and debug sidechains'))
    parse_args_helper(parser)
    return parser.parse_known_args()[0]


class Params:
    def __init__(self, *, configs_dir: Optional[str] = None):
        args = parse_args()

        self.debug_sidechain = False
        if args.debug_sidechain:
            self.debug_sidechain = args.debug_sidechain
        self.debug_mainchain = False
        if args.debug_mainchain:
            self.debug_mainchain = arts.debug_mainchain

        # Undocumented feature: if the environment variable RIPPLED_SIDECHAIN_RR is set, it is
        # assumed to point to the rr executable. Sidechain server 0 will then be run under rr.
        self.sidechain_rr = None
        if 'RIPPLED_SIDECHAIN_RR' in os.environ:
            self.sidechain_rr = os.environ['RIPPLED_SIDECHAIN_RR']

        self.standalone = args.standalone
        self.with_pauses = args.with_pauses
        self.interactive = args.interactive
        self.quiet = args.quiet
        self.verbose = args.verbose

        self.mainchain_exe = None
        if 'RIPPLED_MAINCHAIN_EXE' in os.environ:
            self.mainchain_exe = os.environ['RIPPLED_MAINCHAIN_EXE']
        if args.exe_mainchain:
            self.mainchain_exe = args.exe_mainchain

        self.sidechain_exe = None
        if 'RIPPLED_SIDECHAIN_EXE' in os.environ:
            self.sidechain_exe = os.environ['RIPPLED_SIDECHAIN_EXE']
        if args.exe_sidechain:
            self.sidechain_exe = args.exe_sidechain

        self.configs_dir = None
        if 'RIPPLED_SIDECHAIN_CFG_DIR' in os.environ:
            self.configs_dir = os.environ['RIPPLED_SIDECHAIN_CFG_DIR']
        if args.cfgs_dir:
            self.configs_dir = args.cfgs_dir
        if configs_dir is not None:
            self.configs_dir = configs_dir

        self.hooks_dir = None
        if 'RIPPLED_SIDECHAIN_HOOKS_DIR' in os.environ:
            self.hooks_dir = os.environ['RIPPLED_SIDECHAIN_HOOKS_DIR']
        if args.hooks_dir:
            self.hooks_dir = args.hooks_dir

        if not self.configs_dir:
            self.mainchain_config = None
            self.sidechain_config = None
            self.sidechain_bootstrap_config = None
            self.genesis_account = None
            self.mc_door_account = None
            self.user_account = None
            self.sc_door_account = None
            self.federators = None
            return

        if self.standalone:
            self.mainchain_config = ConfigFile(
                file_name=f'{self.configs_dir}/main.no_shards.dog/rippled.cfg')
            self.sidechain_config = ConfigFile(
                file_name=
                f'{self.configs_dir}/main.no_shards.dog.sidechain/rippled.cfg')
            self.sidechain_bootstrap_config = ConfigFile(
                file_name=
                f'{self.configs_dir}/main.no_shards.dog.sidechain/sidechain_bootstrap.cfg'
            )
        else:
            self.mainchain_config = ConfigFile(
                file_name=
                f'{self.configs_dir}/sidechain_testnet/main.no_shards.mainchain_0/rippled.cfg'
            )
            self.sidechain_config = ConfigFile(
                file_name=
                f'{self.configs_dir}/sidechain_testnet/sidechain_0/rippled.cfg'
            )
            self.sidechain_bootstrap_config = ConfigFile(
                file_name=
                f'{self.configs_dir}/sidechain_testnet/sidechain_0/sidechain_bootstrap.cfg'
            )

        self.genesis_account = Account(
            account_id='rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
            secret_key='masterpassphrase',
            nickname='genesis')
        self.mc_door_account = Account(
            account_id=self.sidechain_config.sidechain.mainchain_account,
            secret_key=self.sidechain_bootstrap_config.sidechain.
            mainchain_secret,
            nickname='door')
        self.user_account = Account(
            account_id='rJynXY96Vuq6B58pST9K5Ak5KgJ2JcRsQy',
            secret_key='snVsJfrr2MbVpniNiUU6EDMGBbtzN',
            nickname='alice')

        self.sc_door_account = Account(
            account_id='rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
            secret_key='masterpassphrase',
            nickname='door')
        self.federators = [
            l.split()[1].strip() for l in
            self.sidechain_bootstrap_config.sidechain_federators.get_lines()
        ]

    def check_error(self) -> str:
        '''
        Check for errors. Return `None` if no errors,
        otherwise return a string describing the error
        '''
        if not self.mainchain_exe:
            return 'Missing mainchain_exe location. Either set the env variable RIPPLED_MAINCHAIN_EXE or use the --exe_mainchain command line switch'
        if not self.sidechain_exe:
            return 'Missing sidechain_exe location. Either set the env variable RIPPLED_SIDECHAIN_EXE or use the --exe_sidechain command line switch'
        if not self.configs_dir:
            return 'Missing configs directory location. Either set the env variable RIPPLED_SIDECHAIN_CFG_DIR or use the --cfgs_dir command line switch'
        if self.verbose and self.quiet:
            return 'Cannot specify both verbose and quiet options at the same time'


mainDoorKeeper = 0
sideDoorKeeper = 1
updateSignerList = 2


def setup_mainchain(mc_app: App,
                    params: Params,
                    setup_user_accounts: bool = True):
    mc_app.add_to_keymanager(params.mc_door_account)
    if setup_user_accounts:
        mc_app.add_to_keymanager(params.user_account)

    mc_app(LogLevel('fatal'))

    # Allow rippling through the genesis account
    mc_app(AccountSet(account=params.genesis_account).set_default_ripple(True))
    mc_app.maybe_ledger_accept()

    # Create and fund the mc door account
    mc_app(
        Payment(account=params.genesis_account,
                dst=params.mc_door_account,
                amt=XRP(10_000)))
    mc_app.maybe_ledger_accept()

    # Create a trust line so USD/root account ious can be sent cross chain
    mc_app(
        Trust(account=params.mc_door_account,
              limit_amt=Asset(value=1_000_000,
                              currency='USD',
                              issuer=params.genesis_account)))

    # set the chain's signer list and disable the master key
    divide = 4 * len(params.federators)
    by = 5
    quorum = (divide + by - 1) // by
    mc_app(
        SignerListSet(account=params.mc_door_account,
                      quorum=quorum,
                      keys=params.federators))
    mc_app.maybe_ledger_accept()
    r = mc_app(Ticket(account=params.mc_door_account, src_tag=mainDoorKeeper))
    mc_app.maybe_ledger_accept()
    mc_app(Ticket(account=params.mc_door_account, src_tag=sideDoorKeeper))
    mc_app.maybe_ledger_accept()
    mc_app(Ticket(account=params.mc_door_account, src_tag=updateSignerList))
    mc_app.maybe_ledger_accept()
    mc_app(AccountSet(account=params.mc_door_account).set_disable_master())
    mc_app.maybe_ledger_accept()

    if setup_user_accounts:
        # Create and fund a regular user account
        mc_app(
            Payment(account=params.genesis_account,
                    dst=params.user_account,
                    amt=XRP(2_000)))
        mc_app.maybe_ledger_accept()


def setup_sidechain(sc_app: App,
                    params: Params,
                    setup_user_accounts: bool = True):
    sc_app.add_to_keymanager(params.sc_door_account)
    if setup_user_accounts:
        sc_app.add_to_keymanager(params.user_account)

    sc_app(LogLevel('fatal'))
    sc_app(LogLevel('trace', partition='SidechainFederator'))

    # set the chain's signer list and disable the master key
    divide = 4 * len(params.federators)
    by = 5
    quorum = (divide + by - 1) // by
    sc_app(
        SignerListSet(account=params.genesis_account,
                      quorum=quorum,
                      keys=params.federators))
    sc_app.maybe_ledger_accept()
    sc_app(Ticket(account=params.genesis_account, src_tag=mainDoorKeeper))
    sc_app.maybe_ledger_accept()
    sc_app(Ticket(account=params.genesis_account, src_tag=sideDoorKeeper))
    sc_app.maybe_ledger_accept()
    sc_app(Ticket(account=params.genesis_account, src_tag=updateSignerList))
    sc_app.maybe_ledger_accept()
    sc_app(AccountSet(account=params.genesis_account).set_disable_master())
    sc_app.maybe_ledger_accept()


def _xchain_transfer(from_chain: App, to_chain: App, src: Account,
                     dst: Account, amt: Asset, from_chain_door: Account,
                     to_chain_door: Account):
    memos = [{'Memo': {'MemoData': dst.account_id_str_as_hex()}}]
    from_chain(Payment(account=src, dst=from_chain_door, amt=amt, memos=memos))
    from_chain.maybe_ledger_accept()
    if to_chain.standalone:
        # from_chain (side chain) sends a txn, but won't close the to_chain (main chain) ledger
        time.sleep(1)
        to_chain.maybe_ledger_accept()


def main_to_side_transfer(mc_app: App, sc_app: App, src: Account, dst: Account,
                          amt: Asset, params: Params):
    _xchain_transfer(mc_app, sc_app, src, dst, amt, params.mc_door_account,
                     params.sc_door_account)


def side_to_main_transfer(mc_app: App, sc_app: App, src: Account, dst: Account,
                          amt: Asset, params: Params):
    _xchain_transfer(sc_app, mc_app, src, dst, amt, params.sc_door_account,
                     params.mc_door_account)


def simple_test(mc_app: App, sc_app: App, params: Params):
    try:
        bob = sc_app.create_account('bob')
        main_to_side_transfer(mc_app, sc_app, params.user_account, bob,
                              XRP(200), params)
        main_to_side_transfer(mc_app, sc_app, params.user_account, bob,
                              XRP(60), params)

        if params.with_pauses:
            _convert_log_files_to_json(
                mc_app.get_configs() + sc_app.get_configs(),
                'checkpoint1.json')
            input(
                "Pausing to check for main -> side txns (press enter to continue)"
            )

        side_to_main_transfer(mc_app, sc_app, bob, params.user_account, XRP(9),
                              params)
        side_to_main_transfer(mc_app, sc_app, bob, params.user_account,
                              XRP(11), params)

        if params.with_pauses:
            input(
                "Pausing to check for side -> main txns (press enter to continue)"
            )
    finally:
        _convert_log_files_to_json(mc_app.get_configs() + sc_app.get_configs(),
                                   'final.json')


def _rm_debug_log(config: ConfigFile):
    try:
        debug_log = config.debug_logfile.get_line()
        if debug_log:
            print(f'removing debug file: {debug_log}', flush=True)
            os.remove(debug_log)
    except:
        pass


def _standalone_with_callback(params: Params,
                              callback: Callable[[App, App], None],
                              setup_user_accounts: bool = True):

    if (params.debug_mainchain):
        input("Start mainchain server and press enter to continue: ")
    else:
        _rm_debug_log(params.mainchain_config)
    with single_client_app(config=params.mainchain_config,
                           exe=params.mainchain_exe,
                           standalone=True,
                           run_server=not params.debug_mainchain) as mc_app:

        mc_connect_subscription(mc_app, params.mc_door_account)
        setup_mainchain(mc_app, params, setup_user_accounts)

        if (params.debug_sidechain):
            input("Start sidechain server and press enter to continue: ")
        else:
            _rm_debug_log(params.sidechain_config)
        with single_client_app(
                config=params.sidechain_config,
                exe=params.sidechain_exe,
                standalone=True,
                run_server=not params.debug_sidechain) as sc_app:

            sc_connect_subscription(sc_app, params.sc_door_account)
            setup_sidechain(sc_app, params, setup_user_accounts)
            callback(mc_app, sc_app)


def _convert_log_files_to_json(to_convert: List[ConfigFile], suffix: str):
    '''
    Convert the log file to json
    '''
    for c in to_convert:
        try:
            debug_log = c.debug_logfile.get_line()
            if not os.path.exists(debug_log):
                continue
            converted_log = f'{debug_log}.{suffix}'
            if os.path.exists(converted_log):
                os.remove(converted_log)
            print(f'Converting log {debug_log} to {converted_log}', flush=True)
            convert_log(debug_log, converted_log, pure_json=True)
        except:
            eprint(f'Exception converting log')


def _multinode_with_callback(params: Params,
                             callback: Callable[[App, App], None],
                             setup_user_accounts: bool = True):

    mainchain_cfg = ConfigFile(
        file_name=
        f'{params.configs_dir}/sidechain_testnet/main.no_shards.mainchain_0/rippled.cfg'
    )
    _rm_debug_log(mainchain_cfg)
    if params.debug_mainchain:
        input("Start mainchain server and press enter to continue: ")
    with single_client_app(config=mainchain_cfg,
                           exe=params.mainchain_exe,
                           standalone=True,
                           run_server=not params.debug_mainchain) as mc_app:

        if params.with_pauses:
            input("Pausing after mainchain start (press enter to continue)")

        mc_connect_subscription(mc_app, params.mc_door_account)
        setup_mainchain(mc_app, params, setup_user_accounts)
        if params.with_pauses:
            input("Pausing after mainchain setup (press enter to continue)")

        testnet_configs = configs_for_testnet(
            f'{params.configs_dir}/sidechain_testnet/sidechain_')
        for c in testnet_configs:
            _rm_debug_log(c)

        run_server_list = [True] * len(testnet_configs)
        if params.debug_sidechain:
            run_server_list[0] = False
            input(
                f'Start testnet server {testnet_configs[0].get_file_name()} and press enter to continue: '
            )

        with testnet_app(exe=params.sidechain_exe,
                         configs=testnet_configs,
                         run_server=run_server_list,
                         sidechain_rr=params.sidechain_rr) as n_app:

            if params.with_pauses:
                input("Pausing after testnet start (press enter to continue)")

            sc_connect_subscription(n_app, params.sc_door_account)
            setup_sidechain(n_app, params, setup_user_accounts)
            if params.with_pauses:
                input(
                    "Pausing after sidechain setup (press enter to continue)")
            callback(mc_app, n_app)


def standalone_test(params: Params):
    def callback(mc_app: App, sc_app: App):
        simple_test(mc_app, sc_app, params)

    _standalone_with_callback(params, callback)


def multinode_test(params: Params):
    def callback(mc_app: App, sc_app: App):
        simple_test(mc_app, sc_app, params)

    _multinode_with_callback(params, callback)


# The mainchain runs in standalone mode. Most operations - like cross chain
# paymens - will automatically close ledgers. However, some operations, like
# refunds need an extra close. This loop automatically closes ledgers.
def close_mainchain_ledgers(stop_token: Value, params: Params, sleep_time=4):
    with single_client_app(config=params.mainchain_config,
                           exe=params.mainchain_exe,
                           standalone=True,
                           run_server=False) as mc_app:
        while stop_token.value != 0:
            mc_app.maybe_ledger_accept()
            time.sleep(sleep_time)


def standalone_interactive_repl(params: Params):
    def callback(mc_app: App, sc_app: App):
        # process will run while stop token is non-zero
        stop_token = Value('i', 1)
        p = None
        if mc_app.standalone:
            p = Process(target=close_mainchain_ledgers,
                        args=(stop_token, params))
            p.start()
        try:
            interactive.repl(mc_app, sc_app)
        finally:
            if p:
                stop_token.value = 0
                p.join()

    _standalone_with_callback(params, callback, setup_user_accounts=False)


def multinode_interactive_repl(params: Params):
    def callback(mc_app: App, sc_app: App):
        # process will run while stop token is non-zero
        stop_token = Value('i', 1)
        p = None
        if mc_app.standalone:
            p = Process(target=close_mainchain_ledgers,
                        args=(stop_token, params))
            p.start()
        try:
            interactive.repl(mc_app, sc_app)
        finally:
            if p:
                stop_token.value = 0
                p.join()

    _multinode_with_callback(params, callback, setup_user_accounts=False)


def main():
    params = Params()
    interactive.set_hooks_dir(params.hooks_dir)

    if err_str := params.check_error():
        eprint(err_str)
        sys.exit(1)

    if params.quiet:
        print("Disabling eprint")
        disable_eprint()

    if params.interactive:
        if params.standalone:
            standalone_interactive_repl(params)
        else:
            multinode_interactive_repl(params)
    elif params.standalone:
        standalone_test(params)
    else:
        multinode_test(params)


if __name__ == '__main__':
    main()
