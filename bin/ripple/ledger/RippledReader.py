from __future__ import absolute_import, division, print_function, unicode_literals

import json
import os
import subprocess

from ripple.ledger.Args import ARGS
from ripple.util import File
from ripple.util import Range

_ERROR_CODE_REASON = {
    62: 'No rippled server is running.',
}

_DEFAULT_ERROR_ = "Couldn't connect to server."

class RippledReader(object):
    def __init__(self):
        fname = File.normalize(ARGS.rippled)
        if not os.path.exists(fname):
            raise Exception('No rippled found at %s.' % fname)
        self.cmd = [fname]
        if ARGS.config:
            self.cmd.extend(['--conf', _normalize(ARGS.config)])
        self.info = self._command('server_info')['info']
        c = self.info.get('complete_ledgers')
        if c == 'empty':
            self.complete = []
        else:
            self.complete = sorted(Range.from_string(c))

    def name_to_ledger_index(self, ledger_name, is_full=False):
        return self.get_ledger(ledger_name, is_full)['ledger_index']

    def get_ledger(self, name, is_full=False):
        cmd = ['ledger', str(name)]
        if is_full:
            cmd.append('full')
        return self._command(*cmd)['ledger']

    def _command(self, *cmds):
        cmd = self.cmd + list(cmds)
        try:
            data = subprocess.check_output(cmd, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as e:
            raise Exception(_ERROR_CODE_REASON.get(
                e.returncode, _DEFAULT_ERROR_))

        part = json.loads(data)
        try:
            return part['result']
        except:
            raise ValueError(part.get('error', 'unknown error'))
