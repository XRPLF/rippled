//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE
#include "soci-oracle.h"
#include "statement.h"
#include "error.h"
#include <soci-platform.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

void oracle_vector_into_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indOCIHolderVec_.resize(size);
    indOCIHolders_ = &indOCIHolderVec_[0];

    sizes_.resize(size);
    rCodes_.resize(size);
}

void oracle_vector_into_type_backend::define_by_pos(
    int &position, void *data, exchange_type type)
{
    data_ = data; // for future reference
    type_ = type; // for future reference

    ub2 oracleType = 0; // dummy initialization to please the compiler
    sb4 size = 0;       // also dummy

    switch (type)
    {
    // simple cases
    case x_char:
        {
            oracleType = SQLT_AFC;
            size = sizeof(char);
            std::vector<char> *vp = static_cast<std::vector<char> *>(data);
            std::vector<char> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_short:
        {
            oracleType = SQLT_INT;
            size = sizeof(short);
            std::vector<short> *vp = static_cast<std::vector<short> *>(data);
            std::vector<short> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_integer:
        {
            oracleType = SQLT_INT;
            size = sizeof(int);
            std::vector<int> *vp = static_cast<std::vector<int> *>(data);
            std::vector<int> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;
    case x_double:
        {
            oracleType = SQLT_FLT;
            size = sizeof(double);
            std::vector<double> *vp = static_cast<std::vector<double> *>(data);
            std::vector<double> &v(*vp);
            prepare_indicators(v.size());
            data = &v[0];
        }
        break;

    // cases that require adjustments and buffer management

    case x_long_long:
        {
            oracleType = SQLT_STR;
            std::vector<long long> *v
                = static_cast<std::vector<long long> *>(data);
            colSize_ = 100; // arbitrary buffer size for each entry
            std::size_t const bufSize = colSize_ * v->size();
            buf_ = new char[bufSize];

            prepare_indicators(v->size());

            size = static_cast<sb4>(colSize_);
            data = buf_;
        }
        break;
    case x_unsigned_long_long:
        {
            oracleType = SQLT_STR;
            std::vector<unsigned long long> *v
                = static_cast<std::vector<unsigned long long> *>(data);
            colSize_ = 100; // arbitrary buffer size for each entry
            std::size_t const bufSize = colSize_ * v->size();
            buf_ = new char[bufSize];

            prepare_indicators(v->size());

            size = static_cast<sb4>(colSize_);
            data = buf_;
        }
        break;
    case x_stdstring:
        {
            oracleType = SQLT_CHR;
            std::vector<std::string> *v
                = static_cast<std::vector<std::string> *>(data);
            colSize_ = statement_.column_size(position) + 1;
            std::size_t bufSize = colSize_ * v->size();
            buf_ = new char[bufSize];

            prepare_indicators(v->size());

            size = static_cast<sb4>(colSize_);
            data = buf_;
        }
        break;
    case x_stdtm:
        {
            oracleType = SQLT_DAT;
            std::vector<std::tm> *v
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(v->size());

            size = 7; // 7 is the size of SQLT_DAT
            std::size_t bufSize = size * v->size();

            buf_ = new char[bufSize];
            data = buf_;
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    }

    sword res = OCIDefineByPos(statement_.stmtp_, &defnp_,
        statement_.session_.errhp_,
        position++, data, size, oracleType,
        indOCIHolders_, &sizes_[0], &rCodes_[0], OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }
}

void oracle_vector_into_type_backend::pre_fetch()
{
    // nothing to do for the supported types
}

void oracle_vector_into_type_backend::post_fetch(bool gotData, indicator *ind)
{
    if (gotData)
    {
        // first, deal with data

        // only std::string, std::tm, long long and Statement need special handling
        if (type_ == x_stdstring)
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data_);

            std::vector<std::string> &v(*vp);

            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[i].assign(pos, sizes_[i]);
                }
                pos += colSize_;
            }
        }
        else if (type_ == x_long_long)
        {
            std::vector<long long> *vp
                = static_cast<std::vector<long long> *>(data_);

            std::vector<long long> &v(*vp);

            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[i] = std::strtoll(pos, NULL, 10);
                }
                pos += colSize_;
            }
        }
        else if (type_ == x_unsigned_long_long)
        {
            std::vector<unsigned long long> *vp
                = static_cast<std::vector<unsigned long long> *>(data_);

            std::vector<unsigned long long> &v(*vp);

            char *pos = buf_;
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[i] = std::strtoull(pos, NULL, 10);
                }
                pos += colSize_;
            }
        }
        else if (type_ == x_stdtm)
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data_);

            std::vector<std::tm> &v(*vp);

            ub1 *pos = reinterpret_cast<ub1*>(buf_);
            std::size_t const vsize = v.size();
            for (std::size_t i = 0; i != vsize; ++i)
            {
                if (indOCIHolderVec_[i] == -1)
                {
                     pos += 7; // size of SQLT_DAT
                }
                else
                {
                    std::tm t;
                    t.tm_isdst = -1;

                    t.tm_year = (*pos++ - 100) * 100;
                    t.tm_year += *pos++ - 2000;
                    t.tm_mon = *pos++ - 1;
                    t.tm_mday = *pos++;
                    t.tm_hour = *pos++ - 1;
                    t.tm_min = *pos++ - 1;
                    t.tm_sec = *pos++ - 1;

                    // normalize and compute the remaining fields
                    std::mktime(&t);
                    v[i] = t;
                }
            }
        }
        else if (type_ == x_statement)
        {
            statement *st = static_cast<statement *>(data_);
            st->define_and_bind();
        }

        // then - deal with indicators
        if (ind != NULL)
        {
            std::size_t const indSize = statement_.get_number_of_rows();
            for (std::size_t i = 0; i != indSize; ++i)
            {
                if (indOCIHolderVec_[i] == 0)
                {
                    ind[i] = i_ok;
                }
                else if (indOCIHolderVec_[i] == -1)
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
            std::size_t const indSize = indOCIHolderVec_.size();
            for (std::size_t i = 0; i != indSize; ++i)
            {
                if (indOCIHolderVec_[i] == -1)
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

void oracle_vector_into_type_backend::resize(std::size_t sz)
{
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
            std::vector<long long> *v
                = static_cast<std::vector<long long> *>(data_);
            v->resize(sz);
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v
                = static_cast<std::vector<unsigned long long> *>(data_);
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

std::size_t oracle_vector_into_type_backend::size()
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
            std::vector<long long> *v
                = static_cast<std::vector<long long> *>(data_);
            sz = v->size();
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *v
                = static_cast<std::vector<unsigned long long> *>(data_);
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

void oracle_vector_into_type_backend::clean_up()
{
    if (defnp_ != NULL)
    {
        OCIHandleFree(defnp_, OCI_HTYPE_DEFINE);
        defnp_ = NULL;
    }

    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
