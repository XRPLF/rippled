// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define SOCI_ODBC_SOURCE
#include <soci-platform.h>
#include "soci-odbc.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace soci;
using namespace soci::details;

#ifdef _MSC_VER
#pragma warning(disable:4996)
#define snprintf _snprintf
#endif


void* odbc_standard_use_type_backend::prepare_for_bind(
    SQLLEN &size, SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    switch (type_)
    {
    // simple cases
    case x_short:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_SSHORT;
        size = sizeof(short);
        break;
    case x_integer:
        sqlType = SQL_INTEGER;
        cType = SQL_C_SLONG;
        size = sizeof(int);
        break;
    case x_long_long:
        if (use_string_for_bigint())
        {
          sqlType = SQL_NUMERIC;
          cType = SQL_C_CHAR;
          size = max_bigint_length;
          buf_ = new char[size];
          snprintf(buf_, size, "%" LL_FMT_FLAGS "d",
                   *static_cast<long long *>(data_));
          indHolder_ = SQL_NTS;
        }
        else // Normal case, use ODBC support.
        {
          sqlType = SQL_BIGINT;
          cType = SQL_C_SBIGINT;
          size = sizeof(long long);
        }
        break;
    case x_unsigned_long_long:
        if (use_string_for_bigint())
        {
          sqlType = SQL_NUMERIC;
          cType = SQL_C_CHAR;
          size = max_bigint_length;
          buf_ = new char[size];
          snprintf(buf_, size, "%" LL_FMT_FLAGS "u",
                   *static_cast<unsigned long long *>(data_));
          indHolder_ = SQL_NTS;
        }
        else // Normal case, use ODBC support.
        {
          sqlType = SQL_BIGINT;
          cType = SQL_C_UBIGINT;
          size = sizeof(unsigned long long);
        }
        break;
    case x_double:
        sqlType = SQL_DOUBLE;
        cType = SQL_C_DOUBLE;
        size = sizeof(double);
        break;

    case x_char:
        sqlType = SQL_CHAR;
        cType = SQL_C_CHAR;
        size = 2;
        buf_ = new char[size];
        buf_[0] = *static_cast<char*>(data_);
        buf_[1] = '\0';
        indHolder_ = SQL_NTS;
        break;
    case x_stdstring:
    {
        std::string* s = static_cast<std::string*>(data_);
        sqlType = SQL_VARCHAR;
        cType = SQL_C_CHAR;
        size = s->size();
        buf_ = new char[size+1];
        memcpy(buf_, s->c_str(), size);
        buf_[size++] = '\0';
        indHolder_ = SQL_NTS;
    }
    break;
    case x_stdtm:
    {
        std::tm *t = static_cast<std::tm *>(data_);

        sqlType = SQL_TIMESTAMP;
        cType = SQL_C_TIMESTAMP;
        buf_ = new char[sizeof(TIMESTAMP_STRUCT)];
        size = 19; // This number is not the size in bytes, but the number
                   // of characters in the date if it was written out
                   // yyyy-mm-dd hh:mm:ss

        TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(buf_);

        ts->year = static_cast<SQLSMALLINT>(t->tm_year + 1900);
        ts->month = static_cast<SQLUSMALLINT>(t->tm_mon + 1);
        ts->day = static_cast<SQLUSMALLINT>(t->tm_mday);
        ts->hour = static_cast<SQLUSMALLINT>(t->tm_hour);
        ts->minute = static_cast<SQLUSMALLINT>(t->tm_min);
        ts->second = static_cast<SQLUSMALLINT>(t->tm_sec);
        ts->fraction = 0;
    }
    break;

    case x_blob:
    {
//         sqlType = SQL_VARBINARY;
//         cType = SQL_C_BINARY;

//         BLOB *b = static_cast<BLOB *>(data);

//         odbc_blob_backend *bbe
//         = static_cast<odbc_blob_backend *>(b->getBackEnd());

//         size = 0;
//         indHolder_ = size;
        //TODO            data = &bbe->lobp_;
    }
    break;
    case x_statement:
    case x_rowid:
        // Unsupported data types.
        return NULL;
    }

    // Return either the pointer to C++ data itself or the buffer that we
    // allocated, if any.
    return buf_ ? buf_ : data_;
}

void odbc_standard_use_type_backend::bind_by_pos(
    int &position, void *data, exchange_type type, bool /* readOnly */)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    position_ = position++;
    data_ = data;
    type_ = type;

    statement_.boundByPos_ = true;
}

void odbc_standard_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type, bool /* readOnly */)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    int position = -1;
    int count = 1;

    for (std::vector<std::string>::iterator it = statement_.names_.begin();
         it != statement_.names_.end(); ++it)
    {
        if (*it == name)
        {
            position = count;
            break;
        }
        count++;
    }

    if (position == -1)
    {
        std::ostringstream ss;
        ss << "Unable to find name '" << name << "' to bind to";
        throw soci_error(ss.str().c_str());
    }

    position_ = position;
    data_ = data;
    type_ = type;

    statement_.boundByName_ = true;
}

void odbc_standard_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    SQLSMALLINT sqlType;
    SQLSMALLINT cType;
    SQLLEN size;

    void* const sqlData = prepare_for_bind(size, sqlType, cType);

    SQLRETURN rc = SQLBindParameter(statement_.hstmt_,
                                    static_cast<SQLUSMALLINT>(position_),
                                    SQL_PARAM_INPUT,
                                    cType, sqlType, size, 0,
                                    sqlData, 0, &indHolder_);

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, statement_.hstmt_,
                                "Binding");
    }

    // then handle indicators
    if (ind != NULL && *ind == i_null)
    {
        indHolder_ = SQL_NULL_DATA; // null
    }
}

void odbc_standard_use_type_backend::post_use(bool gotData, indicator *ind)
{
    if (ind != NULL)
    {
        if (gotData)
        {
            if (indHolder_ == 0)
            {
                *ind = i_ok;
            }
            else if (indHolder_ == SQL_NULL_DATA)
            {
                *ind = i_null;
            }
            else
            {
                *ind = i_truncated;
            }
        }
    }
}

void odbc_standard_use_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
