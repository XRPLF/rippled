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

Database::Database (const char* host)
    : mNumCol (0)
{
    mHost   = host;
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

    return nullptr;
}

std::int32_t Database::getInt (const char* colName)
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

std::uint64_t Database::getBigInt (const char* colName)
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

} // ripple
