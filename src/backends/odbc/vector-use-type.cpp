//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci/soci-platform.h"
#include "soci/odbc/soci-odbc.h"
#include "soci-static-assert.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef _MSC_VER
// disables the warning about converting int to void*.  This is a 64 bit compatibility
// warning, but odbc requires the value to be converted on this line
// SQLSetStmtAttr(statement_.hstmt_, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)arraySize, 0);
#pragma warning(disable:4312)
#endif

using namespace soci;
using namespace soci::details;

void odbc_vector_use_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indHolderVec_.resize(size);
    indHolders_ = &indHolderVec_[0];
}

void odbc_vector_use_type_backend::prepare_for_bind(void *&data, SQLUINTEGER &size,
    SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    switch (type_)
    {    // simple cases
    case x_short:
        {
            sqlType = SQL_SMALLINT;
            cType = SQL_C_SSHORT;
            size = sizeof(short);
            std::vector<short> *vp = static_cast<std::vector<short> *>(data);
            std::vector<short> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_integer:
        {
            sqlType = SQL_INTEGER;
            cType = SQL_C_SLONG;
            size = sizeof(SQLINTEGER);
            SOCI_STATIC_ASSERT(sizeof(SQLINTEGER) == sizeof(int));
            std::vector<int> *vp = static_cast<std::vector<int> *>(data);
            std::vector<int> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *vp =
                static_cast<std::vector<long long> *>(data);
            std::vector<long long> &v(*vp);
            std::size_t const vsize = v.size();
            prepare_indicators(vsize);

            if (use_string_for_bigint())
            {
                sqlType = SQL_NUMERIC;
                cType = SQL_C_CHAR;
                size = max_bigint_length;
                buf_ = new char[size * vsize];
                data = buf_;
            }
            else // Normal case, use ODBC support.
            {
                sqlType = SQL_BIGINT;
                cType = SQL_C_SBIGINT;
                size = sizeof(long long);
                data = &v[0];
            }
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *vp =
                static_cast<std::vector<unsigned long long> *>(data);
            std::vector<unsigned long long> &v(*vp);
            std::size_t const vsize = v.size();
            prepare_indicators(vsize);

            if (use_string_for_bigint())
            {
                sqlType = SQL_NUMERIC;
                cType = SQL_C_CHAR;
                size = max_bigint_length;
                buf_ = new char[size * vsize];
                data = buf_;
            }
            else // Normal case, use ODBC support.
            {
                sqlType = SQL_BIGINT;
                cType = SQL_C_SBIGINT;
                size = sizeof(unsigned long long);
                data = &v[0];
            }
        }
        break;
    case x_double:
        {
            sqlType = SQL_DOUBLE;
            cType = SQL_C_DOUBLE;
            size = sizeof(double);
            std::vector<double> *vp = static_cast<std::vector<double> *>(data);
            std::vector<double> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;

    // cases that require adjustments and buffer management
    case x_char:
        {
            std::vector<char> *vp
                = static_cast<std::vector<char> *>(data);
            std::size_t const vsize = vp->size();

            prepare_indicators(vsize);

            size = sizeof(char) * 2;
            buf_ = new char[size * vsize];

            char *pos = buf_;

            for (std::size_t i = 0; i != vsize; ++i)
            {
                *pos++ = (*vp)[i];
                *pos++ = 0;
            }

            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;
            data = buf_;
        }
        break;
    case x_stdstring:
        {
            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;

            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data);
            std::vector<std::string> &v(*vp);

            std::size_t maxSize = 0;
            std::size_t const vecSize = v.size();
            prepare_indicators(vecSize);
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                std::size_t sz = v[i].length();
                indHolderVec_[i] = static_cast<long>(sz);
                maxSize = sz > maxSize ? sz : maxSize;
            }

            maxSize++; // For terminating nul.

            buf_ = new char[maxSize * vecSize];
            memset(buf_, 0, maxSize * vecSize);

            char *pos = buf_;
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                memcpy(pos, v[i].c_str(), v[i].length());
                pos += maxSize;
            }

            data = buf_;
            size = static_cast<SQLINTEGER>(maxSize);
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(vp->size());

            buf_ = new char[sizeof(TIMESTAMP_STRUCT) * vp->size()];

            sqlType = SQL_TYPE_TIMESTAMP;
            cType = SQL_C_TYPE_TIMESTAMP;
            data = buf_;
            size = 19; // This number is not the size in bytes, but the number
                      // of characters in the date if it was written out
                      // yyyy-mm-dd hh:mm:ss
        }
        break;

    // not supported
    default:
        throw soci_error("Use vector element used with non-supported type.");
    }

    colSize_ = size;
}

void odbc_vector_use_type_backend::bind_helper(int &position, void *data, exchange_type type)
{
    data_ = data; // for future reference
    type_ = type; // for future reference

    SQLSMALLINT sqlType(0);
    SQLSMALLINT cType(0);
    SQLUINTEGER size(0);

    prepare_for_bind(data, size, sqlType, cType);

    SQLULEN const arraySize = static_cast<SQLULEN>(indHolderVec_.size());
    SQLSetStmtAttr(statement_.hstmt_, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)arraySize, 0);

    SQLRETURN rc = SQLBindParameter(statement_.hstmt_, static_cast<SQLUSMALLINT>(position++),
                                    SQL_PARAM_INPUT, cType, sqlType, size, 0,
                                    static_cast<SQLPOINTER>(data), size, indHolders_);

    if (is_odbc_error(rc))
    {
        std::ostringstream ss;
        ss << "binding input vector parameter #" << position;
        throw odbc_soci_error(SQL_HANDLE_STMT, statement_.hstmt_, ss.str());
    }
}

void odbc_vector_use_type_backend::bind_by_pos(int &position,
        void *data, exchange_type type)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    bind_helper(position, data, type);

    statement_.boundByPos_ = true;
}

void odbc_vector_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type)
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

    if (position != -1)
    {
        bind_helper(position, data, type);
    }
    else
    {
        std::ostringstream ss;
        ss << "Unable to find name '" << name << "' to bind to";
        throw soci_error(ss.str().c_str());
    }

    statement_.boundByName_ = true;
}

void odbc_vector_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    SQLLEN non_null_indicator = 0;
    switch (type_)
    {
        case x_short:
        case x_integer:
        case x_double:
            // Length of the parameter value is ignored for these types.
            break;

        case x_char:
        case x_stdstring:
            non_null_indicator = SQL_NTS;
            break;

        case x_stdtm:
            {
                std::vector<std::tm> *vp
                     = static_cast<std::vector<std::tm> *>(data_);

                std::vector<std::tm> &v(*vp);

                char *pos = buf_;
                std::size_t const vsize = v.size();
                for (std::size_t i = 0; i != vsize; ++i)
                {
                    std::tm t = v[i];
                    TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(pos);

                    ts->year = static_cast<SQLSMALLINT>(t.tm_year + 1900);
                    ts->month = static_cast<SQLUSMALLINT>(t.tm_mon + 1);
                    ts->day = static_cast<SQLUSMALLINT>(t.tm_mday);
                    ts->hour = static_cast<SQLUSMALLINT>(t.tm_hour);
                    ts->minute = static_cast<SQLUSMALLINT>(t.tm_min);
                    ts->second = static_cast<SQLUSMALLINT>(t.tm_sec);
                    ts->fraction = 0;
                    pos += sizeof(TIMESTAMP_STRUCT);
                }
            }
            break;

        case x_long_long:
            if (use_string_for_bigint())
            {
                std::vector<long long> *vp
                     = static_cast<std::vector<long long> *>(data_);
                std::vector<long long> &v(*vp);

                char *pos = buf_;
                std::size_t const vsize = v.size();
                for (std::size_t i = 0; i != vsize; ++i)
                {
                    snprintf(pos, max_bigint_length, "%" LL_FMT_FLAGS "d", v[i]);
                    pos += max_bigint_length;
                }

                non_null_indicator = SQL_NTS;
            }
            break;

        case x_unsigned_long_long:
            if (use_string_for_bigint())
            {
                std::vector<unsigned long long> *vp
                     = static_cast<std::vector<unsigned long long> *>(data_);
                std::vector<unsigned long long> &v(*vp);

                char *pos = buf_;
                std::size_t const vsize = v.size();
                for (std::size_t i = 0; i != vsize; ++i)
                {
                    snprintf(pos, max_bigint_length, "%" LL_FMT_FLAGS "u", v[i]);
                    pos += max_bigint_length;
                }

                non_null_indicator = SQL_NTS;
            }
            break;

        case x_statement:
        case x_rowid:
        case x_blob:
        case x_xmltype:
        case x_longstring:
            // Those are unreachable, we would have thrown from
            // prepare_for_bind() if we we were using one of them, only handle
            // them here to avoid compiler warnings about unhandled enum
            // elements.
            break;
    }

    // then handle indicators
    if (ind != NULL)
    {
        std::size_t const vsize = size();
        for (std::size_t i = 0; i != vsize; ++i, ++ind)
        {
            if (*ind == i_null)
            {
                indHolderVec_[i] = SQL_NULL_DATA; // null
            }
            else
            {
            // for strings we have already set the values
            if (type_ != x_stdstring)
                {
                    indHolderVec_[i] = non_null_indicator;
                }
            }
        }
    }
    else
    {
        // no indicators - treat all fields as OK
        std::size_t const vsize = size();
        for (std::size_t i = 0; i != vsize; ++i, ++ind)
        {
            // for strings we have already set the values
            if (type_ != x_stdstring)
            {
                indHolderVec_[i] = non_null_indicator;
            }
        }
    }
}

std::size_t odbc_vector_use_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type_)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *vp = static_cast<std::vector<char> *>(data_);
            sz = vp->size();
        }
        break;
    case x_short:
        {
            std::vector<short> *vp = static_cast<std::vector<short> *>(data_);
            sz = vp->size();
        }
        break;
    case x_integer:
        {
            std::vector<int> *vp = static_cast<std::vector<int> *>(data_);
            sz = vp->size();
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *vp =
                static_cast<std::vector<long long> *>(data_);
            sz = vp->size();
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *vp =
                static_cast<std::vector<unsigned long long> *>(data_);
            sz = vp->size();
        }
        break;
    case x_double:
        {
            std::vector<double> *vp
                = static_cast<std::vector<double> *>(data_);
            sz = vp->size();
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data_);
            sz = vp->size();
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data_);
            sz = vp->size();
        }
        break;

    // not supported
    default:
        throw soci_error("Use vector element used with non-supported type.");
    }

    return sz;
}

void odbc_vector_use_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
