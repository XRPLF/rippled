import json
from typing import List, Optional, Union

from common import Account, Asset


class Command:
    '''Interface for all commands sent to the server'''

    # command id useful for websocket messages
    next_cmd_id = 1

    def __init__(self):
        self.cmd_id = Command.next_cmd_id
        Command.next_cmd_id += 1

    def cmd_name(self) -> str:
        '''Return the command name for use in a command line'''
        assert False
        return ''

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name, json.dumps(self.to_cmd_obj())]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = self.to_cmd_obj()
        return self.add_websocket_fields(result)

    def to_cmd_obj(self) -> dict:
        '''Return an object suitalbe for use in a command (input to json.dumps or similar)'''
        assert False
        return {}

    def add_websocket_fields(self, cmd_dict: dict) -> dict:
        cmd_dict['id'] = self.cmd_id
        cmd_dict['command'] = self.cmd_name()
        return cmd_dict

    def _set_flag(self, flag_bit: int, value: bool = True):
        '''Set or clear the flag bit'''
        if value:
            self.flags |= flag_bit
        else:
            self.flags &= ~flag_bit
        return self


class SubscriptionCommand(Command):
    def __init__(self):
        super().__init__()


class PathFind(Command):
    '''Rippled ripple_path_find command'''
    def __init__(self,
                 *,
                 src: Account,
                 dst: Account,
                 amt: Asset,
                 send_max: Optional[Asset] = None,
                 src_currencies: Optional[List[Asset]] = None,
                 ledger_hash: Optional[str] = None,
                 ledger_index: Optional[Union[int, str]] = None):
        super().__init__()
        self.src = src
        self.dst = dst
        self.amt = amt
        self.send_max = send_max
        self.src_currencies = src_currencies
        self.ledger_hash = ledger_hash
        self.ledger_index = ledger_index

    def cmd_name(self) -> str:
        return 'ripple_path_find'

    def add_websocket_fields(self, cmd_dict: dict) -> dict:
        cmd_dict = super().add_websocket_fields(cmd_dict)
        cmd_dict['subcommand'] = 'create'
        return cmd_dict

    def to_cmd_obj(self) -> dict:
        '''convert to transaction form (suitable for using json.dumps or similar)'''
        cmd = {
            'source_account': self.src.account_id,
            'destination_account': self.dst.account_id,
            'destination_amount': self.amt.to_cmd_obj()
        }
        if self.send_max is not None:
            cmd['send_max'] = self.send_max.to_cmd_obj()
        if self.ledger_hash is not None:
            cmd['ledger_hash'] = self.ledger_hash
        if self.ledger_index is not None:
            cmd['ledger_index'] = self.ledger_index
        if self.src_currencies:
            c = []
            for sc in self.src_currencies:
                d = {'currency': sc.currency, 'issuer': sc.issuer.account_id}
                c.append(d)
            cmd['source_currencies'] = c
        return cmd


class Sign(Command):
    '''Rippled sign command'''
    def __init__(self, secret: str, tx: dict):
        super().__init__()
        self.tx = tx
        self.secret = secret

    def cmd_name(self) -> str:
        return 'sign'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name(), self.secret, f'{json.dumps(self.tx)}']

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'secret': self.secret, 'tx_json': self.tx}
        return self.add_websocket_fields(result)


class Submit(Command):
    '''Rippled submit command'''
    def __init__(self, tx_blob: str):
        super().__init__()
        self.tx_blob = tx_blob

    def cmd_name(self) -> str:
        return 'submit'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name(), self.tx_blob]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'tx_blob': self.tx_blob}
        return self.add_websocket_fields(result)


class LedgerAccept(Command):
    '''Rippled ledger_accept command'''
    def __init__(self):
        super().__init__()

    def cmd_name(self) -> str:
        return 'ledger_accept'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        return self.add_websocket_fields(result)


class Stop(Command):
    '''Rippled stop command'''
    def __init__(self):
        super().__init__()

    def cmd_name(self) -> str:
        return 'stop'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        return self.add_websocket_fields(result)


class LogLevel(Command):
    '''Rippled log_level command'''
    def __init__(self, severity: str, *, partition: Optional[str] = None):
        super().__init__()
        self.severity = severity
        self.partition = partition

    def cmd_name(self) -> str:
        return 'log_level'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        if self.partition is not None:
            return [self.cmd_name(), self.partition, self.severity]
        return [self.cmd_name(), self.severity]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'severity': self.severity}
        if self.partition is not None:
            result['partition'] = self.partition
        return self.add_websocket_fields(result)


class WalletPropose(Command):
    '''Rippled wallet_propose command'''
    def __init__(self,
                 *,
                 passphrase: Optional[str] = None,
                 seed: Optional[str] = None,
                 seed_hex: Optional[str] = None,
                 key_type: Optional[str] = None):
        super().__init__()
        self.passphrase = passphrase
        self.seed = seed
        self.seed_hex = seed_hex
        self.key_type = key_type

    def cmd_name(self) -> str:
        return 'wallet_propose'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        assert not self.seed and not self.seed_hex and (
            not self.key_type or self.key_type == 'secp256k1')
        if self.passphrase:
            return [self.cmd_name(), self.passphrase]
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        if self.seed is not None:
            result['seed'] = self.seed
        if self.seed_hex is not None:
            result['seed_hex'] = self.seed_hex
        if self.passphrase is not None:
            result['passphrase'] = self.passphrase
        if self.key_type is not None:
            result['key_type'] = self.key_type
        return self.add_websocket_fields(result)


class ValidationCreate(Command):
    '''Rippled validation_create command'''
    def __init__(self, *, secret: Optional[str] = None):
        super().__init__()
        self.secret = secret

    def cmd_name(self) -> str:
        return 'validation_create'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        if self.secret:
            return [self.cmd_name(), self.secret]
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        if self.secret is not None:
            result['secret'] = self.secret
        return self.add_websocket_fields(result)


class AccountInfo(Command):
    '''Rippled account_info command'''
    def __init__(self,
                 account: Account,
                 *,
                 strict: Optional[bool] = None,
                 ledger_hash: Optional[str] = None,
                 ledger_index: Optional[Union[str, int]] = None,
                 queue: Optional[bool] = None,
                 signers_list: Optional[bool] = None):
        super().__init__()
        self.account = account
        self.strict = strict
        self.ledger_hash = ledger_hash
        self.ledger_index = ledger_index
        self.queue = queue
        self.signers_list = signers_list
        assert not ((ledger_hash is not None) and (ledger_index is not None))

    def cmd_name(self) -> str:
        return 'account_info'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        result = [self.cmd_name(), self.account.account_id]
        if self.ledger_index is not None:
            result.append(self.ledger_index)
        if self.ledger_hash is not None:
            result.append(self.ledger_hash)
        if self.strict is not None:
            result.append(self.strict)
        return result

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'account': self.account.account_id}
        if self.ledger_index is not None:
            result['ledger_index'] = self.ledger_index
        if self.ledger_hash is not None:
            result['ledger_hash'] = self.ledger_hash
        if self.strict is not None:
            result['strict'] = self.strict
        if self.queue is not None:
            result['queue'] = self.queue
        return self.add_websocket_fields(result)


class AccountLines(Command):
    '''Rippled account_lines command'''
    def __init__(self,
                 account: Account,
                 *,
                 peer: Optional[Account] = None,
                 ledger_hash: Optional[str] = None,
                 ledger_index: Optional[Union[str, int]] = None,
                 limit: Optional[int] = None,
                 marker=None):
        super().__init__()
        self.account = account
        self.peer = peer
        self.ledger_hash = ledger_hash
        self.ledger_index = ledger_index
        self.limit = limit
        self.marker = marker
        assert not ((ledger_hash is not None) and (ledger_index is not None))

    def cmd_name(self) -> str:
        return 'account_lines'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        assert sum(x is None for x in [
            self.ledger_index, self.ledger_hash, self.limit, self.marker
        ]) == 4
        result = [self.cmd_name(), self.account.account_id]
        if self.peer is not None:
            result.append(self.peer)
        return result

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'account': self.account.account_id}
        if self.peer is not None:
            result['peer'] = self.peer
        if self.ledger_index is not None:
            result['ledger_index'] = self.ledger_index
        if self.ledger_hash is not None:
            result['ledger_hash'] = self.ledger_hash
        if self.limit is not None:
            result['limit'] = self.limit
        if self.marker is not None:
            result['marker'] = self.marker
        return self.add_websocket_fields(result)


class AccountTx(Command):
    def __init__(self,
                 account: Account,
                 *,
                 limit: Optional[int] = None,
                 marker=None):
        super().__init__()
        self.account = account
        self.limit = limit
        self.marker = marker

    def cmd_name(self) -> str:
        return 'account_tx'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        result = [self.cmd_name(), self.account.account_id]
        return result

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {'account': self.account.account_id}
        if self.limit is not None:
            result['limit'] = self.limit
        if self.marker is not None:
            result['marker'] = self.marker
        return self.add_websocket_fields(result)


class BookOffers(Command):
    '''Rippled book_offers command'''
    def __init__(self,
                 taker_pays: Asset,
                 taker_gets: Asset,
                 *,
                 taker: Optional[Account] = None,
                 ledger_hash: Optional[str] = None,
                 ledger_index: Optional[Union[str, int]] = None,
                 limit: Optional[int] = None,
                 marker=None):
        super().__init__()
        self.taker_pays = taker_pays
        self.taker_gets = taker_gets
        self.taker = taker
        self.ledger_hash = ledger_hash
        self.ledger_index = ledger_index
        self.limit = limit
        self.marker = marker
        assert not ((ledger_hash is not None) and (ledger_index is not None))

    def cmd_name(self) -> str:
        return 'book_offers'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        assert sum(x is None for x in [
            self.ledger_index, self.ledger_hash, self.limit, self.marker
        ]) == 4
        return [
            self.cmd_name(),
            self.taker_pays.cmd_str(),
            self.taker_gets.cmd_str()
        ]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {
            'taker_pays': self.taker_pays.to_cmd_obj(),
            'taker_gets': self.taker_gets.to_cmd_obj()
        }
        if self.taker is not None:
            result['taker'] = self.taker.account_id
        if self.ledger_index is not None:
            result['ledger_index'] = self.ledger_index
        if self.ledger_hash is not None:
            result['ledger_hash'] = self.ledger_hash
        if self.limit is not None:
            result['limit'] = self.limit
        if self.marker is not None:
            result['marker'] = self.marker
        return self.add_websocket_fields(result)


class BookSubscription:
    '''Spec for a book in a subscribe command'''
    def __init__(self,
                 taker_pays: Asset,
                 taker_gets: Asset,
                 *,
                 taker: Optional[Account] = None,
                 snapshot: Optional[bool] = None,
                 both: Optional[bool] = None):
        self.taker_pays = taker_pays
        self.taker_gets = taker_gets
        self.taker = taker
        self.snapshot = snapshot
        self.both = both

    def to_cmd_obj(self) -> dict:
        '''Return an object suitalbe for use in a command'''
        result = {
            'taker_pays': self.taker_pays.to_cmd_obj(),
            'taker_gets': self.taker_gets.to_cmd_obj()
        }
        if self.taker is not None:
            result['taker'] = self.taker.account_id
        if self.snapshot is not None:
            result['snapshot'] = self.snapshot
        if self.both is not None:
            result['both'] = self.both
        return result


class ServerInfo(Command):
    '''Rippled server_info command'''
    def __init__(self):
        super().__init__()

    def cmd_name(self) -> str:
        return 'server_info'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        return self.add_websocket_fields(result)


class FederatorInfo(Command):
    '''Rippled federator_info command'''
    def __init__(self):
        super().__init__()

    def cmd_name(self) -> str:
        return 'federator_info'

    def get_command_line_list(self) -> List[str]:
        '''Return a list of strings suitable for a command line command for a rippled server'''
        return [self.cmd_name()]

    def get_websocket_dict(self) -> dict:
        '''Return a dictionary suitable for converting to json and sending to a rippled server using a websocket'''
        result = {}
        return self.add_websocket_fields(result)


class Subscribe(SubscriptionCommand):
    '''The subscribe method requests periodic notifications from the server
    when certain events happen. See: https://developers.ripple.com/subscribe.html'''
    def __init__(
            self,
            *,
            streams: Optional[List[str]] = None,
            accounts: Optional[List[Account]] = None,
            accounts_proposed: Optional[List[Account]] = None,
            account_history_account: Optional[Account] = None,
            books: Optional[
                List[BookSubscription]] = None,  # taker_pays, taker_gets
            url: Optional[str] = None,
            url_username: Optional[str] = None,
            url_password: Optional[str] = None):
        super().__init__()
        self.streams = streams
        self.accounts = accounts
        self.account_history_account = account_history_account
        self.accounts_proposed = accounts_proposed
        self.books = books
        self.url = url
        self.url_username = url_username
        self.url_password = url_password
        self.websocket = None

    def cmd_name(self) -> str:
        if self.websocket:
            return 'unsubscribe'
        return 'subscribe'

    def to_cmd_obj(self) -> dict:
        d = {}
        if self.streams is not None:
            d['streams'] = self.streams
        if self.accounts is not None:
            d['accounts'] = [a.account_id for a in self.accounts]
        if self.account_history_account is not None:
            d['account_history_tx_stream'] = {
                'account': self.account_history_account.account_id
            }
        if self.accounts_proposed is not None:
            d['accounts_proposed'] = [
                a.account_id for a in self.accounts_proposed
            ]
        if self.books is not None:
            d['books'] = [b.to_cmd_obj() for b in self.books]
        if self.url is not None:
            d['url'] = self.url
        if self.url_username is not None:
            d['url_username'] = self.url_username
        if self.url_password is not None:
            d['url_password'] = self.url_password
        return d
