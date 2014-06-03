//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {

SETUP_LOG (SqliteDatabase)

SqliteStatement::SqliteStatement (SqliteDatabase* db, const char* sql, bool aux)
{
    assert (db);

    sqlite3* conn = aux ? db->getAuxConnection () : db->peekConnection ();
    int j = sqlite3_prepare_v2 (conn, sql, strlen (sql) + 1, &statement, nullptr);

    if (j != SQLITE_OK)
        throw j;
}

SqliteStatement::SqliteStatement (SqliteDatabase* db, const std::string& sql, bool aux)
{
    assert (db);

    sqlite3* conn = aux ? db->getAuxConnection () : db->peekConnection ();
    int j = sqlite3_prepare_v2 (conn, sql.c_str (), sql.size () + 1, &statement, nullptr);

    if (j != SQLITE_OK)
        throw j;
}

SqliteStatement::~SqliteStatement ()
{
    sqlite3_finalize (statement);
}

//------------------------------------------------------------------------------

SqliteDatabase::SqliteDatabase (const char* host)
    : Database (host)
    , Thread ("sqlitedb")
    , mWalQ (nullptr)
    , walRunning (false)
{
    startThread ();

    mConnection     = nullptr;
    mAuxConnection  = nullptr;
    mCurrentStmt    = nullptr;
}

SqliteDatabase::~SqliteDatabase ()
{
    // Blocks until the thread exits in an orderly fashion
    stopThread ();
}

void SqliteDatabase::connect ()
{
    int rc = sqlite3_open_v2 (mHost.c_str (), &mConnection,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);

    if (rc)
    {
        WriteLog (lsFATAL, SqliteDatabase) << "Can't open " << mHost << " " << rc;
        sqlite3_close (mConnection);
        assert ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
    }
}

sqlite3* SqliteDatabase::getAuxConnection ()
{
    ScopedLockType sl (m_walMutex);

    if (mAuxConnection == nullptr)
    {
        int rc = sqlite3_open_v2 (mHost.c_str (), &mAuxConnection,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);

        if (rc)
        {
            WriteLog (lsFATAL, SqliteDatabase) << "Can't aux open " << mHost << " " << rc;
            assert ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));

            if (mAuxConnection != nullptr)
            {
                sqlite3_close (mConnection);
                mAuxConnection = nullptr;
            }
        }
    }

    return mAuxConnection;
}

void SqliteDatabase::disconnect ()
{
    sqlite3_finalize (mCurrentStmt);
    sqlite3_close (mConnection);

    if (mAuxConnection != nullptr)
        sqlite3_close (mAuxConnection);
}

// returns true if the query went ok
bool SqliteDatabase::executeSQL (const char* sql, bool fail_ok)
{
#ifdef DEBUG_HANGING_LOCKS
    assert (fail_ok || (mCurrentStmt == nullptr));
#endif

    sqlite3_finalize (mCurrentStmt);

    int rc = sqlite3_prepare_v2 (mConnection, sql, -1, &mCurrentStmt, nullptr);

    if (SQLITE_OK != rc)
    {
        if (!fail_ok)
        {
#ifdef BEAST_DEBUG
            WriteLog (lsWARNING, SqliteDatabase) << "Perror:" << mHost << ": " << rc;
            WriteLog (lsWARNING, SqliteDatabase) << "Statement: " << sql;
            WriteLog (lsWARNING, SqliteDatabase) << "Error: " << sqlite3_errmsg (mConnection);
#endif
        }

        endIterRows ();
        return false;
    }

    rc = sqlite3_step (mCurrentStmt);

    if (rc == SQLITE_ROW)
    {
        mMoreRows = true;
    }
    else if (rc == SQLITE_DONE)
    {
        endIterRows ();
        mMoreRows = false;
    }
    else
    {
        if ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED))
        {
            WriteLog (lsFATAL, SqliteDatabase) << mHost  << " returns error " << rc << ": " << sqlite3_errmsg (mConnection);
            assert (false);
        }

        mMoreRows = false;

        if (!fail_ok)
        {
#ifdef BEAST_DEBUG
            WriteLog (lsWARNING, SqliteDatabase) << "SQL Serror:" << mHost << ": " << rc;
            WriteLog (lsWARNING, SqliteDatabase) << "Statement: " << sql;
            WriteLog (lsWARNING, SqliteDatabase) << "Error: " << sqlite3_errmsg (mConnection);
#endif
        }

        endIterRows ();
        return false;
    }

    return true;
}

// returns false if there are no results
bool SqliteDatabase::startIterRows (bool finalize)
{
    mColNameTable.clear ();
    mColNameTable.resize (sqlite3_column_count (mCurrentStmt));

    for (unsigned n = 0; n < mColNameTable.size (); n++)
    {
        mColNameTable[n] = sqlite3_column_name (mCurrentStmt, n);
    }

    if (!mMoreRows && finalize)
        endIterRows ();

    return (mMoreRows);
}

void SqliteDatabase::endIterRows ()
{
    sqlite3_finalize (mCurrentStmt);
    mCurrentStmt = nullptr;
}

// call this after you executeSQL
// will return false if there are no more rows
bool SqliteDatabase::getNextRow (bool finalize)
{
    if (mMoreRows)
    {
        int rc = sqlite3_step (mCurrentStmt);

        if (rc == SQLITE_ROW)
            return (true);

        assert ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
        CondLog ((rc != SQLITE_DONE), lsWARNING, SqliteDatabase) << "Rerror: " << mHost << ": " << rc;
    }

    if (finalize)
        endIterRows ();

    return false;
}

bool SqliteDatabase::getNull (int colIndex)
{
    return (SQLITE_NULL == sqlite3_column_type (mCurrentStmt, colIndex));
}

char* SqliteDatabase::getStr (int colIndex, std::string& retStr)
{
    const char* text = reinterpret_cast<const char*> (sqlite3_column_text (mCurrentStmt, colIndex));
    retStr = (text == nullptr) ? "" : text;
    return const_cast<char*> (retStr.c_str ());
}

std::int32_t SqliteDatabase::getInt (int colIndex)
{
    return (sqlite3_column_int (mCurrentStmt, colIndex));
}

float SqliteDatabase::getFloat (int colIndex)
{
    return (static_cast <float> (sqlite3_column_double (mCurrentStmt, colIndex)));
}

bool SqliteDatabase::getBool (int colIndex)
{
    return (sqlite3_column_int (mCurrentStmt, colIndex) ? true : false);
}

int SqliteDatabase::getBinary (int colIndex, unsigned char* buf, int maxSize)
{
    const void* blob = sqlite3_column_blob (mCurrentStmt, colIndex);
    int size = sqlite3_column_bytes (mCurrentStmt, colIndex);

    if (size < maxSize) maxSize = size;

    memcpy (buf, blob, maxSize);
    return (size);
}

Blob SqliteDatabase::getBinary (int colIndex)
{
    const unsigned char*        blob    = reinterpret_cast<const unsigned char*> (sqlite3_column_blob (mCurrentStmt, colIndex));
    size_t                      iSize   = sqlite3_column_bytes (mCurrentStmt, colIndex);
    Blob    vucResult;

    vucResult.resize (iSize);
    std::copy (blob, blob + iSize, vucResult.begin ());

    return vucResult;
}

std::uint64_t SqliteDatabase::getBigInt (int colIndex)
{
    return (sqlite3_column_int64 (mCurrentStmt, colIndex));
}

int SqliteDatabase::getKBUsedAll ()
{
    return static_cast<int> (sqlite3_memory_used () / 1024);
}

int SqliteDatabase::getKBUsedDB ()
{
    int cur = 0, hiw = 0;
    sqlite3_db_status (mConnection, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
    return cur / 1024;
}

static int SqliteWALHook (void* s, sqlite3* dbCon, const char* dbName, int walSize)
{
    (reinterpret_cast<SqliteDatabase*> (s))->doHook (dbName, walSize);
    return SQLITE_OK;
}

bool SqliteDatabase::setupCheckpointing (JobQueue* q)
{
    mWalQ = q;
    sqlite3_wal_hook (mConnection, SqliteWALHook, this);
    return true;
}

void SqliteDatabase::doHook (const char* db, int pages)
{
    if (pages < 1000)
        return;

    {
        ScopedLockType sl (m_walMutex);

        if (walRunning)
            return;

        walRunning = true;
    }

    if (mWalQ)
    {
        mWalQ->addJob (jtWAL, std::string ("WAL:") + mHost, std::bind (&SqliteDatabase::runWal, this));
    }
    else
    {
        notify();
    }
}

void SqliteDatabase::run ()
{
    // Simple thread loop runs Wal every time it wakes up via
    // the call to Thread::notify, unless Thread::threadShouldExit returns
    // true in which case we simply break.
    //
    for (;;)
    {
        wait ();
        if (threadShouldExit())
            break;
        runWal();
    }
}

void SqliteDatabase::runWal ()
{
    int log = 0, ckpt = 0;
    int ret = sqlite3_wal_checkpoint_v2 (mConnection, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

    if (ret != SQLITE_OK)
    {
        WriteLog ((ret == SQLITE_LOCKED) ? lsTRACE : lsWARNING, SqliteDatabase) << "WAL("
                << sqlite3_db_filename (mConnection, "main") << "): error " << ret;
    }
    else
        WriteLog (lsTRACE, SqliteDatabase) << "WAL(" << sqlite3_db_filename (mConnection, "main") <<
                                           "): frames=" << log << ", written=" << ckpt;

    {
        ScopedLockType sl (m_walMutex);
        walRunning = false;
    }
}

sqlite3_stmt* SqliteStatement::peekStatement ()
{
    return statement;
}

int SqliteStatement::bind (int position, const void* data, int length)
{
    return sqlite3_bind_blob (statement, position, data, length, SQLITE_TRANSIENT);
}

int SqliteStatement::bindStatic (int position, const void* data, int length)
{
    return sqlite3_bind_blob (statement, position, data, length, SQLITE_STATIC);
}

int SqliteStatement::bindStatic (int position, Blob const& value)
{
    return sqlite3_bind_blob (statement, position, &value.front (), value.size (), SQLITE_STATIC);
}

int SqliteStatement::bind (int position, std::uint32_t value)
{
    return sqlite3_bind_int64 (statement, position, static_cast<sqlite3_int64> (value));
}

int SqliteStatement::bind (int position, const std::string& value)
{
    return sqlite3_bind_text (statement, position, value.data (), value.size (), SQLITE_TRANSIENT);
}

int SqliteStatement::bindStatic (int position, const std::string& value)
{
    return sqlite3_bind_text (statement, position, value.data (), value.size (), SQLITE_STATIC);
}

int SqliteStatement::bind (int position)
{
    return sqlite3_bind_null (statement, position);
}

int SqliteStatement::size (int column)
{
    return sqlite3_column_bytes (statement, column);
}

const void* SqliteStatement::peekBlob (int column)
{
    return sqlite3_column_blob (statement, column);
}

Blob SqliteStatement::getBlob (int column)
{
    int size = sqlite3_column_bytes (statement, column);
    Blob ret (size);
    memcpy (& (ret.front ()), sqlite3_column_blob (statement, column), size);
    return ret;
}

std::string SqliteStatement::getString (int column)
{
    return reinterpret_cast<const char*> (sqlite3_column_text (statement, column));
}

const char* SqliteStatement::peekString (int column)
{
    return reinterpret_cast<const char*> (sqlite3_column_text (statement, column));
}

std::uint32_t SqliteStatement::getUInt32 (int column)
{
    return static_cast<std::uint32_t> (sqlite3_column_int64 (statement, column));
}

std::int64_t SqliteStatement::getInt64 (int column)
{
    return sqlite3_column_int64 (statement, column);
}

int SqliteStatement::step ()
{
    return sqlite3_step (statement);
}

int SqliteStatement::reset ()
{
    return sqlite3_reset (statement);
}

bool SqliteStatement::isOk (int j)
{
    return j == SQLITE_OK;
}

bool SqliteStatement::isDone (int j)
{
    return j == SQLITE_DONE;
}

bool SqliteStatement::isRow (int j)
{
    return j == SQLITE_ROW;
}

bool SqliteStatement::isError (int j)
{
    switch (j)
    {
    case SQLITE_OK:
    case SQLITE_ROW:
    case SQLITE_DONE:
        return false;

    default:
        return true;
    }
}

std::string SqliteStatement::getError (int j)
{
    return sqlite3_errstr (j);
}

} // ripple
