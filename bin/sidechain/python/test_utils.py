import asyncio
import collections
from contextlib import contextmanager
import json
import logging
import pprint
import time
from typing import Callable, Dict, List, Optional

from app import App, balances_dataframe
from common import Account, Asset, XRP, eprint
from command import Subscribe

MC_SUBSCRIBE_QUEUE = []
SC_SUBSCRIBE_QUEUE = []


def _mc_subscribe_callback(v: dict):
    MC_SUBSCRIBE_QUEUE.append(v)
    logging.info(f'mc subscribe_callback:\n{json.dumps(v, indent=1)}')


def _sc_subscribe_callback(v: dict):
    SC_SUBSCRIBE_QUEUE.append(v)
    logging.info(f'sc subscribe_callback:\n{json.dumps(v, indent=1)}')


def mc_connect_subscription(app: App, door_account: Account):
    app(Subscribe(account_history_account=door_account),
        _mc_subscribe_callback)


def sc_connect_subscription(app: App, door_account: Account):
    app(Subscribe(account_history_account=door_account),
        _sc_subscribe_callback)


# This pops elements off the subscribe_queue until the transaction is found
# It mofifies the queue in place.
async def async_wait_for_payment_detect(app: App, subscribe_queue: List[dict],
                                        src: Account, dst: Account,
                                        amt_asset: Asset):
    logging.info(
        f'Wait for payment {src.account_id = } {dst.account_id = } {amt_asset = }'
    )
    n_txns = 10  # keep this many txn in a circular buffer.
    # If the payment is not detected, write them to the log.
    last_n_paytxns = collections.deque(maxlen=n_txns)
    for i in range(30):
        while subscribe_queue:
            d = subscribe_queue.pop(0)
            if 'transaction' not in d:
                continue
            txn = d['transaction']
            if txn['TransactionType'] != 'Payment':
                continue

            txn_asset = Asset(from_rpc_result=txn['Amount'])
            if txn['Account'] == src.account_id and txn[
                    'Destination'] == dst.account_id and txn_asset == amt_asset:
                if d['engine_result_code'] == 0:
                    logging.info(
                        f'Found payment {src.account_id = } {dst.account_id = } {amt_asset = }'
                    )
                    return
                else:
                    logging.error(
                        f'Expected payment failed {src.account_id = } {dst.account_id = } {amt_asset = }'
                    )
                    raise ValueError(
                        f'Expected payment failed {src.account_id = } {dst.account_id = } {amt_asset = }'
                    )
            else:
                last_n_paytxns.append(txn)
        if i > 0 and not (i % 5):
            logging.warning(
                f'Waiting for txn detect {src.account_id = } {dst.account_id = } {amt_asset = }'
            )
        # side chain can send transactions to the main chain, but won't close the ledger
        # We don't know when the transaction will be sent, so may need to close the ledger here
        await app.async_maybe_ledger_accept()
        await asyncio.sleep(2)
    logging.warning(
        f'Last {len(last_n_paytxns)} pay txns while waiting for payment detect'
    )
    for t in last_n_paytxns:
        logging.warning(
            f'Detected pay transaction while waiting for payment: {t}')
    logging.error(
        f'Expected txn detect {src.account_id = } {dst.account_id = } {amt_asset = }'
    )
    raise ValueError(
        f'Expected txn detect {src.account_id = } {dst.account_id = } {amt_asset = }'
    )


def mc_wait_for_payment_detect(app: App, src: Account, dst: Account,
                               amt_asset: Asset):
    logging.info(f'mainchain waiting for payment detect')
    return asyncio.get_event_loop().run_until_complete(
        async_wait_for_payment_detect(app, MC_SUBSCRIBE_QUEUE, src, dst,
                                      amt_asset))


def sc_wait_for_payment_detect(app: App, src: Account, dst: Account,
                               amt_asset: Asset):
    logging.info(f'sidechain waiting for payment detect')
    return asyncio.get_event_loop().run_until_complete(
        async_wait_for_payment_detect(app, SC_SUBSCRIBE_QUEUE, src, dst,
                                      amt_asset))


def wait_for_balance_change(app: App,
                            acc: Account,
                            pre_balance: Asset,
                            expected_diff: Optional[Asset] = None):
    logging.info(
        f'waiting for balance change {acc.account_id = } {pre_balance = } {expected_diff = }'
    )
    for i in range(30):
        new_bal = app.get_balance(acc, pre_balance(0))
        diff = new_bal - pre_balance
        if new_bal != pre_balance:
            logging.info(
                f'Balance changed {acc.account_id = } {pre_balance = } {new_bal = } {diff = } {expected_diff = }'
            )
            if expected_diff is None or diff == expected_diff:
                return
        app.maybe_ledger_accept()
        time.sleep(2)
        if i > 0 and not (i % 5):
            logging.warning(
                f'Waiting for balance to change {acc.account_id = } {pre_balance = }'
            )
    logging.error(
        f'Expected balance to change {acc.account_id = } {pre_balance = } {new_bal = } {diff = } {expected_diff = }'
    )
    raise ValueError(
        f'Expected balance to change {acc.account_id = } {pre_balance = } {new_bal = } {diff = } {expected_diff = }'
    )


def log_chain_state(mc_app, sc_app, log, msg='Chain State'):
    chains = [mc_app, sc_app]
    chain_names = ['mainchain', 'sidechain']
    balances = balances_dataframe(chains, chain_names)
    df_as_str = balances.to_string(float_format=lambda x: f'{x:,.6f}')
    log(f'{msg} Balances: \n{df_as_str}')
    federator_info = sc_app.federator_info()
    log(f'{msg} Federator Info: \n{pprint.pformat(federator_info)}')


# Tests can set this to True to help debug test failures by showing account
# balances in the log before the test runs
test_context_verbose_logging = False


@contextmanager
def test_context(mc_app, sc_app, verbose_logging: Optional[bool] = None):
    '''Write extra context info to the log on test failure'''
    global test_context_verbose_logging
    if verbose_logging is None:
        verbose_logging = test_context_verbose_logging
    try:
        if verbose_logging:
            log_chain_state(mc_app, sc_app, logging.info)
        start_time = time.monotonic()
        yield
    except:
        log_chain_state(mc_app, sc_app, logging.error)
        raise
    finally:
        elapased_time = time.monotonic() - start_time
        logging.info(f'Test elapsed time: {elapased_time}')
    if verbose_logging:
        log_chain_state(mc_app, sc_app, logging.info)
