import logging
import pprint
import pytest
from multiprocessing import Process, Value
from typing import Dict
import sys

from app import App
from common import Asset, eprint, disable_eprint, drops, XRP
import interactive
from sidechain import Params
import sidechain
import test_utils
import time
from transaction import Payment, Trust
import tst_common


def simple_xrp_test(mc_app: App, sc_app: App, params: Params):
    alice = mc_app.account_from_alias('alice')
    adam = sc_app.account_from_alias('adam')
    mc_door = mc_app.account_from_alias('door')
    sc_door = sc_app.account_from_alias('door')

    # main to side
    # First txn funds the side chain account
    with test_utils.test_context(mc_app, sc_app):
        to_send_asset = XRP(9999)
        mc_pre_bal = mc_app.get_balance(mc_door, to_send_asset)
        sc_pre_bal = sc_app.get_balance(adam, to_send_asset)
        sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam,
                                        to_send_asset, params)
        test_utils.wait_for_balance_change(mc_app, mc_door, mc_pre_bal,
                                           to_send_asset)
        test_utils.wait_for_balance_change(sc_app, adam, sc_pre_bal,
                                           to_send_asset)

    for i in range(2):
        # even amounts for main to side
        for value in range(20, 30, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = drops(value)
                mc_pre_bal = mc_app.get_balance(mc_door, to_send_asset)
                sc_pre_bal = sc_app.get_balance(adam, to_send_asset)
                sidechain.main_to_side_transfer(mc_app, sc_app, alice, adam,
                                                to_send_asset, params)
                test_utils.wait_for_balance_change(mc_app, mc_door, mc_pre_bal,
                                                   to_send_asset)
                test_utils.wait_for_balance_change(sc_app, adam, sc_pre_bal,
                                                   to_send_asset)

        # side to main
        # odd amounts for side to main
        for value in range(19, 29, 2):
            with test_utils.test_context(mc_app, sc_app):
                to_send_asset = drops(value)
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


def run_all(mc_app: App, sc_app: App, params: Params):
    setup_accounts(mc_app, sc_app, params)
    logging.info(f'mainchain:\n{mc_app.key_manager.to_string()}')
    logging.info(f'sidechain:\n{sc_app.key_manager.to_string()}')
    simple_xrp_test(mc_app, sc_app, params)
    simple_iou_test(mc_app, sc_app, params)


def test_simple_xchain(configs_dirs_dict: Dict[int, str]):
    tst_common.test_start(configs_dirs_dict, run_all)
