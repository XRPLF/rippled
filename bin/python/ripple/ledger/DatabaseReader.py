from __future__ import absolute_import, division, print_function, unicode_literals

import json
import os
import subprocess

from ripple.ledger.Args import ARGS
from ripple.util import ConfigFile
from ripple.util import Database
from ripple.util import File
from ripple.util import Log
from ripple.util import Range

LEDGER_QUERY = """
SELECT
     L.*, count(1) validations
FROM
    (select LedgerHash, LedgerSeq from Ledgers ORDER BY LedgerSeq DESC) L
    JOIN Validations V
    ON (V.LedgerHash = L.LedgerHash)
    GROUP BY L.LedgerHash
    HAVING validations >= {validation_quorum}
    ORDER BY 2;
"""

COMPLETE_QUERY = """
SELECT
     L.LedgerSeq, count(*) validations
FROM
    (select LedgerHash, LedgerSeq from Ledgers ORDER BY LedgerSeq) L
    JOIN Validations V
    ON (V.LedgerHash = L.LedgerHash)
    GROUP BY L.LedgerHash
    HAVING validations >= :validation_quorum
    ORDER BY 2;
"""

_DATABASE_NAME = 'ledger.db'

USE_PLACEHOLDERS = False

class DatabaseReader(object):
    def __init__(self, config):
        assert ARGS.database != ARGS.NONE
        database = ARGS.database or config['database_path']
        if not database.endswith(_DATABASE_NAME):
            database = os.path.join(database, _DATABASE_NAME)
        if USE_PLACEHOLDERS:
            cursor = Database.fetchall(
                database, COMPLETE_QUERY, config)
        else:
            cursor = Database.fetchall(
                database, LEDGER_QUERY.format(**config), {})
        self.complete = [c[1] for c in cursor]

    def name_to_ledger_index(self, ledger_name, is_full=False):
        if not self.complete:
            return None
        if ledger_name == 'closed':
            return self.complete[-1]
        if ledger_name == 'current':
            return None
        if ledger_name == 'validated':
            return self.complete[-1]

    def get_ledger(self, name, is_full=False):
        cmd = ['ledger', str(name)]
        if is_full:
            cmd.append('full')
        response = self._command(*cmd)
        result = response.get('ledger')
        if result:
            return result
        error = response['error']
        etext = _ERROR_TEXT.get(error)
        if etext:
            error = '%s (%s)' % (etext, error)
        Log.fatal(_ERROR_TEXT.get(error, error))
