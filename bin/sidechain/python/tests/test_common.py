import logging
import pprint
import pytest
from multiprocessing import Process, Value
from typing import Callable, Dict
import sys

from app import App
from common import eprint, disable_eprint, XRP
from sidechain import Params
import sidechain
import test_utils
import time


def run(mc_app: App, sc_app: App, params: Params, test_case: Callable[[App, App, Params], None]):
    # process will run while stop token is non-zero
    stop_token = Value('i', 1)
    p = None
    if mc_app.standalone:
        p = Process(target=sidechain.close_mainchain_ledgers,
                    args=(stop_token, params))
        p.start()
    try:
        while 0:
            federator_info = sc_app.federator_info()
            should_loop = False
            for v in federator_info.values():
                for c in ['mainchain', 'sidechain']:
                    state = v['info'][c]['listener_info']['state']
                    logging.error(f'XYZZY: {state = }')
                    if state != 'normal':
                        should_loop = True
            if not should_loop:
                break
            time.sleep(1)
        test_case(mc_app, sc_app, params)
    finally:
        if p:
            stop_token.value = 0
            p.join()
        sidechain._convert_log_files_to_json(
            mc_app.get_configs() + sc_app.get_configs(), 'final.json')


def standalone_test(params: Params, test_case: Callable[[App, App, Params], None]):
    def callback(mc_app: App, sc_app: App):
        run(mc_app, sc_app, params, test_case)

    sidechain._standalone_with_callback(params,
                                        callback,
                                        setup_user_accounts=False)


def multinode_test(params: Params, test_case: Callable[[App, App, Params], None]):
    def callback(mc_app: App, sc_app: App):
        run(mc_app, sc_app, params, test_case)

    sidechain._multinode_with_callback(params,
                                       callback,
                                       setup_user_accounts=False)


def test_start(configs_dirs_dict: Dict[int, str],
               test_case: Callable[[App, App, Params], None]):
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
        standalone_test(params, test_case)
    else:
        multinode_test(params, test_case)
