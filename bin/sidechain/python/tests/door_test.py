from typing import Dict
from app import App
from common import XRP
from sidechain import Params
import sidechain
import test_utils
import time
from transaction import Payment
import test_common

batch_test_num_accounts = 200


def door_test(mc_app: App, sc_app: App, params: Params):
    # setup, create accounts on both chains
    for i in range(batch_test_num_accounts):
        name = "m_" + str(i)
        account_main = mc_app.create_account(name)
        name = "s_" + str(i)
        account_side = sc_app.create_account(name)
        mc_app(Payment(account=params.genesis_account, dst=account_main, amt=XRP(20_000)))
        mc_app.maybe_ledger_accept()
    account_main_last = mc_app.account_from_alias("m_" + str(batch_test_num_accounts - 1))
    test_utils.wait_for_balance_change(mc_app, account_main_last, XRP(0), XRP(20_000))

    # test
    to_side_xrp = XRP(1000)
    to_main_xrp = XRP(100)
    last_tx_xrp = XRP(343)
    with test_utils.test_context(mc_app, sc_app, True):
        # send xchain payment to open accounts on sidechain
        for i in range(batch_test_num_accounts):
            name_main = "m_" + str(i)
            account_main = mc_app.account_from_alias(name_main)
            name_side = "s_" + str(i)
            account_side = sc_app.account_from_alias(name_side)
            memos = [{'Memo': {'MemoData': account_side.account_id_str_as_hex()}}]
            mc_app(Payment(account=account_main, dst=params.mc_door_account,
                           amt=to_side_xrp, memos=memos))

        # wait some time for the door to change
        door_closing = False
        door_reopened = False
        for i in range(batch_test_num_accounts * 2 + 40):
            server_index = [0]
            federator_info = sc_app.federator_info(server_index)
            for v in federator_info.values():
                door_status = v['info']['mainchain']['door_status']['status']
                if not door_closing:
                    if door_status != 'open':
                        door_closing = True
                else:
                    if door_status == 'open':
                        door_reopened = True

            if not door_reopened:
                time.sleep(1)
                mc_app.maybe_ledger_accept()
            else:
                break

        if not door_reopened:
            raise ValueError('Expected door status changes did not happen')

        # wait for accounts created on sidechain
        for i in range(batch_test_num_accounts):
            name_side = "s_" + str(i)
            account_side = sc_app.account_from_alias(name_side)
            test_utils.wait_for_balance_change(sc_app, account_side,
                                               XRP(0), to_side_xrp)

        # # try one xchain payment, each direction
        name_main = "m_" + str(0)
        account_main = mc_app.account_from_alias(name_main)
        name_side = "s_" + str(0)
        account_side = sc_app.account_from_alias(name_side)

        pre_bal = mc_app.get_balance(account_main, XRP(0))
        sidechain.side_to_main_transfer(mc_app, sc_app, account_side, account_main,
                                        to_main_xrp, params)
        test_utils.wait_for_balance_change(mc_app, account_main, pre_bal,
                                           to_main_xrp)

        pre_bal = sc_app.get_balance(account_side, XRP(0))
        sidechain.main_to_side_transfer(mc_app, sc_app, account_main, account_side,
                                        last_tx_xrp, params)
        test_utils.wait_for_balance_change(sc_app, account_side, pre_bal,
                                           last_tx_xrp)


def test_door_operations(configs_dirs_dict: Dict[int, str]):
    test_common.test_start(configs_dirs_dict, door_test)
