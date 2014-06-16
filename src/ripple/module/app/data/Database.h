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

#ifndef RIPPLE_DATABASE_H_INCLUDED
#define RIPPLE_DATABASE_H_INCLUDED

namespace ripple {

// VFALCO Get rid of these macros
//
#define SQL_FOREACH(_db, _strQuery)     \
    if ((_db)->executeSQL(_strQuery))   \
        for (bool _bMore = (_db)->startIterRows(); _bMore; _bMore = (_db)->getNextRow())

#define SQL_EXISTS(_db, _strQuery)     \
    ((_db)->executeSQL(_strQuery) && (_db)->startIterRows())

/*
    this maintains the connection to the database
*/

class SqliteDatabase;
class JobQueue;

class Database
{
public:
    explicit Database (const char* host);

    virtual ~Database ();

    virtual void connect () = 0;

    virtual void disconnect () = 0;

    // returns true if the query went ok
    virtual bool executeSQL (const char* sql, bool fail_okay = false) = 0;

    bool executeSQL (std::string strSql, bool fail_okay = false)
    {
        return executeSQL (strSql.c_str (), fail_okay);
    }

    // returns false if there are no results
    virtual bool startIterRows (bool finalize = true) = 0;
    virtual void endIterRows () = 0;

    // call this after you executeSQL
    // will return false if there are no more rows
    virtual bool getNextRow (bool finalize = true) = 0;

    // get Data from the current row
    bool getNull (const char* colName);
    char* getStr (const char* colName, std::string& retStr);
    std::string getStrBinary (const std::string& strColName);
    std::int32_t getInt (const char* colName);
    float getFloat (const char* colName);
    bool getBool (const char* colName);

    // returns amount stored in buf
    int getBinary (const char* colName, unsigned char* buf, int maxSize);
    Blob getBinary (const std::string& strColName);

    std::uint64_t getBigInt (const char* colName);

    virtual bool getNull (int colIndex) = 0;
    virtual char* getStr (int colIndex, std::string& retStr) = 0;
    virtual std::int32_t getInt (int colIndex) = 0;
    virtual float getFloat (int colIndex) = 0;
    virtual bool getBool (int colIndex) = 0;
    virtual int getBinary (int colIndex, unsigned char* buf, int maxSize) = 0;
    virtual std::uint64_t getBigInt (int colIndex) = 0;
    virtual Blob getBinary (int colIndex) = 0;

    // int getSingleDBValueInt(const char* sql);
    // float getSingleDBValueFloat(const char* sql);
    // char* getSingleDBValueStr(const char* sql, std::string& retStr);

    // VFALCO TODO Make this parameter a reference instead of a pointer.
    virtual bool setupCheckpointing (JobQueue*)
    {
        return false;
    }
    virtual SqliteDatabase* getSqliteDB ()
    {
        return nullptr;
    }
    virtual int getKBUsedAll ()
    {
        return -1;
    }
    virtual int getKBUsedDB ()
    {
        return -1;
    }

protected:
    bool getColNumber (const char* colName, int* retIndex);

    int mNumCol;
    std::string mHost;
    std::vector <std::string> mColNameTable;
};

} // ripple

#endif
