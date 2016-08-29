#!/usr/bin/env python
"""
Test for setting ephemeral keys for the validator manifest.
"""

from __future__ import (
    absolute_import, division, print_function, unicode_literals
)

import argparse
import contextlib
from contextlib import contextmanager
import json
import os
import platform
import shutil
import subprocess
import time

DELAY_WHILE_PROCESS_STARTS_UP = 1.5
ARGS = None

NOT_FOUND = -1  # not in log
ACCEPTED_NEW = 0  # added new manifest
ACCEPTED_UPDATE = 1  # replaced old manifest with new
UNTRUSTED = 2  # don't trust master key
STALE = 3  # seq is too old
REVOKED = 4  # revoked validator key
INVALID = 5  # invalid signature

MANIFEST_ACTION_STR_TO_ID = {
    'NotFound': NOT_FOUND,  # not found in log
    'AcceptedNew': ACCEPTED_NEW,
    'AcceptedUpdate': ACCEPTED_UPDATE,
    'Untrusted': UNTRUSTED,
    'Stale': STALE,
    'Revoked': REVOKED,
    'Invalid': INVALID,
}

MANIFEST_ACTION_ID_TO_STR = {
    v: k for k, v in MANIFEST_ACTION_STR_TO_ID.items()
}

CONF_TEMPLATE = """
[server]
port_rpc
port_peer
port_wss_admin

[port_rpc]
port = {rpc_port}
ip = 127.0.0.1
admin = 127.0.0.1
protocol = https

[port_peer]
port = {peer_port}
ip = 0.0.0.0
protocol = peer

[port_wss_admin]
port = {wss_port}
ip = 127.0.0.1
admin = 127.0.0.1
protocol = wss

[node_size]
medium

[node_db]
type={node_db_type}
path={node_db_path}
open_files=2000
filter_bits=12
cache_mb=256
file_size_mb=8
file_size_mult=2
online_delete=256
advisory_delete=0

[database_path]
{db_path}

[debug_logfile]
{debug_logfile}

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

[ips]
r.ripple.com 51235

[ips_fixed]
{sibling_ip} {sibling_port}

[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7    RL1
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj    RL2
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C    RL3
n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS    RL4
n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA    RL5

[validation_seed]
{validation_seed}
#vaidation_public_key: {validation_public_key}

# Other rippled's trusting this validator need this key
[validator_keys]
{all_validator_keys}

[peer_private]
1

[overlay]
expire = 1
auto_connect = 1

[validation_manifest]
{validation_manifest}

[rpc_startup]
{{ "command": "log_level", "severity": "debug" }}

[ssl_verify]
0
"""
# End config template


def static_vars(**kwargs):
    def decorate(func):
        for k in kwargs:
            setattr(func, k, kwargs[k])
        return func
    return decorate


@static_vars(rpc=5005, peer=51235, wss=6006)
def checkout_port_nums():
    """Returns a tuple of port nums for rpc, peer, and wss_admin"""
    checkout_port_nums.rpc += 1
    checkout_port_nums.peer += 1
    checkout_port_nums.wss += 1
    return (
        checkout_port_nums.rpc,
        checkout_port_nums.peer,
        checkout_port_nums.wss
    )


def is_windows():
    return platform.system() == 'Windows'


def manifest_create():
    """returns dict with keys: 'validator_keys', 'master_secret'"""
    to_run = ['python', ARGS.ripple_home + '/bin/python/Manifest.py', 'create']
    r = subprocess.check_output(to_run)
    result = {}
    k = None
    for l in r.splitlines():
        l = l.strip()
        if not l:
            continue
        elif l == '[validator_keys]':
            k = l[1:-1]
        elif l == '[master_secret]':
            k = l[1:-1]
        elif l.startswith('['):
            raise ValueError(
                'Unexpected key: {} from `manifest create`'.format(l))
        else:
            if not k:
                raise ValueError('Value with no key')
            result[k] = l
            k = None

        if k in result:
            raise ValueError('Repeat key from `manifest create`: ' + k)
    if len(result) != 2:
        raise ValueError(
            'Expected 2 keys from `manifest create` but got {} keys instead ({})'.
            format(len(result), result))

    return result


def sign_manifest(seq, validation_pk, master_secret):
    """returns the signed manifest as a string"""
    to_run = ['python', ARGS.ripple_home + '/bin/python/Manifest.py', 'sign',
              str(seq), validation_pk, master_secret]
    try:
        r = subprocess.check_output(to_run)
    except subprocess.CalledProcessError as e:
        print('Error in sign_manifest: ', e.output)
        raise e
    result = []
    for l in r.splitlines():
        l.strip()
        if not l or l == '[validation_manifest]':
            continue
        result.append(l)
    return '\n'.join(result)


def get_ripple_exe():
    """Find the rippled executable"""
    prefix = ARGS.ripple_home + '/build/'
    exe = ['rippled', 'RippleD.exe']
    to_test = [prefix + t + '.debug/' + e
               for t in ['clang', 'gcc', 'msvc'] for e in exe]
    for e in exe:
        to_test.append(prefix + '/' + e)
    for t in to_test:
        if os.path.isfile(t):
            return t


class RippledServer(object):
    def __init__(self, exe, config_file, server_out):
        self.config_file = config_file
        self.exe = exe
        self.process = None
        self.server_out = server_out
        self.reinit(config_file)

    def reinit(self, config_file):
        self.config_file = config_file
        self.to_run = [self.exe, '--verbose', '--conf', self.config_file]

    @property
    def config_root(self):
        return os.path.dirname(self.config_file)

    @property
    def master_secret_file(self):
        return self.config_root + '/master_secret.txt'

    def startup(self):
        if ARGS.verbose:
            print('starting rippled:' + self.config_file)
        fout = open(self.server_out, 'w')
        self.process = subprocess.Popen(
            self.to_run, stdout=fout, stderr=subprocess.STDOUT)

    def shutdown(self):
        if not self.process:
            return
        fout = open(os.devnull, 'w')
        subprocess.Popen(
            self.to_run + ['stop'], stdout=fout, stderr=subprocess.STDOUT)
        self.process.wait()
        self.process = None

    def rotate_logfile(self):
        if self.server_out == os.devnull:
            return
        for i in range(100):
            backup_name = '{}.{}'.format(self.server_out, i)
            if not os.path.exists(backup_name):
                os.rename(self.server_out, backup_name)
                return
        raise ValueError('Could not rotate logfile: {}'.
                         format(self.server_out))

    def validation_create(self):
        """returns dict with keys:
        'validation_key', 'validation_public_key', 'validation_seed'
        """
        to_run = [self.exe, '-q', '--conf', self.config_file,
                  '--', 'validation_create']
        try:
            return json.loads(subprocess.check_output(to_run))['result']
        except subprocess.CalledProcessError as e:
            print('Error in validation_create: ', e.output)
            raise e


@contextmanager
def rippled_server(config_file, server_out=os.devnull):
    """Start a ripple server"""
    try:
        server = None
        server = RippledServer(ARGS.ripple_exe, config_file, server_out)
        server.startup()
        yield server
    finally:
        if server:
            server.shutdown()


@contextmanager
def pause_server(server, config_file):
    """Shutdown and then restart a ripple server"""
    try:
        server.shutdown()
        server.rotate_logfile()
        yield server
    finally:
        server.reinit(config_file)
        server.startup()


def parse_date(d, t):
    """Return the timestamp of a line, or none if the line has no timestamp"""
    try:
        return time.strptime(d+' '+t, '%Y-%B-%d %H:%M:%S')
    except:
        return None


def to_dict(l):
    """Given a line of the form Key0: Value0;Key2: Valuue2; Return a dict"""
    fields = l.split(';')
    result = {}
    for f in fields:
        if f:
            v = f.split(':')
            assert len(v) == 2
            result[v[0].strip()] = v[1].strip()
    return result


def check_ephemeral_key(validator_key,
                        log_file,
                        seq,
                        change_time):
    """
    Detect when a server is informed of a validator's ephemeral key change.
    `change_time` and `seq` may be None, in which case they are ignored.
    """
    manifest_prefix = 'Manifest:'
    # a manifest line has the form Manifest: action; Key: value;
    # Key can be Pk (public key), Seq, OldSeq,
    for l in open(log_file):
        sa = l.split()
        if len(sa) < 5 or sa[4] != manifest_prefix:
            continue

        d = to_dict(' '.join(sa[4:]))
        # check the seq number and validator_key
        if d['Pk'] != validator_key:
            continue
        if seq is not None and int(d['Seq']) != seq:
            continue

        if change_time:
            t = parse_date(sa[0], sa[1])
            if not t or t < change_time:
                continue
        action = d['Manifest']
        return MANIFEST_ACTION_STR_TO_ID[action]
    return NOT_FOUND


def check_ephemeral_keys(validator_key,
                         log_files,
                         seq,
                         change_time=None,
                         timeout_s=60):
    result = [NOT_FOUND for i in range(len(log_files))]
    if timeout_s < 10:
        sleep_time = 1
    elif timeout_s < 60:
        sleep_time = 5
    else:
        sleep_time = 10
    n = timeout_s//sleep_time
    if n == 0:
        n = 1
    start_time = time.time()
    for _ in range(n):
        for i, lf in enumerate(log_files):
            if result[i] != NOT_FOUND:
                continue
            result[i] = check_ephemeral_key(validator_key,
                                            lf,
                                            seq,
                                            change_time)
            if result[i] != NOT_FOUND:
                if all(r != NOT_FOUND for r in result):
                    return result
                else:
                    server_dir = os.path.basename(os.path.dirname(log_files[i]))
                    if ARGS.verbose:
                        print('Check for {}: {}'.format(
                            server_dir, MANIFEST_ACTION_ID_TO_STR[result[i]]))
        tsf = time.time() - start_time
        if tsf > 20:
            if ARGS.verbose:
                print('Waiting for key to propigate: ', tsf)
        time.sleep(sleep_time)
    return result


def get_validator_key(config_file):
    in_validator_keys = False
    for l in open(config_file):
        sl = l.strip()
        if not in_validator_keys and sl == '[validator_keys]':
            in_validator_keys = True
            continue
        if in_validator_keys:
            if sl.startswith('['):
                raise ValueError('ThisServer validator key not found')
            if sl.startswith('#'):
                continue
            s = sl.split()
            if len(s) == 2 and s[1] == 'ThisServer':
                return s[0]


def new_config_ephemeral_key(
        server, seq, rm_dbs=False, master_secret_file=None):
    """Generate a new ephemeral key, add to config, restart server"""
    config_root = server.config_root
    config_file = config_root + '/rippled.cfg'
    db_dir = config_root + '/db'
    if not master_secret_file:
        master_secret_file = server.master_secret_file
    with open(master_secret_file) as f:
        master_secret = f.read()
    v = server.validation_create()
    signed = sign_manifest(seq, v['validation_public_key'], master_secret)
    with pause_server(server, config_file):
        if rm_dbs and os.path.exists(db_dir):
            shutil.rmtree(db_dir)
            os.makedirs(db_dir)
        # replace the validation_manifest section with `signed`
        bak = config_file + '.bak'
        if is_windows() and os.path.isfile(bak):
            os.remove(bak)
        os.rename(config_file, bak)
        in_manifest = False
        with open(bak, 'r') as src:
            with open(config_file, 'w') as out:
                for l in src:
                    sl = l.strip()
                    if not in_manifest and sl == '[validation_manifest]':
                        in_manifest = True
                    elif in_manifest:
                        if sl.startswith('[') or sl.startswith('#'):
                            in_manifest = False
                            out.write(signed)
                            out.write('\n\n')
                        else:
                            continue
                    out.write(l)
    return (bak, config_file)


def parse_args():
    parser = argparse.ArgumentParser(
        description=('Create config files for n validators')
    )

    parser.add_argument(
        '--ripple_home', '-r',
        default=os.sep.join(os.path.realpath(__file__).split(os.sep)[:-5]),
        help=('Root directory of the ripple repo'), )
    parser.add_argument('--num_validators', '-n',
                        default=2,
                        help=('Number of validators'), )
    parser.add_argument('--conf', '-c', help=('rippled config file'), )
    parser.add_argument('--out', '-o',
                        default='test_output',
                        help=('config root directory'), )
    parser.add_argument(
        '--existing', '-e',
        action='store_true',
        help=('use existing config files'), )
    parser.add_argument(
        '--generate', '-g',
        action='store_true',
        help=('generate conf files only'), )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help=('verbose status reporting'), )
    parser.add_argument(
        '--quiet', '-q',
        action='store_true',
        help=('quiet status reporting'), )

    return parser.parse_args()


def get_configs(manifest_seq):
    global ARGS
    ARGS.ripple_home = os.path.expanduser(ARGS.ripple_home)

    n = int(ARGS.num_validators)
    if n<2:
        raise ValueError(
            'Need at least 2 rippled servers. Specified: {}'.format(n))
    config_root = ARGS.out
    ARGS.ripple_exe = get_ripple_exe()
    if not ARGS.ripple_exe:
        raise ValueError('No Exe Found')

    if ARGS.existing:
        return [
            os.path.abspath('{}/validator_{}/rippled.cfg'.format(config_root, i))
            for i in range(n)
        ]

    initial_config = ARGS.conf

    manifests = [manifest_create() for i in range(n)]
    port_nums = [checkout_port_nums() for i in range(n)]
    with rippled_server(initial_config) as server:
        time.sleep(DELAY_WHILE_PROCESS_STARTS_UP)
        validations = [server.validation_create() for i in range(n)]

    signed_manifests = [sign_manifest(manifest_seq,
                                      v['validation_public_key'],
                                      m['master_secret'])
                        for m, v in zip(manifests, validations)]
    node_db_type = 'RocksDB' if not is_windows() else 'NuDB'
    node_db_filename = node_db_type.lower()

    config_files = []
    for i, (m, v, s) in enumerate(zip(manifests, validations, signed_manifests)):
        sibling_index = (i - 1) % len(manifests)
        all_validator_keys = '\n'.join([
            m['validator_keys'] + ' ThisServer',
            manifests[sibling_index]['validator_keys'] + ' NextInRing'])
        this_validator_dir = os.path.abspath(
            '{}/validator_{}'.format(config_root, i))
        db_path = this_validator_dir + '/db'
        node_db_path = db_path + '/' + node_db_filename
        log_path = this_validator_dir + '/log'
        debug_logfile = log_path + '/debug.log'
        rpc_port, peer_port, wss_port = port_nums[i]
        sibling_ip = '127.0.0.1'
        sibling_port = port_nums[sibling_index][1]
        d = {
            'validation_manifest': s,
            'all_validator_keys': all_validator_keys,
            'node_db_type': node_db_type,
            'node_db_path': node_db_path,
            'db_path': db_path,
            'debug_logfile': debug_logfile,
            'rpc_port': rpc_port,
            'peer_port': peer_port,
            'wss_port': wss_port,
            'sibling_ip': sibling_ip,
            'sibling_port': sibling_port,
        }
        d.update(m)
        d.update(v)

        for p in [this_validator_dir, db_path, log_path]:
            if not os.path.exists(p):
                os.makedirs(p)

        config_files.append('{}/rippled.cfg'.format(this_validator_dir))
        with open(config_files[-1], 'w') as f:
            f.write(CONF_TEMPLATE.format(**d))

        with open('{}/master_secret.txt'.format(this_validator_dir), 'w') as f:
            f.write(m['master_secret'])

    return config_files


def update_ephemeral_key(
        server, new_seq, log_files,
        expected=None, rm_dbs=False, master_secret_file=None,
        restore_origional_conf=False, timeout_s=300):
    if not expected:
        expected = {}

    change_time = time.gmtime()
    back_conf, new_conf = new_config_ephemeral_key(
        server,
        new_seq,
        rm_dbs,
        master_secret_file
    )
    validator_key = get_validator_key(server.config_file)
    start_time = time.time()
    ck = check_ephemeral_keys(validator_key,
                              log_files,
                              seq=new_seq,
                              change_time=change_time,
                              timeout_s=timeout_s)
    if ARGS.verbose:
        print('Check finished: {} secs.'.format(int(time.time() - start_time)))
    all_success = True
    for i, r in enumerate(ck):
        e = expected.get(i, UNTRUSTED)
        server_dir = os.path.basename(os.path.dirname(log_files[i]))
        status = 'OK' if e == r else 'FAIL'
        print('{}: Server: {} Expected: {} Got: {}'.
              format(status, server_dir,
                     MANIFEST_ACTION_ID_TO_STR[e], MANIFEST_ACTION_ID_TO_STR[r]))
        all_success = all_success and (e == r)
    if restore_origional_conf:
        if is_windows() and os.path.isfile(new_conf):
            os.remove(new_conf)
        os.rename(back_conf, new_conf)
    return all_success


def run_main():
    global ARGS
    ARGS = parse_args()
    manifest_seq = 1
    config_files = get_configs(manifest_seq)
    if ARGS.generate:
        return
    if len(config_files) <= 1:
        print('Script requires at least 2 servers. Actual #: {}'.
              format(len(config_files)))
        return
    with contextlib.nested(*(rippled_server(c, os.path.dirname(c)+'/log.txt')
                             for c in config_files)) as servers:
        log_files = [os.path.dirname(cf)+'/log.txt' for cf in config_files[1:]]
        validator_key = get_validator_key(config_files[0])
        start_time = time.time()
        ck = check_ephemeral_keys(validator_key,
                                  [log_files[0]],
                                  seq=None,
                                  timeout_s=60)
        if ARGS.verbose:
            print('Check finished: {} secs.'.format(
                int(time.time() - start_time)))
        if any(r == NOT_FOUND for r in ck):
            print('FAIL: Initial key did not propigate to all servers')
            return

        manifest_seq += 2
        expected = {i: UNTRUSTED for i in range(len(log_files))}
        expected[0] = ACCEPTED_UPDATE
        if not ARGS.quiet:
            print('Testing key update')
        kr = update_ephemeral_key(servers[0], manifest_seq, log_files, expected)
        if not kr:
            print('\nFail: Key Update Test. Exiting')
            return

        expected = {i: UNTRUSTED for i in range(len(log_files))}
        expected[0] = STALE
        if not ARGS.quiet:
            print('Testing stale key')
        kr = update_ephemeral_key(
            servers[0], manifest_seq-1, log_files, expected, rm_dbs=True)
        if not kr:
            print('\nFail: Stale Key Test. Exiting')
            return

        expected = {i: UNTRUSTED for i in range(len(log_files))}
        expected[0] = STALE
        if not ARGS.quiet:
            print('Testing stale key 2')
        kr = update_ephemeral_key(
            servers[0], manifest_seq, log_files, expected, rm_dbs=True)
        if not kr:
            print('\nFail: Stale Key Test. Exiting')
            return

        expected = {i: UNTRUSTED for i in range(len(log_files))}
        expected[0] = REVOKED
        if not ARGS.quiet:
            print('Testing revoked key')
        kr = update_ephemeral_key(
            servers[0], 0xffffffff, log_files, expected, rm_dbs=True)
        if not kr:
            print('\nFail: Revoked Key Text. Exiting')
            return
        print('\nOK: All tests passed')

if __name__ == '__main__':
    run_main()
