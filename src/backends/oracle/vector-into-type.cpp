//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "soci/statement.h"
#include "error.h"
#include "soci/soci-platform.h"
#include "soci-mktime.h"
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

void oracle_vector_into_type_backend::define_by_pos_bulk(
    int & position, void * data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data; // for future reference
    type_ = type; // for future reference
    begin_ = begin;
    end_ = end;

    end_var_ = full_size();

    ub2 oracleType = 0;  // dummy initialization to please the compiler
    sb4 elementSize = 0; // also dummy
    void * dataBuf;

    switch (type)
    {
    // simple cases
    case x_char:
        {
            oracleType = SQLT_AFC;
            elementSize = sizeof(char);
            std::vector<char> *vp = static_cast<std::vector<char> *>(data);
            std::vector<char> &v(*vp);
            prepare_indicators(size());
            dataBuf = &v[begin_];
        }
        break;
    case x_short:
        {
            oracleType = SQLT_INT;
            elementSize = sizeof(short);
            std::vector<short> *vp = static_cast<std::vector<short> *>(data);
            std::vector<short> &v(*vp);
            prepare_indicators(size());
            dataBuf = &v[begin_];
        }
        break;
    case x_integer:
        {
            oracleType = SQLT_INT;
            elementSize = sizeof(int);
            std::vector<int> *vp = static_cast<std::vector<int> *>(data);
            std::vector<int> &v(*vp);
            prepare_indicators(size());
            dataBuf = &v[begin_];
        }
        break;
    case x_double:
        {
            oracleType = statement_.session_.get_double_sql_type();
            elementSize = sizeof(double);
            std::vector<double> *vp = static_cast<std::vector<double> *>(data);
            std::vector<double> &v(*vp);
            prepare_indicators(size());
            dataBuf = &v[begin_];
        }
        break;

    // cases that require adjustments and buffer management

    case x_long_long:
        {
            oracleType = SQLT_STR;
            const std::size_t vecSize = size();
            colSize_ = 100; // arbitrary buffer size for each entry
            std::size_t const bufSize = colSize_ * vecSize;
            buf_ = new char[bufSize];

            prepare_indicators(vecSize);

            elementSize = static_cast<sb4>(colSize_);
            dataBuf = buf_;
        }
        break;
    case x_unsigned_long_long:
        {
            oracleType = SQLT_STR;
            const std::size_t vecSize = size();
            colSize_ = 100; // arbitrary buffer size for each entry
            std::size_t const bufSize = colSize_ * vecSize;
            buf_ = new char[bufSize];

            prepare_indicators(vecSize);

            elementSize = static_cast<sb4>(colSize_);
            dataBuf = buf_;
        }
        break;
    case x_stdstring:
        {
            oracleType = SQLT_CHR;
            const std::size_t vecSize = size();
            colSize_ = statement_.column_size(position) + 1;
            std::size_t bufSize = colSize_ * vecSize;
            buf_ = new char[bufSize];

            prepare_indicators(vecSize);

            elementSize = static_cast<sb4>(colSize_);
            dataBuf = buf_;
        }
        break;
    case x_stdtm:
        {
            oracleType = SQLT_DAT;
            const std::size_t vecSize = size();

            prepare_indicators(vecSize);

            elementSize = 7; // 7 is the size of SQLT_DAT
            std::size_t bufSize = elementSize * vecSize;

            buf_ = new char[bufSize];
            dataBuf = buf_;
        }
        break;

    case x_xmltype:
    case x_longstring:
    case x_statement:
    case x_rowid:
    case x_blob:
        throw soci_error("Unsupported type for vector into parameter");
    }

    sword res = OCIDefineByPos(statement_.stmtp_, &defnp_,
        statement_.session_.errhp_,
        position++, dataBuf, elementSize, oracleType,
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

void oracle_vector_into_type_backend::post_fetch(bool gotData, indicator * ind)
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
            std::size_t const vecSize = size();
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[begin_ + i].assign(pos, sizes_[i]);
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
            std::size_t const vecSize = size();
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[begin_ + i] = std::strtoll(pos, NULL, 10);
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
            std::size_t const vecSize = size();
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                if (indOCIHolderVec_[i] != -1)
                {
                    v[begin_ + i] = std::strtoull(pos, NULL, 10);
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
            std::size_t const vecSize = size();
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                if (indOCIHolderVec_[i] == -1)
                {
                     pos += 7; // size of SQLT_DAT
                }
                else
                {
                    int year = (*pos++ - 100) * 100;
                    year += *pos++ - 100;
                    int const month = *pos++;
                    int const day = *pos++;
                    int const hour = *pos++ - 1;
                    int const minute = *pos++ - 1;
                    int const second = *pos++ - 1;

                    details::mktime_from_ymdhms(v[begin_ + i],
                        year, month, day, hour, minute, second);
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
                    ind[begin_ + i] = i_ok;
                }
                else if (indOCIHolderVec_[i] == -1)
                {
                    ind[begin_ + i] = i_null;
                }
                else
                {
                    ind[begin_ + i] = i_truncated;
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
    if (user_ranges_)
    {
        // resize only in terms of user-provided ranges (below)
    }
    else
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

        case x_xmltype:    break; // not supported
        case x_longstring: break; // not supported
        case x_statement:  break; // not supported
        case x_rowid:      break; // not supported
        case x_blob:       break; // not supported
        }

        end_var_ = sz;
    }

    // resize ranges, either user-provided or internally managed
    *end_ = begin_ + sz;
}

std::size_t oracle_vector_into_type_backend::size()
{
    // as a special error-detection measure, check if the actual vector size
    // was changed since the original bind (when it was stored in end_var_):
    const std::size_t actual_size = full_size();
    if (actual_size != end_var_)
    {
        // ... and in that case return the actual size
        return actual_size;
    }
    
    if (end_ != NULL && *end_ != 0)
    {
        return *end_ - begin_;
    }
    else
    {
        return end_var_;
    }
}

std::size_t oracle_vector_into_type_backend::full_size()
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

    case x_xmltype:    break; // not supported
    case x_longstring: break; // not supported
    case x_statement:  break; // not supported
    case x_rowid:      break; // not supported
    case x_blob:       break; // not supported
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
