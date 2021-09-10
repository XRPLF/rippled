import datetime
import json
from typing import Dict, List, Optional, Union

from command import Command
from common import Account, Asset, Path, PathList, to_rippled_epoch


class Transaction(Command):
    '''Interface for all transactions'''
    def __init__(
        self,
        *,
        account: Account,
        flags: Optional[int] = None,
        fee: Optional[Union[Asset, int]] = None,
        sequence: Optional[int] = None,
        account_txn_id: Optional[str] = None,
        last_ledger_sequence: Optional[int] = None,
        src_tag: Optional[int] = None,
        memos: Optional[List[Dict[str, dict]]] = None,
    ):
        super().__init__()
        self.account = account
        # set even if None
        self.flags = flags
        self.fee = fee
        self.sequence = sequence
        self.account_txn_id = account_txn_id
        self.last_ledger_sequence = last_ledger_sequence
        self.src_tag = src_tag
        self.memos = memos

    def cmd_name(self) -> str:
        return 'submit'

    def set_seq_and_fee(self, seq: int, fee: Union[Asset, int]):
        self.sequence = seq
        self.fee = fee

    def to_cmd_obj(self) -> dict:
        txn = {
            'Account': self.account.account_id,
        }
        if self.flags is not None:
            txn['Flags'] = flags
        if self.fee is not None:
            if isinstance(self.fee, int):
                txn['Fee'] = f'{self.fee}'  # must be a string
            else:
                txn['Fee'] = self.fee.to_cmd_obj()
        if self.sequence is not None:
            txn['Sequence'] = self.sequence
        if self.account_txn_id is not None:
            txn['AccountTxnID'] = self.account_txn_id
        if self.last_ledger_sequence is not None:
            txn['LastLedgerSequence'] = self.last_ledger_sequence
        if self.src_tag is not None:
            txn['SourceTag'] = self.src_tag
        if self.memos is not None:
            txn['Memos'] = self.memos
        return txn


class Payment(Transaction):
    '''A payment transaction'''
    def __init__(self,
                 *,
                 dst: Account,
                 amt: Asset,
                 send_max: Optional[Asset] = None,
                 paths: Optional[PathList] = None,
                 dst_tag: Optional[int] = None,
                 deliver_min: Optional[Asset] = None,
                 **rest):
        super().__init__(**rest)
        self.dst = dst
        self.amt = amt
        self.send_max = send_max
        if paths is not None and isinstance(paths, Path):
            # allow paths = Path([...]) special case
            self.paths = PathList([paths])
        else:
            self.paths = paths
        self.dst_tag = dst_tag
        self.deliver_min = deliver_min

    def set_partial_payment(self, value: bool = True):
        '''Set or clear the partial payment flag'''
        self._set_flag(0x0002_0000, value)

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        txn = super().to_cmd_obj()
        txn = {
            **txn,
            'TransactionType': 'Payment',
            'Destination': self.dst.account_id,
            'Amount': self.amt.to_cmd_obj(),
        }
        if self.paths is not None:
            txn['Paths'] = self.paths.to_cmd_obj()
        if self.send_max is not None:
            txn['SendMax'] = self.send_max.to_cmd_obj()
        if self.dst_tag is not None:
            txn['DestinationTag'] = self.dst_tag
        if self.deliver_min is not None:
            txn['DeliverMin'] = self.deliver_min
        return txn


class Trust(Transaction):
    '''A trust set transaction'''
    def __init__(self,
                 *,
                 limit_amt: Optional[Asset] = None,
                 qin: Optional[int] = None,
                 qout: Optional[int] = None,
                 **rest):
        super().__init__(**rest)
        self.limit_amt = limit_amt
        self.qin = qin
        self.qout = qout

    def set_auth(self):
        '''Set the auth flag (cannot be cleared)'''
        self._set_flag(0x00010000)
        return self

    def set_no_ripple(self, value: bool = True):
        '''Set or clear the noRipple flag'''
        self._set_flag(0x0002_0000, value)
        self._set_flag(0x0004_0000, not value)
        return self

    def set_freeze(self, value: bool = True):
        '''Set or clear the freeze flag'''
        self._set_flag(0x0020_0000, value)
        self._set_flag(0x0040_0000, not value)
        return self

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        result = super().to_cmd_obj()
        result = {
            **result,
            'TransactionType': 'TrustSet',
            'LimitAmount': self.limit_amt.to_cmd_obj(),
        }
        if self.qin is not None:
            result['QualityIn'] = self.qin
        if self.qout is not None:
            result['QualityOut'] = self.qout
        return result


class SetRegularKey(Transaction):
    '''A SetRegularKey transaction'''
    def __init__(self, *, key: str, **rest):
        super().__init__(**rest)
        self.key = key

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        result = super().to_cmd_obj()
        result = {
            **result,
            'TransactionType': 'SetRegularKey',
            'RegularKey': self.key,
        }
        return result


class SignerListSet(Transaction):
    '''A SignerListSet transaction'''
    def __init__(self,
                 *,
                 keys: List[str],
                 weights: Optional[List[int]] = None,
                 quorum: int,
                 **rest):
        super().__init__(**rest)
        self.keys = keys
        self.quorum = quorum
        if weights:
            if len(weights) != len(keys):
                raise ValueError(
                    f'SignerSetList number of weights must equal number of keys (or be empty). Weights: {weights} Keys: {keys}'
                )
            self.weights = weights
        else:
            self.weights = [1] * len(keys)

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        result = super().to_cmd_obj()
        result = {
            **result,
            'TransactionType': 'SignerListSet',
            'SignerQuorum': self.quorum,
        }
        entries = []
        for k, w in zip(self.keys, self.weights):
            entries.append({'SignerEntry': {'Account': k, 'SignerWeight': w}})
        result['SignerEntries'] = entries
        return result


class AccountSet(Transaction):
    '''An account set transaction'''
    def __init__(self, account: Account, **rest):
        super().__init__(account=account, **rest)
        self.clear_flag = None
        self.set_flag = None
        self.transfer_rate = None
        self.tick_size = None

    def _set_account_flag(self, flag_id: int, value):
        if value:
            self.set_flag = flag_id
        else:
            self.clear_flag = flag_id
        return self

    def set_account_txn_id(self, value: bool = True):
        '''Set or clear the asfAccountTxnID flag'''
        return self._set_account_flag(5, value)

    def set_default_ripple(self, value: bool = True):
        '''Set or clear the asfDefaultRipple flag'''
        return self._set_account_flag(8, value)

    def set_deposit_auth(self, value: bool = True):
        '''Set or clear the asfDepositAuth flag'''
        return self._set_account_flag(9, value)

    def set_disable_master(self, value: bool = True):
        '''Set or clear the asfDisableMaster flag'''
        return self._set_account_flag(4, value)

    def set_disallow_xrp(self, value: bool = True):
        '''Set or clear the asfDisallowXRP flag'''
        return self._set_account_flag(3, value)

    def set_global_freeze(self, value: bool = True):
        '''Set or clear the asfGlobalFreeze flag'''
        return self._set_account_flag(7, value)

    def set_no_freeze(self, value: bool = True):
        '''Set or clear the asfNoFreeze flag'''
        return self._set_account_flag(6, value)

    def set_require_auth(self, value: bool = True):
        '''Set or clear the asfRequireAuth flag'''
        return self._set_account_flag(2, value)

    def set_require_dest(self, value: bool = True):
        '''Set or clear the asfRequireDest flag'''
        return self._set_account_flag(1, value)

    def set_transfer_rate(self, value: int):
        '''Set the fee to change when users transfer this account's issued currencies'''
        self.transfer_rate = value
        return self

    def set_tick_size(self, value: int):
        '''Tick size to use for offers involving a currency issued by this address'''
        self.tick_size = value
        return self

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        result = super().to_cmd_obj()
        result = {
            **result,
            'TransactionType': 'AccountSet',
        }
        if self.clear_flag is not None:
            result['ClearFlag'] = self.clear_flag
        if self.set_flag is not None:
            result['SetFlag'] = self.set_flag
        if self.transfer_rate is not None:
            result['TransferRate'] = self.transfer_rate
        if self.tick_size is not None:
            result['TickSize'] = self.tick_size
        return result


class Offer(Transaction):
    '''An offer transaction'''
    def __init__(self,
                 *,
                 taker_pays: Asset,
                 taker_gets: Asset,
                 expiration: Optional[int] = None,
                 offer_sequence: Optional[int] = None,
                 **rest):
        super().__init__(**rest)
        self.taker_pays = taker_pays
        self.taker_gets = taker_gets
        self.expiration = expiration
        self.offer_sequence = offer_sequence

    def set_passive(self, value: bool = True):
        return self._set_flag(0x0001_0000, value)

    def set_immediate_or_cancel(self, value: bool = True):
        return self._set_flag(0x0002_0000, value)

    def set_fill_or_kill(self, value: bool = True):
        return self._set_flag(0x0004_0000, value)

    def set_sell(self, value: bool = True):
        return self._set_flag(0x0008_0000, value)

    def to_cmd_obj(self) -> dict:
        txn = super().to_cmd_obj()
        txn = {
            **txn,
            'TransactionType': 'OfferCreate',
            'TakerPays': self.taker_pays.to_cmd_obj(),
            'TakerGets': self.taker_gets.to_cmd_obj(),
        }
        if self.expiration is not None:
            txn['Expiration'] = self.expiration
        if self.offer_sequence is not None:
            txn['OfferSequence'] = self.offer_sequence
        return txn


class Ticket(Transaction):
    '''A ticket create transaction'''
    def __init__(self, *, count: int = 1, **rest):
        super().__init__(**rest)
        self.count = count

    def to_cmd_obj(self) -> dict:
        txn = super().to_cmd_obj()
        txn = {
            **txn,
            'TransactionType': 'TicketCreate',
            'TicketCount': self.count,
        }
        return txn


class SetHook(Transaction):
    '''A SetHook transaction for the experimental hook amendment'''
    def __init__(self,
                 *,
                 create_code: str,
                 hook_on: str = '0000000000000000',
                 **rest):
        super().__init__(**rest)
        self.create_code = create_code
        self.hook_on = hook_on

    def to_cmd_obj(self) -> dict:
        txn = super().to_cmd_obj()
        txn = {
            **txn,
            'TransactionType': 'SetHook',
            'CreateCode': self.create_code,
            'HookOn': self.hook_on,
        }
        return txn
