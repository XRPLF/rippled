//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_DATABASE_RIPPLEHEADER
#define RIPPLE_DATABASE_RIPPLEHEADER

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
    int32 getInt (const char* colName);
    float getFloat (const char* colName);
    bool getBool (const char* colName);

    // returns amount stored in buf
    int getBinary (const char* colName, unsigned char* buf, int maxSize);
    Blob getBinary (const std::string& strColName);

    uint64 getBigInt (const char* colName);

    virtual bool getNull (int colIndex) = 0;
    virtual char* getStr (int colIndex, std::string& retStr) = 0;
    virtual int32 getInt (int colIndex) = 0;
    virtual float getFloat (int colIndex) = 0;
    virtual bool getBool (int colIndex) = 0;
    virtual int getBinary (int colIndex, unsigned char* buf, int maxSize) = 0;
    virtual uint64 getBigInt (int colIndex) = 0;
    virtual Blob getBinary (int colIndex) = 0;

    // int getSingleDBValueInt(const char* sql);
    // float getSingleDBValueFloat(const char* sql);
    // char* getSingleDBValueStr(const char* sql, std::string& retStr);

    virtual bool setupCheckpointing (JobQueue*)
    {
        return false;
    }
    virtual SqliteDatabase* getSqliteDB ()
    {
        return NULL;
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

#endif

// vim:ts=4
