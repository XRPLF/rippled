#!/usr/bin/env python3

import argparse
from collections import defaultdict
import datetime
import json
import numpy as np
import os
import pandas as pd
import string
import sys
from typing import Dict, Set

from common import eprint
import log_analyzer


def _has_256bit_hex_field_other(data, result: Set[str]):
    return


_has_256bit_hex_field_overloads = defaultdict(
    lambda: _has_256bit_hex_field_other)


def _has_256bit_hex_field_str(data: str, result: Set[str]):
    if len(data) != 64:
        return
    for c in data:
        o = ord(c.upper())
        if ord('A') <= o <= ord('F'):
            continue
        if ord('0') <= o <= ord('9'):
            continue
        return
    result.add(data)


_has_256bit_hex_field_overloads[str] = _has_256bit_hex_field_str


def _has_256bit_hex_field_dict(data: dict, result: Set[str]):
    for k, v in data.items():
        if k in [
                "meta", "index", "LedgerIndex", "ledger_index", "ledger_hash",
                "SigningPubKey", "suppression"
        ]:
            continue
        _has_256bit_hex_field_overloads[type(v)](v, result)


_has_256bit_hex_field_overloads[dict] = _has_256bit_hex_field_dict


def _has_256bit_hex_field_list(data: list, result: Set[str]):
    for v in data:
        _has_256bit_hex_field_overloads[type(v)](v, result)


_has_256bit_hex_field_overloads[list] = _has_256bit_hex_field_list


def has_256bit_hex_field(data: dict) -> Set[str]:
    '''
    Find all the fields that are strings 64 chars long with only hex digits
    This is useful when grouping transactions by hex
    '''
    result = set()
    _has_256bit_hex_field_dict(data, result)
    return result


def group_by_txn(data: dict) -> dict:
    '''
    return a dictionary where the key is the transaction hash, the value is another dictionary.
    The second dictionary the key is the server id, and the values are a list of log items
    '''
    def _make_default():
        return defaultdict(lambda: list())

    result = defaultdict(_make_default)
    for server_id, log_list in data.items():
        for log_item in log_list:
            if txn_hashes := has_256bit_hex_field(log_item):
                for h in txn_hashes:
                    result[h][server_id].append(log_item)
    return result


def _rekey_dict_by_txn_date(hash_to_timestamp: dict,
                            grouped_by_txn: dict) -> dict:
    '''
    hash_to_timestamp is a dictionary with a key of the txn hash and a value of the timestamp.
    grouped_by_txn is a dictionary with a key of the txn and an unspecified value.
    the keys in hash_to_timestamp are a superset of the keys in grouped_by_txn
    This function returns a new grouped_by_txn dictionary with the transactions sorted by date.
    '''
    known_txns = [
        k for k, v in sorted(hash_to_timestamp.items(), key=lambda x: x[1])
    ]
    result = {}
    for k, v in grouped_by_txn.items():
        if k not in known_txns:
            result[k] = v
    for h in known_txns:
        result[h] = grouped_by_txn[h]
    return result


def _to_timestamp(str_time: str) -> datetime.datetime:
    return datetime.datetime.strptime(
        str_time.split('.')[0], "%Y-%b-%d %H:%M:%S")


class Report:
    def __init__(self, in_dir, out_dir):
        self.in_dir = in_dir
        self.out_dir = out_dir

        self.combined_logs_file_name = f'{self.out_dir}/combined_logs.json'
        self.grouped_by_txn_file_name = f'{self.out_dir}/grouped_by_txn.json'
        self.counts_by_txn_and_server_file_name = f'{self.out_dir}/counts_by_txn_and_server.org'
        self.data = None  # combined logs

        # grouped_by_txn is a dictionary where the key is the server id. mainchain servers
        # have a key of `mainchain_#` and sidechain servers have a key of
        # `sidechain_#`, where `#` is a number.
        self.grouped_by_txn = None

        if not os.path.isdir(in_dir):
            eprint(f'The input {self.in_dir} must be an existing directory')
            sys.exit(1)

        if os.path.exists(self.out_dir):
            if not os.path.isdir(self.out_dir):
                eprint(
                    f'The output: {self.out_dir} exists and is not a directory'
                )
                sys.exit(1)
        else:
            os.makedirs(self.out_dir)

        self.combine_logs()
        with open(self.combined_logs_file_name) as f:
            self.data = json.load(f)
        self.grouped_by_txn = group_by_txn(self.data)

        # counts_by_txn_and_server is a dictionary where the key is the txn_hash
        # and the value is a pandas df with a row for every server and a column for every message
        # the value is a count of how many times that message appears for that server.
        counts_by_txn_and_server = {}
        # dict where the key is a transaction hash and the value is the transaction
        hash_to_txn = {}
        # dict where the key is a transaction hash and the value is earliest timestamp in a log file
        hash_to_timestamp = {}
        for txn_hash, server_dict in self.grouped_by_txn.items():
            message_set = set()
            # message list is ordered by when it appears in the log
            message_list = []
            for server_id, messages in server_dict.items():
                for m in messages:
                    try:
                        d = m['data']
                        if 'msg' in d and 'transaction' in d['msg']:
                            t = d['msg']['transaction']
                        elif 'tx_json' in d:
                            t = d['tx_json']
                        if t['hash'] == txn_hash:
                            hash_to_txn[txn_hash] = t
                    except:
                        pass
                    msg = m['msg']
                    t = _to_timestamp(m['t'])
                    if txn_hash not in hash_to_timestamp:
                        hash_to_timestamp[txn_hash] = t
                    elif hash_to_timestamp[txn_hash] > t:
                        hash_to_timestamp[txn_hash] = t
                    if msg not in message_set:
                        message_set.add(msg)
                        message_list.append(msg)
            df = pd.DataFrame(0,
                              index=server_dict.keys(),
                              columns=message_list)
            for server_id, messages in server_dict.items():
                for m in messages:
                    df[m['msg']][server_id] += 1
            counts_by_txn_and_server[txn_hash] = df

        # sort the transactions by timestamp, but the txns with unknown timestamp at the beginning
        self.grouped_by_txn = _rekey_dict_by_txn_date(hash_to_timestamp,
                                                      self.grouped_by_txn)
        counts_by_txn_and_server = _rekey_dict_by_txn_date(
            hash_to_timestamp, counts_by_txn_and_server)

        with open(self.grouped_by_txn_file_name, 'w') as out:
            print(json.dumps(self.grouped_by_txn, indent=1), file=out)

        with open(self.counts_by_txn_and_server_file_name, 'w') as out:
            for txn_hash, df in counts_by_txn_and_server.items():
                print(f'\n\n* Txn: {txn_hash}', file=out)
                if txn_hash in hash_to_txn:
                    print(json.dumps(hash_to_txn[txn_hash], indent=1),
                          file=out)
                rename_dict = {}
                for column, renamed_column in zip(df.columns.array,
                                                  string.ascii_uppercase):
                    print(f'{renamed_column} = {column}', file=out)
                    rename_dict[column] = renamed_column
                df.rename(columns=rename_dict, inplace=True)
                print(f'\n{df}', file=out)

    def combine_logs(self):
        try:
            with open(self.combined_logs_file_name, "w") as out:
                log_analyzer.convert_all(args.input, out, pure_json=True)
        except Exception as e:
            eprint(f'Excption: {e}')
            raise e


def main(input_dir_name: str, output_dir_name: str):
    r = Report(input_dir_name, output_dir_name)

    # Values are a list of log lines formatted as json. There are five fields:
    # `t` is the timestamp.
    # `m` is the module.
    # `l` is the log level.
    # `msg` is the message.
    # `data` is the data.
    # For example:
    #
    # {
    #  "t": "2021-Oct-08 21:33:41.731371562 UTC",
    #  "m": "SidechainFederator",
    #  "l": "TRC",
    #  "msg": "no last xchain txn with result",
    #  "data": {
    #   "needsOtherChainLastXChainTxn": true,
    #   "isMainchain": false,
    #   "jlogId": 121
    #  }
    # },


# Lifecycle of a transaction
# For each federator record:
# Transaction detected: amount, seq, destination, chain, hash
# Signature received: hash, seq
# Signature sent: hash, seq, federator dst
# Transaction submitted
# Result received, and detect if error
# Detect any field that doesn't match

# Lifecycle of initialization

# Chain listener messages


def parse_args():
    parser = argparse.ArgumentParser(description=(
        'python script to generate a log report from a sidechain config directory structure containing the logs'
    ))

    parser.add_argument(
        '--input',
        '-i',
        help=('directory with sidechain config directory structure'),
    )

    parser.add_argument(
        '--output',
        '-o',
        help=('output directory for report files'),
    )

    return parser.parse_known_args()[0]


if __name__ == '__main__':
    try:
        args = parse_args()
        main(args.input, args.output)
    except Exception as e:
        eprint(f'Excption: {e}')
        raise e
