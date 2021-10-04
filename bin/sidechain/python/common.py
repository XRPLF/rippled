import binascii
import datetime
import logging
from typing import List, Optional, Union
import pandas as pd
import pytz
import sys

EPRINT_ENABLED = True


def disable_eprint():
    global EPRINT_ENABLED
    EPRINT_ENABLED = False


def enable_eprint():
    global EPRINT_ENABLED
    EPRINT_ENABLED = True


def eprint(*args, **kwargs):
    if not EPRINT_ENABLED:
        return
    logging.error(*args)
    print(*args, file=sys.stderr, flush=True, **kwargs)


def to_rippled_epoch(d: datetime.datetime) -> int:
    '''Convert from a datetime to the number of seconds since Jan 1, 2000 (rippled epoch)'''
    start = datetime.datetime(2000, 1, 1, tzinfo=pytz.utc)
    return int((d - start).total_seconds())


class Account:  # pylint: disable=too-few-public-methods
    '''
    Account in the ripple ledger
    '''
    def __init__(self,
                 *,
                 account_id: Optional[str] = None,
                 nickname: Optional[str] = None,
                 public_key: Optional[str] = None,
                 public_key_hex: Optional[str] = None,
                 secret_key: Optional[str] = None,
                 result_dict: Optional[dict] = None):
        self.account_id = account_id
        self.nickname = nickname
        self.public_key = public_key
        self.public_key_hex = public_key_hex
        self.secret_key = secret_key

        if result_dict is not None:
            self.account_id = result_dict['account_id']
            self.public_key = result_dict['public_key']
            self.public_key_hex = result_dict['public_key_hex']
            self.secret_key = result_dict['master_seed']

    # Accounts are equal if they represent the same account on the ledger
    # I.e. only check the account_id field for equality.
    def __eq__(self, lhs):
        if not isinstance(lhs, self.__class__):
            return False
        return self.account_id == lhs.account_id

    def __ne__(self, lhs):
        return not self.__eq__(lhs)

    def __str__(self) -> str:
        if self.nickname is not None:
            return self.nickname
        return self.account_id

    def alias_or_account_id(self) -> str:
        '''
        return the alias if it exists, otherwise return the id
        '''
        if self.nickname is not None:
            return self.nickname
        return self.account_id

    def account_id_str_as_hex(self) -> str:
        return binascii.hexlify(self.account_id.encode()).decode('utf-8')

    def to_cmd_obj(self) -> dict:
        return {
            'account_id': self.account_id,
            'nickname': self.nickname,
            'public_key': self.public_key,
            'public_key_hex': self.public_key_hex,
            'secret_key': self.secret_key
        }


class Asset:
    '''An XRP or IOU value'''
    def __init__(
        self,
        *,
        value: Union[int, float, None] = None,
        currency: Optional[
            str] = None,  # Will default to 'XRP' if not specified
        issuer: Optional[Account] = None,
        from_asset=None,  # asset is of type Optional[Asset]
        # from_rpc_result is a python object resulting from an rpc command
        from_rpc_result: Optional[Union[dict, str]] = None):

        assert from_asset is None or from_rpc_result is None

        self.value = value
        self.issuer = issuer
        self.currency = currency
        if from_asset is not None:
            if self.value is None:
                self.value = from_asset.value
            if self.issuer is None:
                self.issuer = from_asset.issuer
            if self.currency is None:
                self.currency = from_asset.currency
        if from_rpc_result is not None:
            if isinstance(from_rpc_result, str):
                self.value = int(from_rpc_result)
                self.currency = 'XRP'
            else:
                self.value = from_rpc_result['value']
                self.currency = float(from_rpc_result['currency'])
                self.issuer = Account(account_id=from_rpc_result['issuer'])

        if self.currency is None:
            self.currency = 'XRP'

        if isinstance(self.value, str):
            if self.is_xrp():
                self.value = int(value)
            else:
                self.value = float(value)

    def __call__(self, value: Union[int, float]):
        '''Call operator useful for a terse syntax for assets in tests. I.e. USD(100)'''
        return Asset(value=value, from_asset=self)

    def __add__(self, lhs):
        assert (self.issuer == lhs.issuer and self.currency == lhs.currency)
        return Asset(value=self.value + lhs.value,
                     currency=self.currency,
                     issuer=self.issuer)

    def __sub__(self, lhs):
        assert (self.issuer == lhs.issuer and self.currency == lhs.currency)
        return Asset(value=self.value - lhs.value,
                     currency=self.currency,
                     issuer=self.issuer)

    def __eq__(self, lhs):
        if not isinstance(lhs, self.__class__):
            return False
        return (self.value == lhs.value and self.currency == lhs.currency
                and self.issuer == lhs.issuer)

    def __ne__(self, lhs):
        return not self.__eq__(lhs)

    def __str__(self) -> str:
        value_part = '' if self.value is None else f'{self.value}/'
        issuer_part = '' if self.issuer is None else f'/{self.issuer}'
        return f'{value_part}{self.currency}{issuer_part}'

    def __repr__(self) -> str:
        return self.__str__()

    def is_xrp(self) -> bool:
        ''' return true if the asset represents XRP'''
        return self.currency == 'XRP'

    def cmd_str(self) -> str:
        value_part = '' if self.value is None else f'{self.value}/'
        issuer_part = '' if self.issuer is None else f'/{self.issuer.account_id}'
        return f'{value_part}{self.currency}{issuer_part}'

    def to_cmd_obj(self) -> dict:
        '''Return an object suitalbe for use in a command'''
        if self.currency == 'XRP':
            if self.value is not None:
                return f'{self.value}'  # must be a string
            return {'currency': self.currency}
        result = {'currency': self.currency, 'issuer': self.issuer.account_id}
        if self.value is not None:
            result['value'] = f'{self.value}'  # must be a string
        return result


def XRP(v: Union[int, float]) -> Asset:
    return Asset(value=v * 1_000_000)


def drops(v: int) -> Asset:
    return Asset(value=v)


class Path:
    '''Payment Path'''
    def __init__(self,
                 nodes: Optional[List[Union[Account, Asset]]] = None,
                 *,
                 result_list: Optional[List[dict]] = None):
        assert not (nodes and result_list)
        if result_list is not None:
            self.result_list = result_list
            return
        if nodes is None:
            self.result_list = []
            return
        self.result_list = [
            self._create_account_path_node(n)
            if isinstance(n, Account) else self._create_currency_path_node(n)
            for n in nodes
        ]

    def _create_account_path_node(self, account: Account) -> dict:
        return {
            'account': account.account_id,
            'type': 1,
            'type_hex': '0000000000000001'
        }

    def _create_currency_path_node(self, asset: Asset) -> dict:
        result = {
            'currency': asset.currency,
            'type': 48,
            'type_hex': '0000000000000030'
        }
        if not asset.is_xrp():
            result['issuer'] = asset.issuer.account_id
        return result

    def to_cmd_obj(self) -> list:
        '''Return an object suitalbe for use in a command'''
        return self.result_list


class PathList:
    '''Collection of paths for use in payments'''
    def __init__(self,
                 path_list: Optional[List[Path]] = None,
                 *,
                 result_list: Optional[List[List[dict]]] = None):
        # result_list can be the response from the rippled server
        assert not (path_list and result_list)
        if result_list is not None:
            self.paths = [Path(result_list=l) for l in result_list]
            return
        self.paths = path_list

    def to_cmd_obj(self) -> list:
        '''Return an object suitalbe for use in a command'''
        return [p.to_cmd_obj() for p in self.paths]
