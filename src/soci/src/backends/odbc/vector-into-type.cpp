//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci-odbc.h"
#include <soci-platform.h>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <stdio.h>  // sscanf()

using namespace soci;
using namespace soci::details;

void odbc_vector_into_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indHolderVec_.resize(size);
    indHolders_ = &indHolderVec_[0];
}

void odbc_vector_into_type_backend::define_by_pos(
    int &position, void *data, exchange_type type)
{
    data_ = data; // for future reference
    type_ = type; // for future reference

    SQLLEN size = 0;       // also dummy

    switch (type)
    {
    // simple cases
    case x_short:
        {
            odbcType_ = SQL_C_SSHORT;
            size = sizeof(short);
            std::vector<short> *vp = static_cast<std::vector<short> *>(data);
            std::vector<short> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_integer:
        {
            odbcType_ = SQL_C_SLONG;
            size = sizeof(SQLINTEGER);
            assert(sizeof(SQLINTEGER) == sizeof(int));
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
            prepare_indicators(v.size());
            if (use_string_for_bigint())
            {
                odbcType_ = SQL_C_CHAR;
                size = max_bigint_length;
                std::size_t bufSize = size * v.size();
                colSize_ = size;
                buf_ = new char[bufSize];
                data = buf_;
            }
            else // Normal case, use ODBC support.
            {
                odbcType_ = SQL_C_SBIGINT;
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
            prepare_indicators(v.size());
            if (use_string_for_bigint())
            {
                odbcType_ = SQL_C_CHAR;
                size = max_bigint_length;
                std::size_t bufSize = size * v.size();
                colSize_ = size;
                buf_ = new char[bufSize];
                data = buf_;
            }
            else // Normal case, use ODBC support.
            {
                odbcType_ = SQL_C_UBIGINT;
                size = sizeof(unsigned long long);
                data = &v[0];
            }
        }
        break;
    case x_double:
        {
            odbcType_ = SQL_C_DOUBLE;
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
            odbcType_ = SQL_C_CHAR;

            std::vector<char> *v
                = static_cast<std::vector<char> *>(data);

            prepare_indicators(v->size());

            size = sizeof(char) * 2;
            std::size_t bufSize = size * v->size();

            colSize_ = size;

            buf_ = new char[bufSize];
            data = buf_;
        }
        break;
    case x_stdstring:
        {
            odbcType_ = SQL_C_CHAR;
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data);
            colSize_ = statement_.column_size(position) + 1;
            std::size_t bufSize = colSize_ * v->size();
            buf_ = new char[bufSize];

            prepare_indicators(v->size());

            size = static_cast<SQLINTEGER>(colSize_);
            data = buf_;
        }
        break;
    case x_stdtm:
        {
            odbcType_ = SQL_C_TYPE_TIMESTAMP;
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(v->size());

            size = sizeof(TIMESTAMP_STRUCT);
            colSize_ = size;

            std::size_t bufSize = size * v->size();

            buf_ = new char[bufSize];
            data = buf_;
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    SQLRETURN rc 
        = SQLBindCol(statement_.hstmt_, static_cast<SQLUSMALLINT>(position++),
                odbcType_, static_cast<SQLPOINTER>(data), size, indHolders_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, statement_.hstmt_,
                            "vector into type define by pos");
    }
}

void odbc_vector_into_type_backend::pre_fetch()
{
    // nothing to do for the supported types
}

void odbc_vector_into_type_backend::post_fetch(bool gotData, indicator *ind)
{
    if (gotData)
    {
        // first, deal with data

        // only std::string, std::tm and Statement need special handling
        if (type_ == x_char)
        {
            std::vector<char> *vp
                = static_cast<std::vector<char> *>(data_);

            std::vector<char> &v(*vp);
            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                v[i] = *pos;
                pos += colSize_;
            }
        }
        if (type_ == x_stdstring)
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data_);

            std::vector<std::string> &v(*vp);

            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                v[i].assign(pos, strlen(pos));
                pos += colSize_;
            }
        }
        else if (type_ == x_stdtm)
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data_);

            std::vector<std::tm> &v(*vp);
            char *pos = buf_;
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
                pos += colSize_;
            }
        }
        else if (type_ == x_long_long && use_string_for_bigint())
        {
            std::vector<long long> *vp
                = static_cast<std::vector<long long> *>(data_);
            std::vector<long long> &v(*vp);
            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (sscanf(pos, "%" LL_FMT_FLAGS "d", &v[i]) != 1)
                {
                    throw soci_error("Failed to parse the returned 64-bit integer value");
                }
                pos += colSize_;
            }
        }
        else if (type_ == x_unsigned_long_long && use_string_for_bigint())
        {
            std::vector<unsigned long long> *vp
                = static_cast<std::vector<unsigned long long> *>(data_);
            std::vector<unsigned long long> &v(*vp);
            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (sscanf(pos, "%" LL_FMT_FLAGS "u", &v[i]) != 1)
                {
                    throw soci_error("Failed to parse the returned 64-bit integer value");
                }
                pos += colSize_;
            }
        }

        // then - deal with indicators
        if (ind != NULL)
        {
            std::size_t const indSize = statement_.get_number_of_rows();
            for (std::size_t i = 0; i != indSize; ++i)
            {
                if (indHolderVec_[i] > 0)
                {
                    ind[i] = i_ok;
                }
                else if (indHolderVec_[i] == SQL_NULL_DATA)
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
                if (indHolderVec_[i] == SQL_NULL_DATA)
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

void odbc_vector_into_type_backend::resize(std::size_t sz)
{
    indHolderVec_.resize(sz);
    switch (type_)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *v = static_cast<std::vector<char> *>(data_);
            v->resize(sz);
        }
        break;
    case x_short:
        {
            std::vector<short> *v = static_cast<std::vector<short> *>(data_);
            v->resize(sz);
        }
        break;
    case x_integer:
        {
            std::vector<int> *v = static_cast<std::vector<int> *>(data_);
            v->resize(sz);
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *v =
                static_cast<std::vector<long long> *>(data_);
            v->resize(sz);
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v =
                static_cast<std::vector<unsigned long long> *>(data_);
            v->resize(sz);
        }
        break;
    case x_double:
        {
            std::vector<double> *v
                = static_cast<std::vector<double> *>(data_);
            v->resize(sz);
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data_);
            v->resize(sz);
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data_);
            v->resize(sz);
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }
}

std::size_t odbc_vector_into_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type_)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *v = static_cast<std::vector<char> *>(data_);
            sz = v->size();
        }
        break;
    case x_short:
        {
            std::vector<short> *v = static_cast<std::vector<short> *>(data_);
            sz = v->size();
        }
        break;
    case x_integer:
        {
            std::vector<int> *v = static_cast<std::vector<int> *>(data_);
            sz = v->size();
        }
        break;
    case x_long_long:
        {
            std::vector<long long> *v =
                static_cast<std::vector<long long> *>(data_);
            sz = v->size();
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v =
                static_cast<std::vector<unsigned long long> *>(data_);
            sz = v->size();
        }
        break;
    case x_double:
        {
            std::vector<double> *v
                = static_cast<std::vector<double> *>(data_);
            sz = v->size();
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data_);
            sz = v->size();
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data_);
            sz = v->size();
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    return sz;
}

void odbc_vector_into_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
