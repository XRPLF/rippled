#!/usr/bin/env python3

# Generate rippled config files, each with their own ports, database paths, and validation_seeds.
# There will be configs for shards/no_shards, main/test nets, two config files for each combination
# (so one can run in a dogfood mode while another is tested). To avoid confusion,The directory path
# will be $data_dir/{main | test}.{shard | no_shard}.{dog | test}
# The config file will reside in that directory with the name rippled.cfg
# The validators file will reside in that directory with the name validators.txt
'''
Script to test and debug sidechains.

The rippled exe location can be set through the command line or
the environment variable RIPPLED_MAINCHAIN_EXE

The configs_dir (where the config files will reside) can be set through the command line
or the environment variable RIPPLED_SIDECHAIN_CFG_DIR
'''

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import sys
from typing import Dict, List, Optional, Tuple, Union

from config_file import ConfigFile
from command import ValidationCreate, WalletPropose
from common import Account, Asset, eprint, XRP
from app import App, single_client_app

mainnet_validators = """
[validator_list_sites]
https://vl.ripple.com

[validator_list_keys]
ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734
"""

altnet_validators = """
[validator_list_sites]
https://vl.altnet.rippletest.net

[validator_list_keys]
ED264807102805220DA0F312E71FC2C69E1552C9C5790F6C25E3729DEB573D5860
"""

node_size = 'medium'
default_data_dir = '/home/swd/data/rippled'


@dataclass
class Keypair:
    public_key: str
    secret_key: str
    account_id: Optional[str]


def generate_node_keypairs(n: int, rip: App) -> List[Keypair]:
    '''
    generate keypairs suitable for validator keys
    '''
    result = []
    for i in range(n):
        keys = rip(ValidationCreate())
        result.append(
            Keypair(public_key=keys["validation_public_key"],
                    secret_key=keys["validation_seed"],
                    account_id=None))
    return result


def generate_federator_keypairs(n: int, rip: App) -> List[Keypair]:
    '''
    generate keypairs suitable for federator keys
    '''
    result = []
    for i in range(n):
        keys = rip(WalletPropose(key_type='ed25519'))
        result.append(
            Keypair(public_key=keys["public_key"],
                    secret_key=keys["master_seed"],
                    account_id=keys["account_id"]))
    return result


class Ports:
    '''
    Port numbers for various services.
    Port numbers differ by cfg_index so different configs can run
    at the same time without interfering with each other.
    '''
    peer_port_base = 51235
    http_admin_port_base = 5005
    ws_public_port_base = 6005

    def __init__(self, cfg_index: int):
        self.peer_port = Ports.peer_port_base + cfg_index
        self.http_admin_port = Ports.http_admin_port_base + cfg_index
        self.ws_public_port = Ports.ws_public_port_base + (2 * cfg_index)
        # note admin port uses public port base
        self.ws_admin_port = Ports.ws_public_port_base + (2 * cfg_index) + 1


class Network:
    def __init__(self, num_nodes: int, num_validators: int,
                 start_cfg_index: int, rip: App):
        self.validator_keypairs = generate_node_keypairs(num_validators, rip)
        self.ports = [Ports(start_cfg_index + i) for i in range(num_nodes)]


class SidechainNetwork(Network):
    def __init__(self, num_nodes: int, num_federators: int,
                 num_validators: int, start_cfg_index: int, rip: App):
        super().__init__(num_nodes, num_validators, start_cfg_index, rip)
        self.federator_keypairs = generate_federator_keypairs(
            num_federators, rip)
        self.main_account = rip(WalletPropose(key_type='secp256k1'))


class XChainAsset:
    def __init__(self, main_asset: Asset, side_asset: Asset,
                 main_value: Union[int, float], side_value: Union[int, float],
                 main_refund_penalty: Union[int, float],
                 side_refund_penalty: Union[int, float]):
        self.main_asset = main_asset(main_value)
        self.side_asset = side_asset(side_value)
        self.main_refund_penalty = main_asset(main_refund_penalty)
        self.side_refund_penalty = side_asset(side_refund_penalty)


def generate_asset_stanzas(
        assets: Optional[Dict[str, XChainAsset]] = None) -> str:
    if assets is None:
        # default to xrp only at a 1:1 value
        assets = {}
        assets['xrp_xrp_sidechain_asset'] = XChainAsset(
            XRP(0), XRP(0), 1, 1, 400, 400)

    index_stanza = """
[sidechain_assets]"""

    asset_stanzas = []

    for name, xchainasset in assets.items():
        index_stanza += '\n' + name
        new_stanza = f"""
[{name}]
mainchain_asset={json.dumps(xchainasset.main_asset.to_cmd_obj())}
sidechain_asset={json.dumps(xchainasset.side_asset.to_cmd_obj())}
mainchain_refund_penalty={json.dumps(xchainasset.main_refund_penalty.to_cmd_obj())}
sidechain_refund_penalty={json.dumps(xchainasset.side_refund_penalty.to_cmd_obj())}"""
        asset_stanzas.append(new_stanza)

    return index_stanza + '\n' + '\n'.join(asset_stanzas)


# First element of the returned tuple is the sidechain stanzas
# second element is the bootstrap stanzas
def generate_sidechain_stanza(
        mainchain_ports: Ports,
        main_account: dict,
        federators: List[Keypair],
        signing_key: str,
        mainchain_cfg_file: str,
        xchain_assets: Optional[Dict[str,
                                     XChainAsset]] = None) -> Tuple[str, str]:
    mainchain_ip = "127.0.0.1"

    federators_stanza = """
# federator signing public keys
[sidechain_federators]
"""
    federators_secrets_stanza = """
# federator signing secret keys (for standalone-mode testing only; Normally won't be in a config file)
[sidechain_federators_secrets]
"""
    bootstrap_federators_stanza = """
# first value is federator signing public key, second is the signing pk account
[sidechain_federators]
"""

    assets_stanzas = generate_asset_stanzas(xchain_assets)

    for fed in federators:
        federators_stanza += f'{fed.public_key}\n'
        federators_secrets_stanza += f'{fed.secret_key}\n'
        bootstrap_federators_stanza += f'{fed.public_key} {fed.account_id}\n'

    sidechain_stanzas = f"""
[sidechain]
signing_key={signing_key}
mainchain_account={main_account["account_id"]}
mainchain_ip={mainchain_ip}
mainchain_port_ws={mainchain_ports.ws_public_port}
# mainchain config file is: {mainchain_cfg_file}

{assets_stanzas}

{federators_stanza}

{federators_secrets_stanza}
"""
    bootstrap_stanzas = f"""
[sidechain]
mainchain_secret={main_account["master_seed"]}

{bootstrap_federators_stanza}
"""
    return (sidechain_stanzas, bootstrap_stanzas)


# cfg_type will typically be either 'dog' or 'test', but can be any string. It is only used
# to create the data directories.
def generate_cfg_dir(*,
                     ports: Ports,
                     with_shards: bool,
                     main_net: bool,
                     cfg_type: str,
                     sidechain_stanza: str,
                     sidechain_bootstrap_stanza: str,
                     validation_seed: Optional[str] = None,
                     validators: Optional[List[str]] = None,
                     fixed_ips: Optional[List[Ports]] = None,
                     data_dir: str,
                     full_history: bool = False,
                     with_hooks: bool = False) -> str:
    ips_stanza = ''
    this_ip = '127.0.0.1'
    if fixed_ips:
        ips_stanza = '# Fixed ips for a testnet.\n'
        ips_stanza += '[ips_fixed]\n'
        for i, p in enumerate(fixed_ips):
            if p.peer_port == ports.peer_port:
                continue
            # rippled limits the number of connects per ip. So use the other loopback devices
            ips_stanza += f'127.0.0.{i+1} {p.peer_port}\n'
    else:
        ips_stanza = '# Where to find some other servers speaking the Ripple protocol.\n'
        ips_stanza += '[ips]\n'
        if main_net:
            ips_stanza += 'r.ripple.com 51235\n'
        else:
            ips_stanza += 'r.altnet.rippletest.net 51235\n'
    disable_shards = '' if with_shards else '# '
    disable_delete = '#' if full_history else ''
    history_line = 'full' if full_history else '256'
    earliest_seq_line = ''
    if sidechain_stanza:
        earliest_seq_line = 'earliest_seq=1'
    hooks_line = 'Hooks' if with_hooks else ''
    validation_seed_stanza = ''
    if validation_seed:
        validation_seed_stanza = f'''
[validation_seed]
{validation_seed}
        '''
    node_size = 'medium'
    shard_str = 'shards' if with_shards else 'no_shards'
    net_str = 'main' if main_net else 'test'
    if not fixed_ips:
        sub_dir = data_dir + f'/{net_str}.{shard_str}.{cfg_type}'
        if sidechain_stanza:
            sub_dir += '.sidechain'
    else:
        sub_dir = data_dir + f'/{cfg_type}'
    db_path = sub_dir + '/db'
    debug_logfile = sub_dir + '/debug.log'
    shard_db_path = sub_dir + '/shards'
    node_db_path = db_path + '/nudb'

    cfg_str = f"""
[server]
port_rpc_admin_local
port_peer
port_ws_admin_local
port_ws_public
#ssl_key = /etc/ssl/private/server.key
#ssl_cert = /etc/ssl/certs/server.crt

[port_rpc_admin_local]
port = {ports.http_admin_port}
ip = {this_ip}
admin = {this_ip}
protocol = http

[port_peer]
port = {ports.peer_port}
ip = 0.0.0.0
protocol = peer

[port_ws_admin_local]
port = {ports.ws_admin_port}
ip = {this_ip}
admin = {this_ip}
protocol = ws

[port_ws_public]
port = {ports.ws_public_port}
ip = {this_ip}
protocol = ws
# protocol = wss

[node_size]
{node_size}

[ledger_history]
{history_line}

[node_db]
type=NuDB
path={node_db_path}
open_files=2000
filter_bits=12
cache_mb=256
file_size_mb=8
file_size_mult=2
{earliest_seq_line}
{disable_delete}online_delete=256
{disable_delete}advisory_delete=0

[database_path]
{db_path}

# This needs to be an absolute directory reference, not a relative one.
# Modify this value as required.
[debug_logfile]
{debug_logfile}

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

{ips_stanza}

[validators_file]
validators.txt

[rpc_startup]
{{ "command": "log_level", "severity": "fatal" }}
{{ "command": "log_level", "partition": "SidechainFederator", "severity": "trace" }}

[ssl_verify]
1

{validation_seed_stanza}

{disable_shards}[shard_db]
{disable_shards}type=NuDB
{disable_shards}path={shard_db_path}
{disable_shards}max_historical_shards=6

{sidechain_stanza}

[features]
{hooks_line}
PayChan
Flow
FlowCross
TickSize
fix1368
Escrow
fix1373
EnforceInvariants
SortedDirectories
fix1201
fix1512
fix1513
fix1523
fix1528
DepositAuth
Checks
fix1571
fix1543
fix1623
DepositPreauth
fix1515
fix1578
MultiSignReserve
fixTakerDryOfferRemoval
fixMasterKeyAsRegularKey
fixCheckThreading
fixPayChanRecipientOwnerDir
DeletableAccounts
fixQualityUpperBound
RequireFullyCanonicalSig
fix1781 
HardenedValidations
fixAmendmentMajorityCalc
NegativeUNL
TicketBatch
FlowSortStrands
fixSTAmountCanonicalize
fixRmSmallIncreasedQOffers
CheckCashMakesTrustLine
"""

    validators_str = ''
    for p in [sub_dir, db_path, shard_db_path]:
        Path(p).mkdir(parents=True, exist_ok=True)
    # Add the validators.txt file
    if validators:
        validators_str = '[validators]\n'
        for k in validators:
            validators_str += f'{k}\n'
    else:
        validators_str = mainnet_validators if main_net else altnet_validators
    with open(sub_dir + "/validators.txt", "w") as f:
        f.write(validators_str)

    # add the rippled.cfg file
    with open(sub_dir + "/rippled.cfg", "w") as f:
        f.write(cfg_str)

    if sidechain_bootstrap_stanza:
        # add the bootstrap file
        with open(sub_dir + "/sidechain_bootstrap.cfg", "w") as f:
            f.write(sidechain_bootstrap_stanza)

    return sub_dir + "/rippled.cfg"


def generate_multinode_net(out_dir: str,
                           mainnet: Network,
                           sidenet: SidechainNetwork,
                           xchain_assets: Optional[Dict[str,
                                                        XChainAsset]] = None):
    mainnet_cfgs = []
    for i in range(len(mainnet.ports)):
        validator_kp = mainnet.validator_keypairs[i]
        ports = mainnet.ports[i]
        mainchain_cfg_file = generate_cfg_dir(
            ports=ports,
            with_shards=False,
            main_net=True,
            cfg_type=f'mainchain_{i}',
            sidechain_stanza='',
            sidechain_bootstrap_stanza='',
            validation_seed=validator_kp.secret_key,
            data_dir=out_dir)
        mainnet_cfgs.append(mainchain_cfg_file)

    for i in range(len(sidenet.ports)):
        validator_kp = sidenet.validator_keypairs[i]
        ports = sidenet.ports[i]

        mainnet_i = i % len(mainnet.ports)
        sidechain_stanza, sidechain_bootstrap_stanza = generate_sidechain_stanza(
            mainnet.ports[mainnet_i], sidenet.main_account,
            sidenet.federator_keypairs,
            sidenet.federator_keypairs[i].secret_key, mainnet_cfgs[mainnet_i],
            xchain_assets)

        generate_cfg_dir(
            ports=ports,
            with_shards=False,
            main_net=True,
            cfg_type=f'sidechain_{i}',
            sidechain_stanza=sidechain_stanza,
            sidechain_bootstrap_stanza=sidechain_bootstrap_stanza,
            validation_seed=validator_kp.secret_key,
            validators=[kp.public_key for kp in sidenet.validator_keypairs],
            fixed_ips=sidenet.ports,
            data_dir=out_dir,
            full_history=True,
            with_hooks=False)


def parse_args():
    parser = argparse.ArgumentParser(
        description=('Create config files for testing sidechains'))

    parser.add_argument(
        '--exe',
        '-e',
        help=('path to rippled executable'),
    )

    parser.add_argument(
        '--cfgs_dir',
        '-c',
        help=
        ('path to configuration file dir (where the output config files will be located)'
         ),
    )

    return parser.parse_known_args()[0]


class Params:
    def __init__(self):
        args = parse_args()

        self.exe = None
        if 'RIPPLED_MAINCHAIN_EXE' in os.environ:
            self.exe = os.environ['RIPPLED_MAINCHAIN_EXE']
        if args.exe:
            self.exe = args.exe

        self.configs_dir = None
        if 'RIPPLED_SIDECHAIN_CFG_DIR' in os.environ:
            self.configs_dir = os.environ['RIPPLED_SIDECHAIN_CFG_DIR']
        if args.cfgs_dir:
            self.configs_dir = args.cfgs_dir

    def check_error(self) -> str:
        '''
        Check for errors. Return `None` if no errors,
        otherwise return a string describing the error
        '''
        if not self.exe:
            return 'Missing exe location. Either set the env variable RIPPLED_MAINCHAIN_EXE or use the --exe_mainchain command line switch'
        if not self.configs_dir:
            return 'Missing configs directory location. Either set the env variable RIPPLED_SIDECHAIN_CFG_DIR or use the --cfgs_dir command line switch'


def main(params: Params,
         xchain_assets: Optional[Dict[str, XChainAsset]] = None):

    if err_str := params.check_error():
        eprint(err_str)
        sys.exit(1)
    index = 0
    nonvalidator_cfg_file_name = generate_cfg_dir(
        ports=Ports(index),
        with_shards=False,
        main_net=True,
        cfg_type='non_validator',
        sidechain_stanza='',
        sidechain_bootstrap_stanza='',
        validation_seed=None,
        data_dir=params.configs_dir)
    index = index + 1

    nonvalidator_config = ConfigFile(file_name=nonvalidator_cfg_file_name)
    with single_client_app(exe=params.exe,
                           config=nonvalidator_config,
                           standalone=True) as rip:
        mainnet = Network(num_nodes=1,
                          num_validators=1,
                          start_cfg_index=index,
                          rip=rip)
        sidenet = SidechainNetwork(num_nodes=5,
                                   num_federators=5,
                                   num_validators=5,
                                   start_cfg_index=index + 1,
                                   rip=rip)
        generate_multinode_net(
            out_dir=f'{params.configs_dir}/sidechain_testnet',
            mainnet=mainnet,
            sidenet=sidenet,
            xchain_assets=xchain_assets)
        index = index + 2

        (Path(params.configs_dir) / 'logs').mkdir(parents=True, exist_ok=True)

        for with_shards in [True, False]:
            for is_main_net in [True, False]:
                for cfg_type in ['dog', 'test', 'one', 'two']:
                    if not is_main_net and cfg_type not in ['dog', 'test']:
                        continue

                    mainnet = Network(num_nodes=1,
                                      num_validators=1,
                                      start_cfg_index=index,
                                      rip=rip)
                    mainchain_cfg_file = generate_cfg_dir(
                        data_dir=params.configs_dir,
                        ports=mainnet.ports[0],
                        with_shards=with_shards,
                        main_net=is_main_net,
                        cfg_type=cfg_type,
                        sidechain_stanza='',
                        sidechain_bootstrap_stanza='',
                        validation_seed=mainnet.validator_keypairs[0].
                        secret_key)

                    sidenet = SidechainNetwork(num_nodes=1,
                                               num_federators=5,
                                               num_validators=1,
                                               start_cfg_index=index + 1,
                                               rip=rip)
                    signing_key = sidenet.federator_keypairs[0].secret_key

                    sidechain_stanza, sizechain_bootstrap_stanza = generate_sidechain_stanza(
                        mainnet.ports[0], sidenet.main_account,
                        sidenet.federator_keypairs, signing_key,
                        mainchain_cfg_file)

                    generate_cfg_dir(
                        data_dir=params.configs_dir,
                        ports=sidenet.ports[0],
                        with_shards=with_shards,
                        main_net=is_main_net,
                        cfg_type=cfg_type,
                        sidechain_stanza=sidechain_stanza,
                        sidechain_bootstrap_stanza=sizechain_bootstrap_stanza,
                        validation_seed=sidenet.validator_keypairs[0].
                        secret_key)
                    index = index + 2


if __name__ == '__main__':
    params = Params()
    main(params)
