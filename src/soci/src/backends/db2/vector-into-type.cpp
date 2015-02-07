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

using namespace soci;
using namespace soci::details;

void db2_vector_into_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indVec.resize(size);
    indptr = &indVec[0];
}

void db2_vector_into_type_backend::define_by_pos(
    int &position, void *data, exchange_type type)
{
    this->data = data; // for future reference
    this->type = type; // for future reference

    SQLINTEGER size = 0;       // also dummy

    switch (type)
    {
    // simple cases
    case x_short:
        {
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
            cType = SQL_C_SLONG;
            size = sizeof(SQLINTEGER);
            std::vector<SQLINTEGER> *vp = static_cast<std::vector<SQLINTEGER> *>(data);
            std::vector<SQLINTEGER> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_long_long:
        {
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
            cType = SQL_C_CHAR;

            std::vector<char> *v
                = static_cast<std::vector<char> *>(data);

            prepare_indicators(v->size());

            size = sizeof(char) * 2;
            std::size_t bufSize = size * v->size();

            colSize = size;

            buf = new char[bufSize];
            data = buf;
        }
        break;
    case x_stdstring:
        {
            cType = SQL_C_CHAR;
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data);
            colSize = statement_.column_size(position) + 1;
            std::size_t bufSize = colSize * v->size();
            buf = new char[bufSize];

            prepare_indicators(v->size());

            size = static_cast<SQLINTEGER>(colSize);
            data = buf;
        }
        break;
    case x_stdtm:
        {
            cType = SQL_C_TYPE_TIMESTAMP;
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(v->size());

            size = sizeof(TIMESTAMP_STRUCT);
            colSize = size;

            std::size_t bufSize = size * v->size();

            buf = new char[bufSize];
            data = buf;
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    SQLRETURN cliRC = SQLBindCol(statement_.hStmt, static_cast<SQLUSMALLINT>(position++),
                              cType, data, size, indptr);
    if (cliRC != SQL_SUCCESS)
    {
        throw db2_soci_error("Error while pre-fetching into vector",cliRC);
    }
}

void db2_vector_into_type_backend::pre_fetch()
{
    // nothing to do for the supported types
}

void db2_vector_into_type_backend::post_fetch(bool gotData, indicator *ind)
{
    if (gotData)
    {
        // first, deal with data

        // only std::string, std::tm and Statement need special handling
        if (type == x_char)
        {
            std::vector<char> *vp
                = static_cast<std::vector<char> *>(data);

            std::vector<char> &v(*vp);
            char *pos = buf;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                v[i] = *pos;
                pos += colSize;
            }
        }
        if (type == x_stdstring)
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data);

            std::vector<std::string> &v(*vp);

            char *pos = buf;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                v[i].assign(pos, strlen(pos));
                pos += colSize;
            }
        }
        else if (type == x_stdtm)
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);

            std::vector<std::tm> &v(*vp);
            char *pos = buf;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                std::tm t;

                TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(pos);
                t.tm_isdst = -1;
                t.tm_year = ts->year - 1900;
                t.tm_mon = ts->month - 1;
                t.tm_mday = ts->day;
                t.tm_hour = ts->hour;
                t.tm_min = ts->minute;
                t.tm_sec = ts->second;

                // normalize and compute the remaining fields
                std::mktime(&t);
                v[i] = t;
                pos += colSize;
            }
        }

        // then - deal with indicators
        if (ind != NULL)
        {
            std::size_t const indSize = statement_.get_number_of_rows();
            for (std::size_t i = 0; i != indSize; ++i)
            {
                if (indVec[i] > 0)
                {
                    ind[i] = i_ok;
                }
                else if (indVec[i] == SQL_NULL_DATA)
                {
                    ind[i] = i_null;
                }
                else
                {
                    ind[i] = i_truncated;
                }
            }
        }
        else
        {
            std::size_t const indSize = statement_.get_number_of_rows();
            for (std::size_t i = 0; i != indSize; ++i)
            {
                if (indVec[i] == SQL_NULL_DATA)
                {
                    // fetched null and no indicator - programming error!
                    throw soci_error(
                        "Null value fetched and no indicator defined.");
                }
            }
        }
    }
    else // gotData == false
    {
        // nothing to do here, vectors are truncated anyway
    }
}

void db2_vector_into_type_backend::resize(std::size_t sz)
{
    indVec.resize(sz);
    switch (type)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *v = static_cast<std::vector<char> *>(data);
            v->resize(sz);
        }
        break;
    case x_short:
        {
            std::vector<short> *v = static_cast<std::vector<short> *>(data);
            v->resize(sz);
        }
        break;
    case x_integer:
        {
            std::vector<SQLINTEGER> *v = static_cast<std::vector<SQLINTEGER> *>(data);
            v->resize(sz);
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *v
                = static_cast<std::vector<long long> *>(data);
            v->resize(sz);
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v
                = static_cast<std::vector<unsigned long long> *>(data);
            v->resize(sz);
        }
        break;
    case x_double:
        {
            std::vector<double> *v
                = static_cast<std::vector<double> *>(data);
            v->resize(sz);
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data);
            v->resize(sz);
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data);
            v->resize(sz);
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }
}

std::size_t db2_vector_into_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *v = static_cast<std::vector<char> *>(data);
            sz = v->size();
        }
        break;
    case x_short:
        {
            std::vector<short> *v = static_cast<std::vector<short> *>(data);
            sz = v->size();
        }
        break;
    case x_integer:
        {
            std::vector<SQLINTEGER> *v = static_cast<std::vector<SQLINTEGER> *>(data);
            sz = v->size();
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *v
                = static_cast<std::vector<long long> *>(data);
            sz = v->size();
        }
        break;
   case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v
                = static_cast<std::vector<unsigned long long> *>(data);
            sz = v->size();
        }
        break;
    case x_double:
        {
            std::vector<double> *v
                = static_cast<std::vector<double> *>(data);
            sz = v->size();
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data);
            sz = v->size();
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data);
            sz = v->size();
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    return sz;
}

void db2_vector_into_type_backend::clean_up()
{
    if (buf != NULL)
    {
        delete [] buf;
        buf = NULL;
    }
}
