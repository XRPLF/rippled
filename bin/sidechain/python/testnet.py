'''
Bring up a rippled testnetwork from a set of config files with fixed ips.
'''

from contextlib import contextmanager
import glob
import os
import subprocess
import time
from typing import Callable, List, Optional, Set, Union

from command import ServerInfo
from config_file import ConfigFile
from ripple_client import RippleClient


class Network:
    # If run_server is None, run all the servers.
    # This is useful to help debugging
    def __init__(self,
                 exe: str,
                 configs: List[ConfigFile],
                 *,
                 command_logs: Optional[List[str]] = None,
                 run_server: Optional[List[bool]] = None,
                 extra_args: Optional[List[List[str]]] = None):
        if not configs:
            raise ValueError(f'Must specify at least one config')

        if run_server and len(run_server) != len(configs):
            raise ValueError(
                f'run_server length must match number of configs (or be None): {len(configs) = } {len(run_server) = }'
            )

        self.configs = configs
        self.clients = []
        self.running_server_indexes = set()
        self.processes = {}

        if not run_server:
            run_server = []
        run_server += [True] * (len(configs) - len(run_server))

        self.run_server = run_server

        if not command_logs:
            command_logs = []
        command_logs += [None] * (len(configs) - len(command_logs))

        self.command_logs = command_logs

        # remove the old database directories.
        # we want tests to start from the same empty state every time
        for config in self.configs:
            db_path = config.database_path.get_line()
            if db_path and os.path.isdir(db_path):
                files = glob.glob(f'{db_path}/**', recursive=True)
                for f in files:
                    if os.path.isdir(f):
                        continue
                    os.unlink(f)

        for config, log in zip(self.configs, self.command_logs):
            client = RippleClient(config=config, command_log=log, exe=exe)
            self.clients.append(client)

        self.servers_start(extra_args=extra_args)

    def shutdown(self):
        for a in self.clients:
            a.shutdown()

        self.servers_stop()

    def num_clients(self) -> int:
        return len(self.clients)

    def get_client(self, i: int) -> RippleClient:
        return self.clients[i]

    def get_configs(self) -> List[ConfigFile]:
        return [c.config for c in self.clients]

    def get_pids(self) -> List[int]:
        return [c.get_pid() for c in self.clients if c.get_pid() is not None]

    # Get a dict of the server_state, validated_ledger_seq, and complete_ledgers
    def get_brief_server_info(self) -> dict:
        ret = {'server_state': [], 'ledger_seq': [], 'complete_ledgers': []}
        for c in self.clients:
            r = c.get_brief_server_info()
            for (k, v) in r.items():
                ret[k].append(v)
        return ret

    # returns true if the server is running, false if not. Note, this relies on
    # servers being shut down through the `servers_stop` interface. If a server
    # crashes, or is started or stopped through other means, an incorrect status
    # may be reported.
    def get_running_status(self) -> List[bool]:
        return [
            i in self.running_server_indexes for i in range(len(self.clients))
        ]

    def is_running(self, index: int) -> bool:
        return index in self.running_server_indexes

    def wait_for_validated_ledger(self, server_index: Optional[int] = None):
        '''
        Don't return until the network has at least one validated ledger
        '''

        if server_index is None:
            for i in range(len(self.configs)):
                self.wait_for_validated_ledger(i)
            return

        client = self.clients[server_index]
        for i in range(600):
            r = client.send_command(ServerInfo())
            state = None
            if 'info' in r:
                state = r['info']['server_state']
                if state == 'proposing':
                    print(f'Synced: {server_index} : {state}', flush=True)
                    break
            if not i % 10:
                print(f'Waiting for sync: {server_index} : {state}',
                      flush=True)
            time.sleep(1)

        for i in range(600):
            r = client.send_command(ServerInfo())
            state = None
            if 'info' in r:
                complete_ledgers = r['info']['complete_ledgers']
                if complete_ledgers and complete_ledgers != 'empty':
                    print(f'Have complete ledgers: {server_index} : {state}',
                          flush=True)
                    return
            if not i % 10:
                print(
                    f'Waiting for complete_ledgers: {server_index} : {complete_ledgers}',
                    flush=True)
            time.sleep(1)

        raise ValueError('Could not sync server {client.config_file_name}')

    def servers_start(self,
                      server_indexes: Optional[Union[Set[int],
                                                     List[int]]] = None,
                      *,
                      extra_args: Optional[List[List[str]]] = None):
        if server_indexes is None:
            server_indexes = [i for i in range(len(self.clients))]

        if extra_args is None:
            extra_args = []
        extra_args += [list()] * (len(self.configs) - len(extra_args))

        for i in server_indexes:
            if i in self.running_server_indexes or not self.run_server[i]:
                continue

            client = self.clients[i]
            to_run = [client.exe, '--conf', client.config_file_name]
            print(f'Starting server {client.config_file_name}')
            fout = open(os.devnull, 'w')
            p = subprocess.Popen(to_run + extra_args[i],
                                 stdout=fout,
                                 stderr=subprocess.STDOUT)
            client.set_pid(p.pid)
            print(
                f'started rippled: config: {client.config_file_name} PID: {p.pid}',
                flush=True)
            self.running_server_indexes.add(i)
            self.processes[i] = p

        time.sleep(2)  # give servers time to start

    def servers_stop(self,
                     server_indexes: Optional[Union[Set[int],
                                                    List[int]]] = None):
        if server_indexes is None:
            server_indexes = self.running_server_indexes.copy()

        if 0 in server_indexes:
            print(
                f'WARNING: Server 0 is being stopped. RPC commands cannot be sent until this is restarted.'
            )

        for i in server_indexes:
            if i not in self.running_server_indexes:
                continue
            client = self.clients[i]
            to_run = [client.exe, '--conf', client.config_file_name]
            fout = open(os.devnull, 'w')
            subprocess.Popen(to_run + ['stop'],
                             stdout=fout,
                             stderr=subprocess.STDOUT)
            self.running_server_indexes.discard(i)

        for i in server_indexes:
            self.processes[i].wait()
            del self.processes[i]
            self.get_client(i).set_pid(-1)
