from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util import ConfigFile

from unittest import TestCase

class test_ConfigFile(TestCase):
    def test_trivial(self):
        self.assertEquals(ConfigFile.read(''), {})

    def test_full(self):
        self.assertEquals(ConfigFile.read(FULL.splitlines()), RESULT)

RESULT = {
    'websocket_port': '6206',
    'database_path': '/development/alpha/db',
    'sntp_servers':
        ['time.windows.com', 'time.apple.com', 'time.nist.gov', 'pool.ntp.org'],
    'validation_seed': 'sh1T8T9yGuV7Jb6DPhqSzdU2s5LcV',
    'node_size': 'medium',
    'rpc_startup': {
        'command': 'log_level',
        'severity': 'debug'},
    'ips': ['r.ripple.com', '51235'],
    'node_db': {
        'file_size_mult': '2',
        'file_size_mb': '8',
        'cache_mb': '256',
        'path': '/development/alpha/db/rocksdb',
        'open_files': '2000',
        'type': 'RocksDB',
        'filter_bits': '12'},
    'peer_port': '53235',
    'ledger_history': 'full',
    'rpc_ip': '127.0.0.1',
    'websocket_public_ip': '0.0.0.0',
    'rpc_allow_remote': '0',
    'validators':
         [['n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7', 'RL1'],
          ['n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj', 'RL2'],
          ['n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C', 'RL3'],
          ['n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS', 'RL4'],
          ['n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA', 'RL5']],
    'debug_logfile': '/development/alpha/debug.log',
    'websocket_public_port': '5206',
    'peer_ip': '0.0.0.0',
    'rpc_port': '5205',
    'websocket_ip': '127.0.0.1'}

FULL = """
[ledger_history]
full

# Allow other peers to connect to this server.
#
[peer_ip]
0.0.0.0

[peer_port]
53235

# Allow untrusted clients to connect to this server.
#
[websocket_public_ip]
0.0.0.0

[websocket_public_port]
5206

# Provide trusted websocket ADMIN access to the localhost.
#
[websocket_ip]
127.0.0.1

[websocket_port]
6206

# Provide trusted json-rpc ADMIN access to the localhost.
#
[rpc_ip]
127.0.0.1

[rpc_port]
5205

[rpc_allow_remote]
0

[node_size]
medium

# This is primary persistent datastore for rippled.  This includes transaction
# metadata, account states, and ledger headers.  Helpful information can be
# found here: https://ripple.com/wiki/NodeBackEnd
[node_db]
type=RocksDB
path=/development/alpha/db/rocksdb
open_files=2000
filter_bits=12
cache_mb=256
file_size_mb=8
file_size_mult=2

[database_path]
/development/alpha/db

# This needs to be an absolute directory reference, not a relative one.
# Modify this value as required.
[debug_logfile]
/development/alpha/debug.log

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

# Where to find some other servers speaking the Ripple protocol.
#
[ips]
r.ripple.com 51235

# The latest validators can be obtained from
# https://ripple.com/ripple.txt
#
[validators]
n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7	RL1
n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj	RL2
n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C	RL3
n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS	RL4
n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA	RL5

[validation_seed]
sh1T8T9yGuV7Jb6DPhqSzdU2s5LcV

# Turn down default logging to save disk space in the long run.
# Valid values here are trace, debug, info, warning, error, and fatal
[rpc_startup]
{ "command": "log_level", "severity": "debug" }

# Configure SSL for WebSockets.  Not enabled by default because not everybody
# has an SSL cert on their server, but if you uncomment the following lines and
# set the path to the SSL certificate and private key the WebSockets protocol
# will be protected by SSL/TLS.
#[websocket_secure]
#1

#[websocket_ssl_cert]
#/etc/ssl/certs/server.crt

#[websocket_ssl_key]
#/etc/ssl/private/server.key

# Defaults to 0 ("no") so that you can use self-signed SSL certificates for
# development, or internally.
#[ssl_verify]
#0
""".strip()
