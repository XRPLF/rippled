import logging
import pprint
import pytest
from multiprocessing import Process, Value
from typing import Dict
import sys

from app import App
from common import Asset, eprint, disable_eprint, XRP
import interactive
from sidechain import Params
import sidechain
import test_utils
import time
from transaction import Payment, Trust


def simple_xrp_test(mc_app: App, sc_app: App, params: Params):
    alice = mc_app.account_from_alias('alice')
    adam = sc_app.account_from_alias('adam')

    # main to side
    # First txn funds the side chain account
    with test_utils.test_context(mc_app, sc_app):
        to_send_asset = XRP(1000)
        pre_bal = sc_app.get_balance(adam, to_send_asset)
        sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam,
                                        to_send_asset, params)
        test_utils.wait_for_balance_change(sc_app, adam, pre_bal,
                                           to_send_asset)

    for i in range(2):
        # even amounts for main to side
        for value in range(10, 20, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = XRP(value)
                pre_bal = sc_app.get_balance(adam, to_send_asset)
                sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam,
                                                to_send_asset, params)
                test_utils.wait_for_balance_change(sc_app, adam, pre_bal,
                                                   to_send_asset)

        # side to main
        # odd amounts for side to main
        for value in range(9, 19, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = XRP(value)
                pre_bal = mc_app.get_balance(alice, to_send_asset)
                sidechain.side_to_main_transfer(mc_app, sc_app, adam, alice,
                                                to_send_asset, params)
                test_utils.wait_for_balance_change(mc_app, alice, pre_bal,
                                                   to_send_asset)


def simple_iou_test(mc_app: App, sc_app: App, params: Params):
    alice = mc_app.account_from_alias('alice')
    adam = sc_app.account_from_alias('adam')

    mc_asset = Asset(value=0,
                     currency='USD',
                     issuer=mc_app.account_from_alias('root'))
    sc_asset = Asset(value=0,
                     currency='USD',
                     issuer=sc_app.account_from_alias('door'))
    mc_app.add_asset_alias(mc_asset, 'mcd')  # main chain dollar
    sc_app.add_asset_alias(sc_asset, 'scd')  # side chain dollar
    mc_app(Trust(account=alice, limit_amt=mc_asset(1_000_000)))

    ## make sure adam account on the side chain exists and set the trust line
    with test_utils.test_context(mc_app, sc_app):
        sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam, XRP(300),
                                        params)

    # create a trust line to alice and pay her USD/root
    mc_app(Trust(account=alice, limit_amt=mc_asset(1_000_000)))
    mc_app.maybe_ledger_accept()
    mc_app(
        Payment(account=mc_app.account_from_alias('root'),
                dst=alice,
                amt=mc_asset(10_000)))
    mc_app.maybe_ledger_accept()

    # create a trust line for adam
    sc_app(Trust(account=adam, limit_amt=sc_asset(1_000_000)))

    for i in range(2):
        # even amounts for main to side
        for value in range(10, 20, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = mc_asset(value)
                rcv_asset = sc_asset(value)
                pre_bal = sc_app.get_balance(adam, rcv_asset)
                sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam,
                                                to_send_asset, params)
                test_utils.wait_for_balance_change(sc_app, adam, pre_bal,
                                                   rcv_asset)

        # side to main
        # odd amounts for side to main
        for value in range(9, 19, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = sc_asset(value)
                rcv_asset = mc_asset(value)
                pre_bal = mc_app.get_balance(alice, to_send_asset)
                sidechain.side_to_main_transfer(mc_app, sc_app, adam, alice,
                                                to_send_asset, params)
                test_utils.wait_for_balance_change(mc_app, alice, pre_bal,
                                                   rcv_asset)


def run(mc_app: App, sc_app: App, params: Params):
    # process will run while stop token is non-zero
    stop_token = Value('i', 1)
    p = None
    if mc_app.standalone:
        p = Process(target=sidechain.close_mainchain_ledgers,
                    args=(stop_token, params))
        p.start()
    try:
        # TODO: Tests fail without this sleep. Fix this bug.
        time.sleep(10)
        setup_accounts(mc_app, sc_app, params)
        simple_xrp_test(mc_app, sc_app, params)
        simple_iou_test(mc_app, sc_app, params)
    finally:
        if p:
            stop_token.value = 0
            p.join()
        sidechain._convert_log_files_to_json(
            mc_app.get_configs() + sc_app.get_configs(), 'final.json')


def standalone_test(params: Params):
    def callback(mc_app: App, sc_app: App):
        run(mc_app, sc_app, params)

    sidechain._standalone_with_callback(params,
                                        callback,
                                        setup_user_accounts=False)


def setup_accounts(mc_app: App, sc_app: App, params: Params):
    # Setup a funded user account on the main chain, and add an unfunded account.
    # Setup address book and add a funded account on the mainchain.
    # Typical female names are addresses on the mainchain.
    # The first account is funded.
    alice = mc_app.create_account('alice')
    beth = mc_app.create_account('beth')
    carol = mc_app.create_account('carol')
    deb = mc_app.create_account('deb')
    ella = mc_app.create_account('ella')
    mc_app(Payment(account=params.genesis_account, dst=alice, amt=XRP(20_000)))
    mc_app.maybe_ledger_accept()

    # Typical male names are addresses on the sidechain.
    # All accounts are initially unfunded
    adam = sc_app.create_account('adam')
    bob = sc_app.create_account('bob')
    charlie = sc_app.create_account('charlie')
    dan = sc_app.create_account('dan')
    ed = sc_app.create_account('ed')


def multinode_test(params: Params):
    def callback(mc_app: App, sc_app: App):
        run(mc_app, sc_app, params)

    sidechain._multinode_with_callback(params,
                                       callback,
                                       setup_user_accounts=False)


def test_simple_xchain(configs_dirs_dict: Dict[int, str]):
    params = sidechain.Params(configs_dir=configs_dirs_dict[1])

    if err_str := params.check_error():
        eprint(err_str)
        sys.exit(1)

    if params.verbose:
        print("eprint enabled")
    else:
        disable_eprint()

    # Set to true to help debug tests
    test_utils.test_context_verbose_logging = True

    if params.standalone:
        standalone_test(params)
    else:
        multinode_test(params)
