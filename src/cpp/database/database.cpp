//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "database.h"
#include <stdlib.h>
#include <string.h>

Database::Database (const char* host, const char* user, const char* pass) : mNumCol (0)
{
    mDBPass = pass;
    mHost   = host;
    mUser   = user;
}

Database::~Database ()
{
}

bool Database::getNull (const char* colName)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getNull (index);
    }

    return true;
}

char* Database::getStr (const char* colName, std::string& retStr)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getStr (index, retStr);
    }

    return NULL;
}

int32 Database::getInt (const char* colName)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getInt (index);
    }

    return 0;
}

float Database::getFloat (const char* colName)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getFloat (index);
    }

    return 0;
}

bool Database::getBool (const char* colName)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getBool (index);
    }

    return 0;
}

int Database::getBinary (const char* colName, unsigned char* buf, int maxSize)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return (getBinary (index, buf, maxSize));
    }

    return (0);
}

Blob Database::getBinary (const std::string& strColName)
{
    int index;

    if (getColNumber (strColName.c_str (), &index))
    {
        return getBinary (index);
    }

    return Blob ();
}

std::string Database::getStrBinary (const std::string& strColName)
{
    // YYY Could eliminate a copy if getStrBinary was a template.
    return strCopy (getBinary (strColName.c_str ()));
}

uint64 Database::getBigInt (const char* colName)
{
    int index;

    if (getColNumber (colName, &index))
    {
        return getBigInt (index);
    }

    return 0;
}

// returns false if can't find col
bool Database::getColNumber (const char* colName, int* retIndex)
{
    for (unsigned int n = 0; n < mColNameTable.size (); n++)
    {
        if (strcmp (colName, mColNameTable[n].c_str ()) == 0)
        {
            *retIndex = n;
            return (true);
        }
    }

    return false;
}

#if 0
int Database::getSingleDBValueInt (const char* sql)
{
    int ret;

    if ( executeSQL (sql) && startIterRows ()
{
    ret = getInt (0);
        endIterRows ();
    }
    else
    {
        //theUI->statusMsg("ERROR with database: %s",sql);
        ret = 0;
    }
    return (ret);
}
#endif

#if 0
float Database::getSingleDBValueFloat (const char* sql)
{
    float ret;

    if (executeSQL (sql) && startIterRows () && getNextRow ())
    {
        ret = getFloat (0);
        endIterRows ();
    }
    else
    {
        //theUI->statusMsg("ERROR with database: %s",sql);
        ret = 0;
    }

    return (ret);
}
#endif

#if 0
char* Database::getSingleDBValueStr (const char* sql, std::string& retStr)
{
    char* ret;

    if (executeSQL (sql) && startIterRows ())
    {
        ret = getStr (0, retStr);
        endIterRows ();
    }
    else
    {
        //theUI->statusMsg("ERROR with database: %s",sql);
        ret = 0;
    }

    return (ret);
}
#endif

// vim:ts=4
