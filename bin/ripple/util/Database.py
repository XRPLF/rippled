from __future__ import absolute_import, division, print_function, unicode_literals

import sqlite3

def fetchall(database, query, kwds):
    conn = sqlite3.connect(database)
    try:
        cursor = conn.execute(query, kwds)
        return cursor.fetchall()

    finally:
        conn.close()
