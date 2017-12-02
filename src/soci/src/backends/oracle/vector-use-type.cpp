//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "error.h"
#include "soci/soci-platform.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#define snprintf _snprintf
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

void oracle_vector_use_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indOCIHolderVec_.resize(size);
    indOCIHolders_ = &indOCIHolderVec_[0];
}

void oracle_vector_use_type_backend::prepare_for_bind(
    void * & data, sb4 & elementSize, ub2 & oracleType)
{
    switch (type_)
    {
    // simple cases
    case x_char:
        {
            oracleType = SQLT_AFC;
            elementSize = sizeof(char);
            std::vector<char> *vp = static_cast<std::vector<char> *>(data_);
            std::vector<char> &v(*vp);
            prepare_indicators(size());
            data = &v[begin_];
        }
        break;
    case x_short:
        {
            oracleType = SQLT_INT;
            elementSize = sizeof(short);
            std::vector<short> *vp = static_cast<std::vector<short> *>(data_);
            std::vector<short> &v(*vp);
            prepare_indicators(size());
            data = &v[begin_];
        }
        break;
    case x_integer:
        {
            oracleType = SQLT_INT;
            elementSize = sizeof(int);
            std::vector<int> *vp = static_cast<std::vector<int> *>(data_);
            std::vector<int> &v(*vp);
            prepare_indicators(size());
            data = &v[begin_];
        }
        break;
    case x_double:
        {
            oracleType = statement_.session_.get_double_sql_type();
            elementSize = sizeof(double);
            std::vector<double> *vp = static_cast<std::vector<double> *>(data_);
            std::vector<double> &v(*vp);
            prepare_indicators(size());
            data = &v[begin_];
        }
        break;

    // cases that require adjustments and buffer management

    case x_long_long:
        {
            std::size_t const vecSize = size();
            std::size_t const entrySize = 100; // arbitrary
            std::size_t const bufSize = entrySize * vecSize;
            buf_ = new char[bufSize];

            oracleType = SQLT_STR;
            data = buf_;
            elementSize = entrySize;

            prepare_indicators(vecSize);
        }
        break;
    case x_unsigned_long_long:
        {
            std::size_t const vecSize = size();
            std::size_t const entrySize = 100; // arbitrary
            std::size_t const bufSize = entrySize * vecSize;
            buf_ = new char[bufSize];

            oracleType = SQLT_STR;
            data = buf_;
            elementSize = entrySize;

            prepare_indicators(vecSize);
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data_);
            std::vector<std::string> &v(*vp);

            std::size_t maxSize = 0;
            std::size_t const vecSize = size();
            prepare_indicators(vecSize);
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                std::size_t sz = v[begin_ + i].length();
                sizes_.push_back(static_cast<ub2>(sz));
                maxSize = sz > maxSize ? sz : maxSize;
            }

            buf_ = new char[maxSize * vecSize];
            char *pos = buf_;
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                strncpy(pos, v[begin_ + i].c_str(), v[begin_ + i].length());
                pos += maxSize;
            }

            oracleType = SQLT_CHR;
            data = buf_;
            elementSize = static_cast<sb4>(maxSize);
        }
        break;
    case x_stdtm:
        {
            std::size_t const vecSize = size();
            prepare_indicators(vecSize);

            sb4 const dlen = 7; // size of SQLT_DAT
            buf_ = new char[dlen * vecSize];

            oracleType = SQLT_DAT;
            data = buf_;
            elementSize = dlen;
        }
        break;

    case x_xmltype:    break; // not supported
    case x_longstring: break; // not supported
    case x_statement:  break; // not supported
    case x_rowid:      break; // not supported
    case x_blob:       break; // not supported
    }
}

void oracle_vector_use_type_backend::bind_by_pos_bulk(int & position,
    void * data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data; // for future reference
    type_ = type; // for future reference
    begin_ = begin;
    end_ = end;

    end_var_ = full_size();
    
    ub2 oracleType;
    sb4 elementSize;
    void * dataBuf;

    prepare_for_bind(dataBuf, elementSize, oracleType);

    ub2 *sizesP = 0; // used only for std::string
    if (type == x_stdstring)
    {
        sizesP = &sizes_[0];
    }

    sword res = OCIBindByPos(statement_.stmtp_, &bindp_,
        statement_.session_.errhp_,
        position++, dataBuf, elementSize, oracleType,
        indOCIHolders_, sizesP, 0, 0, 0, OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }
}

void oracle_vector_use_type_backend::bind_by_name_bulk(
    std::string const &name, void *data, exchange_type type,
    std::size_t begin, std::size_t * end)
{
    data_ = data; // for future reference
    type_ = type; // for future reference
    begin_ = begin;
    end_ = end;

    end_var_ = full_size();
    
    ub2 oracleType;
    sb4 elementSize;
    void * dataBuf;

    prepare_for_bind(dataBuf, elementSize, oracleType);

    ub2 *sizesP = 0; // used only for std::string
    if (type == x_stdstring)
    {
        sizesP = &sizes_[0];
    }

    sword res = OCIBindByName(statement_.stmtp_, &bindp_,
        statement_.session_.errhp_,
        reinterpret_cast<text*>(const_cast<char*>(name.c_str())),
        static_cast<sb4>(name.size()),
        dataBuf, elementSize, oracleType,
        indOCIHolders_, sizesP, 0, 0, 0, OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }
}

void oracle_vector_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    if (type_ == x_stdstring)
    {
        // nothing to do - it's already done during bind
        // (and it's probably impossible to separate them, because
        // changes in the string size could not be handled here)
    }
    else if (type_ == x_long_long)
    {
        std::vector<long long> *vp
            = static_cast<std::vector<long long> *>(data_);
        std::vector<long long> &v(*vp);

        char *pos = buf_;
        std::size_t const entrySize = 100; // arbitrary, but consistent
        std::size_t const vecSize = size();
        for (std::size_t i = 0; i != vecSize; ++i)
        {
            snprintf(pos, entrySize, "%" LL_FMT_FLAGS "d", v[begin_ + i]);
            pos += entrySize;
        }
    }
    else if (type_ == x_unsigned_long_long)
    {
        std::vector<unsigned long long> *vp
            = static_cast<std::vector<unsigned long long> *>(data_);
        std::vector<unsigned long long> &v(*vp);

        char *pos = buf_;
        std::size_t const entrySize = 100; // arbitrary, but consistent
        std::size_t const vecSize = size();
        for (std::size_t i = 0; i != vecSize; ++i)
        {
            snprintf(pos, entrySize, "%" LL_FMT_FLAGS "u", v[begin_ + i]);
            pos += entrySize;
        }
    }
    else if (type_ == x_stdtm)
    {
        std::vector<std::tm> *vp
            = static_cast<std::vector<std::tm> *>(data_);
        std::vector<std::tm> &v(*vp);

        ub1* pos = reinterpret_cast<ub1*>(buf_);
        std::size_t const vecSize = size();
        for (std::size_t i = 0; i != vecSize; ++i)
        {
            std::tm & t = v[begin_ + i];
            
            *pos++ = static_cast<ub1>(100 + (1900 + t.tm_year) / 100);
            *pos++ = static_cast<ub1>(100 + t.tm_year % 100);
            *pos++ = static_cast<ub1>(t.tm_mon + 1);
            *pos++ = static_cast<ub1>(t.tm_mday);
            *pos++ = static_cast<ub1>(t.tm_hour + 1);
            *pos++ = static_cast<ub1>(t.tm_min + 1);
            *pos++ = static_cast<ub1>(t.tm_sec + 1);
        }
    }

    // then handle indicators
    if (ind != NULL)
    {
        std::size_t const vecSize = size();
        for (std::size_t i = 0; i != vecSize; ++i)
        {
            if (ind[begin_ + i] == i_null)
            {
                indOCIHolderVec_[i] = -1; // null
            }
            else
            {
                indOCIHolderVec_[i] = 0;  // value is OK
            }
        }
    }
    else
    {
        // no indicators - treat all fields as OK
        std::size_t const vecSize = size();
        for (std::size_t i = 0; i != vecSize; ++i)
        {
            indOCIHolderVec_[i] = 0;  // value is OK
        }
    }
}

std::size_t oracle_vector_use_type_backend::size()
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

std::size_t oracle_vector_use_type_backend::full_size()
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
            std::vector<long long> *vp
                = static_cast<std::vector<long long> *>(data_);
            sz = vp->size();
        }
        break;
    case x_unsigned_long_long:
        {
            std::vector<unsigned long long> *vp
                = static_cast<std::vector<unsigned long long> *>(data_);
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

    case x_xmltype:    break; // not supported
    case x_longstring: break; // not supported
    case x_statement:  break; // not supported
    case x_rowid:      break; // not supported
    case x_blob:       break; // not supported
    }

    return sz;
}

void oracle_vector_use_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }

    if (bindp_ != NULL)
    {
        OCIHandleFree(bindp_, OCI_HTYPE_DEFINE);
        bindp_ = NULL;
    }
}
