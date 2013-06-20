//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (SqliteDatabase)

using namespace std;

SqliteStatement::SqliteStatement (SqliteDatabase* db, const char* sql, bool aux)
{
    assert (db);

    sqlite3* conn = aux ? db->getAuxConnection () : db->peekConnection ();
    int j = sqlite3_prepare_v2 (conn, sql, strlen (sql) + 1, &statement, NULL);

    if (j != SQLITE_OK)
        throw j;
}

SqliteStatement::SqliteStatement (SqliteDatabase* db, const std::string& sql, bool aux)
{
    assert (db);

    sqlite3* conn = aux ? db->getAuxConnection () : db->peekConnection ();
    int j = sqlite3_prepare_v2 (conn, sql.c_str (), sql.size () + 1, &statement, NULL);

    if (j != SQLITE_OK)
        throw j;
}

SqliteStatement::~SqliteStatement ()
{
    sqlite3_finalize (statement);
}

SqliteDatabase::SqliteDatabase (const char* host) : Database (host, "", ""), mWalQ (NULL), walRunning (false)
{
    mConnection     = NULL;
    mAuxConnection  = NULL;
    mCurrentStmt    = NULL;
}

void SqliteDatabase::connect ()
{
    int rc = sqlite3_open (mHost.c_str (), &mConnection);

    if (rc)
    {
        WriteLog (lsFATAL, SqliteDatabase) << "Can't open " << mHost << " " << rc;
        sqlite3_close (mConnection);
        assert ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));
    }
}

sqlite3* SqliteDatabase::getAuxConnection ()
{
    boost::mutex::scoped_lock sl (walMutex);

    if (mAuxConnection == NULL)
    {
        int rc = sqlite3_open (mHost.c_str (), &mAuxConnection);

        if (rc)
        {
            WriteLog (lsFATAL, SqliteDatabase) << "Can't aux open " << mHost << " " << rc;
            assert ((rc != SQLITE_BUSY) && (rc != SQLITE_LOCKED));

            if (mAuxConnection != NULL)
            {
                sqlite3_close (mConnection);
                mAuxConnection = NULL;
            }
        }
    }

    return mAuxConnection;
}

void SqliteDatabase::disconnect ()
{
    sqlite3_finalize (mCurrentStmt);
    sqlite3_close (mConnection);

    if (mAuxConnection != NULL)
        sqlite3_close (mAuxConnection);
}

// returns true if the query went ok
bool SqliteDatabase::executeSQL (const char* sql, bool fail_ok)
{
#ifdef DEBUG_HANGING_LOCKS
    assert (fail_ok || (mCurrentStmt == NULL));
#endif

    sqlite3_finalize (mCurrentStmt);

    int rc = sqlite3_prepare_v2 (mConnection, sql, -1, &mCurrentStmt, NULL);

    if (SQLITE_OK != rc)
    {
        if (!fail_ok)
        {
#ifdef DEBUG
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
#ifdef DEBUG
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
    mCurrentStmt = NULL;
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
    retStr = (text == NULL) ? "" : text;
    return const_cast<char*> (retStr.c_str ());
}

int32 SqliteDatabase::getInt (int colIndex)
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

uint64 SqliteDatabase::getBigInt (int colIndex)
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
        boost::mutex::scoped_lock sl (walMutex);

        if (walRunning)
            return;

        walRunning = true;
    }

    if (mWalQ)
        mWalQ->addJob (jtWAL, std::string ("WAL:") + mHost, boost::bind (&SqliteDatabase::runWal, this));
    else
        boost::thread (boost::bind (&SqliteDatabase::runWal, this)).detach ();
}

void SqliteDatabase::runWal ()
{
    int log = 0, ckpt = 0;
    int ret = sqlite3_wal_checkpoint_v2 (mConnection, NULL, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

    if (ret != SQLITE_OK)
    {
        WriteLog ((ret == SQLITE_LOCKED) ? lsTRACE : lsWARNING, SqliteDatabase) << "WAL("
                << sqlite3_db_filename (mConnection, "main") << "): error " << ret;
    }
    else
        WriteLog (lsTRACE, SqliteDatabase) << "WAL(" << sqlite3_db_filename (mConnection, "main") <<
                                           "): frames=" << log << ", written=" << ckpt;

    {
        boost::mutex::scoped_lock sl (walMutex);
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

int SqliteStatement::bind (int position, uint32 value)
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

uint32 SqliteStatement::getUInt32 (int column)
{
    return static_cast<uint32> (sqlite3_column_int64 (statement, column));
}

int64 SqliteStatement::getInt64 (int column)
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
        return true;

    default:
        return false;
    }
}

std::string SqliteStatement::getError (int j)
{
    return sqlite3_errstr (j);
}

// vim:ts=4
