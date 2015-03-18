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

#ifndef RIPPLE_APP_DATA_SQLITEDATABASE_H_INCLUDED
#define RIPPLE_APP_DATA_SQLITEDATABASE_H_INCLUDED

#include <ripple/app/data/Database.h>
#include <ripple/basics/Blob.h>
#include <ripple/core/JobQueue.h>
#include <sqlite/sqlite.h>
#include <beast/threads/Thread.h>
#include <mutex>

namespace ripple {

class SqliteDatabase
    : public Database
    , private beast::Thread
{
public:
    explicit SqliteDatabase (char const* host);
    ~SqliteDatabase ();

    void connect ();
    void disconnect ();

    // returns true if the query went ok
    bool executeSQL (const char* sql, bool fail_okay);

    // tells you how many rows were changed by an update or insert
    int getNumRowsAffected ();

    // returns false if there are no results
    bool startIterRows (bool finalize);
    void endIterRows ();

    // call this after you executeSQL
    // will return false if there are no more rows
    bool getNextRow (bool finalize);

    bool getNull (int colIndex);
    char* getStr (int colIndex, std::string& retStr);
    std::int32_t getInt (int colIndex);
    float getFloat (int colIndex);
    bool getBool (int colIndex);
    // returns amount stored in buf
    int getBinary (int colIndex, unsigned char* buf, int maxSize);
    Blob getBinary (int colIndex);
    std::uint64_t getBigInt (int colIndex);

    sqlite3* peekConnection ()
    {
        return mConnection;
    }
    virtual bool setupCheckpointing (JobQueue*);
    virtual SqliteDatabase* getSqliteDB ()
    {
        return this;
    }

    void doHook (const char* db, int walSize);

    int getKBUsedDB ();
    int getKBUsedAll ();

private:

    void run ();
    void runWal ();

    typedef std::mutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType m_walMutex;

    sqlite3* mConnection;
    sqlite3_stmt* mCurrentStmt;
    bool mMoreRows;

    JobQueue*               mWalQ;
    bool                    walRunning;
};

//------------------------------------------------------------------------------

class SqliteStatement
{
private:
    SqliteStatement (const SqliteStatement&);               // no implementation
    SqliteStatement& operator= (const SqliteStatement&);    // no implementation

protected:
    sqlite3_stmt* statement;

public:
    SqliteStatement (SqliteDatabase* db, const char* statement);
    SqliteStatement (SqliteDatabase* db, std::string const& statement);
    ~SqliteStatement ();

    sqlite3_stmt* peekStatement ();

    // positions start at 1
    int bind (int position, const void* data, int length);
    int bindStatic (int position, const void* data, int length);
    int bindStatic (int position, Blob const& value);

    int bind (int position, std::string const& value);
    int bindStatic (int position, std::string const& value);

    int bind (int position, std::uint32_t value);
    int bind (int position);

    // columns start at 0
    int size (int column);

    const void* peekBlob (int column);
    Blob getBlob (int column);

    std::string getString (int column);
    const char* peekString (int column);
    std::uint32_t getUInt32 (int column);
    std::int64_t getInt64 (int column);

    int step ();
    int reset ();

    // translate return values of step and reset
    bool isOk (int);
    bool isDone (int);
    bool isRow (int);
    bool isError (int);
    std::string getError (int);
};

} // ripple

#endif
