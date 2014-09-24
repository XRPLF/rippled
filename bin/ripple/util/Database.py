from __future__ import absolute_import, division, print_function, unicode_literals

import sqlite3

LEDGER_QUERY = """
SELECT
     L.*, count(1) validations
FROM
    (select LedgerHash, LedgerSeq from Ledgers
     ORDER BY LedgerSeq DESC LIMIT 10000) L
    JOIN Validations V
    ON (V.LedgerHash = L.LedgerHash)
    GROUP BY L.LedgerHash
    HAVING validations >= {validations}
    ORDER BY 2 DESC
    LIMIT 1;
"""

def fetchall_query(database, query, **kwds):
    conn = sqlite3.connect(database)
    try:
        return conn.execute(query.format(**kwds)).fetchall()

    finally:
        conn.close()
