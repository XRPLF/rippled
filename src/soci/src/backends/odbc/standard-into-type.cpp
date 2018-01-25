//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci/soci-platform.h"
#include "soci/odbc/soci-odbc.h"
#include "soci-exchange-cast.h"
#include "soci-mktime.h"
#include <ctime>
#include <stdio.h>  // sscanf()

using namespace soci;
using namespace soci::details;

void odbc_standard_into_type_backend::define_by_pos(
    int & position, void * data, exchange_type type)
{
    data_ = data;
    type_ = type;
    position_ = position++;

    SQLUINTEGER size = 0;
    switch (type_)
    {
    case x_char:
        odbcType_ = SQL_C_CHAR;
        size = sizeof(char) + 1;
        buf_ = new char[size];
        data = buf_;
        break;
    case x_stdstring:
    case x_longstring:
    case x_xmltype:
        odbcType_ = SQL_C_CHAR;
        // Patch: set to min between column size and 100MB (used ot be 32769)
        // Column size for text data type can be too large for buffer allocation
        size = static_cast<SQLUINTEGER>(statement_.column_size(position_));
        size = (size > odbc_max_buffer_length || size == 0) ? odbc_max_buffer_length : size;
        size++;
        buf_ = new char[size];
        data = buf_;
        break;
    case x_short:
        odbcType_ = SQL_C_SSHORT;
        size = sizeof(short);
        break;
    case x_integer:
        odbcType_ = SQL_C_SLONG;
        size = sizeof(int);
        break;
    case x_long_long:
        if (use_string_for_bigint())
        {
          odbcType_ = SQL_C_CHAR;
          size = max_bigint_length;
          buf_ = new char[size];
          data = buf_;
        }
        else // Normal case, use ODBC support.
        {
          odbcType_ = SQL_C_SBIGINT;
          size = sizeof(long long);
        }
        break;
    case x_unsigned_long_long:
        if (use_string_for_bigint())
        {
          odbcType_ = SQL_C_CHAR;
          size = max_bigint_length;
          buf_ = new char[size];
          data = buf_;
        }
        else // Normal case, use ODBC support.
        {
          odbcType_ = SQL_C_UBIGINT;
          size = sizeof(unsigned long long);
        }
        break;
    case x_double:
        odbcType_ = SQL_C_DOUBLE;
        size = sizeof(double);
        break;
    case x_stdtm:
        odbcType_ = SQL_C_TYPE_TIMESTAMP;
        size = sizeof(TIMESTAMP_STRUCT);
        buf_ = new char[size];
        data = buf_;
        break;
    case x_rowid:
        odbcType_ = SQL_C_ULONG;
        size = sizeof(unsigned long);
        break;
    default:
        throw soci_error("Into element used with non-supported type.");
    }

    valueLen_ = 0;

    SQLRETURN rc = SQLBindCol(statement_.hstmt_, static_cast<SQLUSMALLINT>(position_),
        static_cast<SQLUSMALLINT>(odbcType_), data, size, &valueLen_);
    if (is_odbc_error(rc))
    {
        std::ostringstream ss;
        ss << "binding output column #" << position_;
        throw odbc_soci_error(SQL_HANDLE_STMT, statement_.hstmt_, ss.str());
    }
}

void odbc_standard_into_type_backend::pre_fetch()
{
    //...
}

void odbc_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator * ind)
{
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to do anything (fetch() will return false)
        return;
    }

    if (gotData)
    {
        // first, deal with indicators
        if (SQL_NULL_DATA == valueLen_)
        {
            if (ind == NULL)
            {
                throw soci_error(
                    "Null value fetched and no indicator defined.");
            }

            *ind = i_null;
            return;
        }
        else
        {
            if (ind != NULL)
            {
                *ind = i_ok;
            }
        }

        // only std::string and std::tm need special handling
        if (type_ == x_char)
        {
            exchange_type_cast<x_char>(data_) = buf_[0];
        }
        else if (type_ == x_stdstring)
        {
            std::string& s = exchange_type_cast<x_stdstring>(data_);
            s = buf_;
            if (s.size() >= (odbc_max_buffer_length - 1))
            {
                throw soci_error("Buffer size overflow; maybe got too large string");
            }
        }
        else if (type_ == x_longstring)
        {
            exchange_type_cast<x_longstring>(data_).value = buf_;
        }
        else if (type_ == x_xmltype)
        {
            exchange_type_cast<x_xmltype>(data_).value = buf_;
        }
        else if (type_ == x_stdtm)
        {
            std::tm& t = exchange_type_cast<x_stdtm>(data_);

            TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(buf_);

            details::mktime_from_ymdhms(t,
                                        ts->year, ts->month, ts->day,
                                        ts->hour, ts->minute, ts->second);
        }
        else if (type_ == x_long_long && use_string_for_bigint())
        {
          long long& ll = exchange_type_cast<x_long_long>(data_);
          if (sscanf(buf_, "%" LL_FMT_FLAGS "d", &ll) != 1)
          {
            throw soci_error("Failed to parse the returned 64-bit integer value");
          }
        }
        else if (type_ == x_unsigned_long_long && use_string_for_bigint())
        {
          unsigned long long& ll = exchange_type_cast<x_unsigned_long_long>(data_);
          if (sscanf(buf_, "%" LL_FMT_FLAGS "u", &ll) != 1)
          {
            throw soci_error("Failed to parse the returned 64-bit integer value");
          }
        }
    }
}

void odbc_standard_into_type_backend::clean_up()
{
    if (buf_)
    {
        delete [] buf_;
        buf_ = 0;
    }
}
