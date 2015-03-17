//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_DB2_SOURCE
#include "soci-db2.h"
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

void db2_vector_use_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indVec.resize(size);
    indptr = &indVec[0];
}

void db2_vector_use_type_backend::prepare_for_bind(void *&data, SQLUINTEGER &size,
    SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    switch (type)
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
            size = sizeof(int);
            std::vector<int> *vp = static_cast<std::vector<int> *>(data);
            std::vector<int> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_long_long:
        {
            sqlType = SQL_BIGINT;
            cType = SQL_C_SBIGINT;
            size = sizeof(long long);
            std::vector<long long> *vp
                 = static_cast<std::vector<long long> *>(data);
            std::vector<long long> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_unsigned_long_long:
        {
            sqlType = SQL_BIGINT;
            cType = SQL_C_UBIGINT;
            size = sizeof(unsigned long long);
            std::vector<unsigned long long> *vp
                 = static_cast<std::vector<unsigned long long> *>(data);
            std::vector<unsigned long long> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
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
            buf = new char[size * vsize];

            char *pos = buf;

            for (std::size_t i = 0; i != vsize; ++i)
            {
                *pos++ = (*vp)[i];
                *pos++ = 0;
            }

            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;
            data = buf;
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
                std::size_t sz = v[i].length() + 1;  // add one for null
                indVec[i] = static_cast<long>(sz);
                maxSize = sz > maxSize ? sz : maxSize;
            }

            buf = new char[maxSize * vecSize];
            memset(buf, 0, maxSize * vecSize);

            char *pos = buf;
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                strncpy(pos, v[i].c_str(), v[i].length());
                pos += maxSize;
            }

            data = buf;
            size = static_cast<SQLINTEGER>(maxSize);
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(vp->size());

            buf = new char[sizeof(TIMESTAMP_STRUCT) * vp->size()];

            sqlType = SQL_TYPE_TIMESTAMP;
            cType = SQL_C_TYPE_TIMESTAMP;
            data = buf;
            size = 19; // This number is not the size in bytes, but the number
                      // of characters in the date if it was written out
                      // yyyy-mm-dd hh:mm:ss
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    colSize = size;
}

void db2_vector_use_type_backend::bind_helper(int &position, void *data, details::exchange_type type)
{
    this->data = data; // for future reference
    this->type = type; // for future reference

    SQLSMALLINT sqlType;
    SQLSMALLINT cType;
    SQLUINTEGER size;

    prepare_for_bind(data, size, sqlType, cType);

    SQLINTEGER arraySize = (SQLINTEGER)indVec.size();
    SQLSetStmtAttr(statement_.hStmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)arraySize, 0);

    SQLRETURN cliRC = SQLBindParameter(statement_.hStmt, static_cast<SQLUSMALLINT>(position++),
                                    SQL_PARAM_INPUT, cType, sqlType, size, 0,
                                    static_cast<SQLPOINTER>(data), size, indptr);

    if ( cliRC != SQL_SUCCESS )
    {
        throw db2_soci_error("Error while binding value to column", cliRC);
    }
}

void db2_vector_use_type_backend::bind_by_pos(int &position,
        void *data, exchange_type type)
{
    if (statement_.use_binding_method_ == details::db2::BOUND_BY_NAME)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_POSITION;

    bind_helper(position, data, type);
}

void db2_vector_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type)
{
    int position = -1;
    int count = 1;

    if (statement_.use_binding_method_ == details::db2::BOUND_BY_POSITION)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_NAME;

    for (std::vector<std::string>::iterator it = statement_.names.begin();
         it != statement_.names.end(); ++it)
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
}

void db2_vector_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    if (type == x_stdtm)
    {
        std::vector<std::tm> *vp
             = static_cast<std::vector<std::tm> *>(data);

        std::vector<std::tm> &v(*vp);

        char *pos = buf;
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

    // then handle indicators
    if (ind != NULL)
    {
        std::size_t const vsize = size();
        for (std::size_t i = 0; i != vsize; ++i, ++ind)
        {
            if (*ind == i_null)
            {
                indVec[i] = SQL_NULL_DATA; // null
            }
            else
            {
            // for strings we have already set the values
            if (type != x_stdstring)
                {
                    indVec[i] = SQL_NTS;  // value is OK
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
            if (type != x_stdstring)
            {
                indVec[i] = SQL_NTS;  // value is OK
            }
        }
    }
}

std::size_t db2_vector_use_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *vp = static_cast<std::vector<char> *>(data);
            sz = vp->size();
        }
        break;
    case x_short:
        {
            std::vector<short> *vp = static_cast<std::vector<short> *>(data);
            sz = vp->size();
        }
        break;
    case x_integer:
        {
            std::vector<int> *vp = static_cast<std::vector<int> *>(data);
            sz = vp->size();
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *vp
                = static_cast<std::vector<long long> *>(data);
            sz = vp->size();
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *vp
                = static_cast<std::vector<unsigned long long> *>(data);
            sz = vp->size();
        }
        break;
    case x_double:
        {
            std::vector<double> *vp
                = static_cast<std::vector<double> *>(data);
            sz = vp->size();
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data);
            sz = vp->size();
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);
            sz = vp->size();
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    return sz;
}

void db2_vector_use_type_backend::clean_up()
{
    if (buf != NULL)
    {
        delete [] buf;
        buf = NULL;
    }
}
