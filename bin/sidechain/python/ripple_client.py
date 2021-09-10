import asyncio
import datetime
import json
import os
from os.path import expanduser
import subprocess
import sys
from typing import Callable, List, Optional, Union
import time
import websockets

from command import Command, ServerInfo, SubscriptionCommand
from common import eprint
from config_file import ConfigFile


class RippleClient:
    '''Client to send commands to the rippled server'''
    def __init__(self,
                 *,
                 config: ConfigFile,
                 exe: str,
                 command_log: Optional[str] = None):
        self.config = config
        self.exe = exe
        self.command_log = command_log
        section = config.port_ws_admin_local
        self.websocket_uri = f'{section.protocol}://{section.ip}:{section.port}'
        self.subscription_websockets = []
        self.tasks = []
        self.pid = None
        if command_log:
            with open(self.command_log, 'w') as f:
                f.write(f'# Start \n')

    @property
    def config_file_name(self):
        return self.config.get_file_name()

    def shutdown(self):
        try:
            group = asyncio.gather(*self.tasks, return_exceptions=True)
            group.cancel()
            asyncio.get_event_loop().run_until_complete(group)
            for ws in self.subscription_websockets:
                asyncio.get_event_loop().run_until_complete(ws.close())
        except asyncio.CancelledError:
            pass

    def set_pid(self, pid: int):
        self.pid = pid

    def get_pid(self) -> Optional[int]:
        return self.pid

    def get_config(self) -> ConfigFile:
        return self.config

    # Get a dict of the server_state, validated_ledger_seq, and complete_ledgers
    def get_brief_server_info(self) -> dict:
        ret = {
            'server_state': 'NA',
            'ledger_seq': 'NA',
            'complete_ledgers': 'NA'
        }
        if not self.pid or self.pid == -1:
            return ret
        r = self.send_command(ServerInfo())
        if 'info' not in r:
            return ret
        r = r['info']
        for f in ['server_state', 'complete_ledgers']:
            if f in r:
                ret[f] = r[f]
        if 'validated_ledger' in r:
            ret['ledger_seq'] = r['validated_ledger']['seq']
        return ret

    def _write_command_log_command(self, cmd: str, cmd_index: int) -> None:
        if not self.command_log:
            return
        with open(self.command_log, 'a') as f:
            f.write(f'\n\n# command {cmd_index}\n')
            f.write(f'{cmd}')

    def _write_command_log_result(self, result: str, cmd_index: int) -> None:
        if not self.command_log:
            return
        with open(self.command_log, 'a') as f:
            f.write(f'\n\n# result {cmd_index}\n')
            f.write(f'{result}')

    def _send_command_line_command(self, cmd_id: int, *args) -> dict:
        '''Send the command to the rippled server using the command line interface'''
        to_run = [self.exe, '-q', '--conf', self.config_file_name, '--']
        to_run.extend(args)
        self._write_command_log_command(to_run, cmd_id)
        max_retries = 4
        for retry_count in range(0, max_retries + 1):
            try:
                r = subprocess.check_output(to_run)
                self._write_command_log_result(r, cmd_id)
                return json.loads(r.decode('utf-8'))['result']
            except Exception as e:
                if retry_count == max_retries:
                    raise
                eprint(
                    f'Got exception: {str(e)}\nretrying..{retry_count+1} of {max_retries}'
                )
                time.sleep(1)  # give process time to startup

    async def _send_websock_command(
            self,
            cmd: Command,
            conn: Optional[websockets.client.Connect] = None) -> dict:
        assert self.websocket_uri
        if conn is None:
            async with websockets.connect(self.websocket_uri) as ws:
                return await self._send_websock_command(cmd, ws)

        to_send = json.dumps(cmd.get_websocket_dict())
        self._write_command_log_command(to_send, cmd.cmd_id)
        await conn.send(to_send)
        r = await conn.recv()
        self._write_command_log_result(r, cmd.cmd_id)
        j = json.loads(r)
        if not 'result' in j:
            eprint(
                f'Error sending websocket command: {json.dumps(cmd.get_websocket_dict(), indent=1)}'
            )
            eprint(f'Result: {json.dumps(j, indent=1)}')
            raise ValueError('Error sending websocket command')
        return j['result']

    def send_command(self, cmd: Command) -> dict:
        '''Send the command to the rippled server'''
        if self.websocket_uri:
            return asyncio.get_event_loop().run_until_complete(
                self._send_websock_command(cmd))
        return self._send_command_line_command(cmd.cmd_id,
                                               *cmd.get_command_line_list())

    # Need async version to close ledgers from async functions
    async def async_send_command(self, cmd: Command) -> dict:
        '''Send the command to the rippled server'''
        if self.websocket_uri:
            return await self._send_websock_command(cmd)
        return self._send_command_line_command(cmd.cmd_id,
                                               *cmd.get_command_line_list())

    def send_subscribe_command(
            self,
            cmd: SubscriptionCommand,
            callback: Optional[Callable[[dict], None]] = None) -> dict:
        '''Send the command to the rippled server'''
        assert self.websocket_uri
        ws = cmd.websocket
        if ws is None:
            # subscribe
            assert callback
            ws = asyncio.get_event_loop().run_until_complete(
                websockets.connect(self.websocket_uri))
            self.subscription_websockets.append(ws)
        result = asyncio.get_event_loop().run_until_complete(
            self._send_websock_command(cmd, ws))
        if cmd.websocket is not None:
            # unsubscribed. close the websocket
            self.subscription_websockets.remove(cmd.websocket)
            cmd.websocket.close()
            cmd.websocket = None
        else:
            # setup a task to read the websocket
            cmd.websocket = ws  # must be set after the _send_websock_command or will unsubscribe

            async def subscribe_callback(ws: websockets.client.Connect,
                                         cb: Callable[[dict], None]):
                while True:
                    r = await ws.recv()
                    d = json.loads(r)
                    cb(d)

            task = asyncio.get_event_loop().create_task(
                subscribe_callback(cmd.websocket, callback))
            self.tasks.append(task)
        return result

    def stop(self):
        '''Stop the server'''
        return self.send_command(Stop())

    def set_log_level(self, severity: str, *, partition: Optional[str] = None):
        '''Set the server log level'''
        return self.send_command(LogLevel(severity, parition=parition))
