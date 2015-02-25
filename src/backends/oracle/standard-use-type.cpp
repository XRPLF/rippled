//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE
#include "soci-oracle.h"
#include "blob.h"
#include "error.h"
#include "rowid.h"
#include "statement.h"
#include <soci-platform.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#define snprintf _snprintf
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

void oracle_standard_use_type_backend::prepare_for_bind(
    void *&data, sb4 &size, ub2 &oracleType, bool readOnly)
{
    readOnly_ = readOnly;

    switch (type_)
    {
    // simple cases
    case x_char:
        oracleType = SQLT_AFC;
        size = sizeof(char);
        if (readOnly)
        {
            buf_ = new char[size];
            data = buf_;
        }
        break;
    case x_short:
        oracleType = SQLT_INT;
        size = sizeof(short);
        if (readOnly)
        {
            buf_ = new char[size];
            data = buf_;
        }
        break;
    case x_integer:
        oracleType = SQLT_INT;
        size = sizeof(int);
        if (readOnly)
        {
            buf_ = new char[size];
            data = buf_;
        }
        break;
    case x_double:
        oracleType = SQLT_FLT;
        size = sizeof(double);
        if (readOnly)
        {
            buf_ = new char[size];
            data = buf_;
        }
        break;

    // cases that require adjustments and buffer management
    case x_long_long:
    case x_unsigned_long_long:
        oracleType = SQLT_STR;
        size = 100; // arbitrary buffer length
        buf_ = new char[size];
        data = buf_;
        break;
    case x_stdstring:
        oracleType = SQLT_STR;
        // 4000 is Oracle max VARCHAR2 size; 32768 is max LONG size
        size = 32769;
        buf_ = new char[size];
        data = buf_;
        break;
    case x_stdtm:
        oracleType = SQLT_DAT;
        size = 7 * sizeof(ub1);
        buf_ = new char[size];
        data = buf_;
        break;

    // cases that require special handling
    case x_statement:
        {
            oracleType = SQLT_RSET;

            statement *st = static_cast<statement *>(data);
            st->alloc();

            oracle_statement_backend *stbe
                = static_cast<oracle_statement_backend *>(st->get_backend());
            size = 0;
            data = &stbe->stmtp_;
        }
        break;
    case x_rowid:
        {
            oracleType = SQLT_RDD;

            rowid *rid = static_cast<rowid *>(data);

            oracle_rowid_backend *rbe
                = static_cast<oracle_rowid_backend *>(rid->get_backend());

            size = 0;
            data = &rbe->rowidp_;
        }
        break;
    case x_blob:
        {
            oracleType = SQLT_BLOB;

            blob *b = static_cast<blob *>(data);

            oracle_blob_backend *bbe
                = static_cast<oracle_blob_backend *>(b->get_backend());

            size = 0;
            data = &bbe->lobp_;
        }
        break;
    }
}

void oracle_standard_use_type_backend::bind_by_pos(
    int &position, void *data, exchange_type type, bool readOnly)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data; // for future reference
    type_ = type; // for future reference

    ub2 oracleType;
    sb4 size;

    prepare_for_bind(data, size, oracleType, readOnly);

    sword res = OCIBindByPos(statement_.stmtp_, &bindp_,
        statement_.session_.errhp_,
        position++, data, size, oracleType,
        &indOCIHolder_, 0, 0, 0, 0, OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }

    statement_.boundByPos_ = true;
}

void oracle_standard_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type, bool readOnly)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data; // for future reference
    type_ = type; // for future reference

    ub2 oracleType;
    sb4 size;

    prepare_for_bind(data, size, oracleType, readOnly);

    sword res = OCIBindByName(statement_.stmtp_, &bindp_,
        statement_.session_.errhp_,
        reinterpret_cast<text*>(const_cast<char*>(name.c_str())),
        static_cast<sb4>(name.size()),
        data, size, oracleType,
        &indOCIHolder_, 0, 0, 0, 0, OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }

    statement_.boundByName_ = true;
}

void oracle_standard_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    switch (type_)
    {
    case x_char:
        if (readOnly_)
        {
            buf_[0] = *static_cast<char *>(data_);
        }
        break;
    case x_short:
        if (readOnly_)
        {
            *static_cast<short *>(static_cast<void *>(buf_)) = *static_cast<short *>(data_);
        }
        break;
    case x_integer:
        if (readOnly_)
        {
            *static_cast<int *>(static_cast<void *>(buf_)) = *static_cast<int *>(data_);
        }
        break;
    case x_long_long:
        {
            size_t const size = 100; // arbitrary, but consistent with prepare_for_bind
            snprintf(buf_, size, "%" LL_FMT_FLAGS "d", *static_cast<long long *>(data_));
        }
        break;
    case x_unsigned_long_long:
        {
            size_t const size = 100; // arbitrary, but consistent with prepare_for_bind
            snprintf(buf_, size, "%" LL_FMT_FLAGS "u", *static_cast<unsigned long long *>(data_));
        }
        break;
    case x_double:
        if (readOnly_)
        {
            *static_cast<double *>(static_cast<void *>(buf_)) = *static_cast<double *>(data_);
        }
        break;
    case x_stdstring:
        {
            std::string *s = static_cast<std::string *>(data_);

            // 4000 is Oracle max VARCHAR2 size; 32768 is max LONG size
            std::size_t const bufSize = 32769;
            std::size_t const sSize = s->size();
            std::size_t const toCopy =
                sSize < bufSize -1 ? sSize + 1 : bufSize - 1;
            strncpy(buf_, s->c_str(), toCopy);
            buf_[toCopy] = '\0';
        }
        break;
    case x_stdtm:
        {
            std::tm *t = static_cast<std::tm *>(data_);
            ub1* pos = reinterpret_cast<ub1*>(buf_);

            *pos++ = static_cast<ub1>(100 + (1900 + t->tm_year) / 100);
            *pos++ = static_cast<ub1>(100 + t->tm_year % 100);
            *pos++ = static_cast<ub1>(t->tm_mon + 1);
            *pos++ = static_cast<ub1>(t->tm_mday);
            *pos++ = static_cast<ub1>(t->tm_hour + 1);
            *pos++ = static_cast<ub1>(t->tm_min + 1);
            *pos = static_cast<ub1>(t->tm_sec + 1);
        }
        break;
    case x_statement:
        {
            statement *s = static_cast<statement *>(data_);

            s->undefine_and_bind();
        }
        break;
    case x_rowid:
    case x_blob:
        // nothing to do
        break;
    }

    // then handle indicators
    if (ind != NULL && *ind == i_null)
    {
        indOCIHolder_ = -1; // null
    }
    else
    {
        indOCIHolder_ = 0;  // value is OK
    }
}

void oracle_standard_use_type_backend::post_use(bool gotData, indicator *ind)
{
    // It is possible to have the bound element being overwritten
    // by the database.
    //
    // With readOnly_ == true the propagation of modification should *not*
    // take place and in addition the attempt of modification should be detected and reported.

    // first, deal with data
    if (gotData)
    {
        switch (type_)
        {
        case x_char:
            if (readOnly_)
            {
                const char original = *static_cast<char *>(data_);
                const char bound = buf_[0];

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_short:
            if (readOnly_)
            {
                const short original = *static_cast<short *>(data_);
                const short bound = *static_cast<short *>(static_cast<void *>(buf_));

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_integer:
            if (readOnly_)
            {
                const int original = *static_cast<int *>(data_);
                const int bound = *static_cast<int *>(static_cast<void *>(buf_));

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_long_long:
            if (readOnly_)
            {
                long long const original = *static_cast<long long *>(data_);
                long long const bound = std::strtoll(buf_, NULL, 10);

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_unsigned_long_long:
            if (readOnly_)
            {
                unsigned long long const original = *static_cast<unsigned long long *>(data_);
                unsigned long long const bound = std::strtoull(buf_, NULL, 10);

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_double:
            if (readOnly_)
            {
                const double original = *static_cast<double *>(data_);
                const double bound = *static_cast<double *>(static_cast<void *>(buf_));

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_stdstring:
            {
                std::string & original = *static_cast<std::string *>(data_);
                if (original != buf_)
                {
                    if (readOnly_)
                    {
                        throw soci_error("Attempted modification of const use element");
                    }
                    else
                    {
                        original = buf_;
                    }
                }
            }
            break;
        case x_stdtm:
            {
                std::tm & original = *static_cast<std::tm *>(data_);

                std::tm bound;
                ub1 *pos = reinterpret_cast<ub1*>(buf_);
                bound.tm_isdst = -1;
                bound.tm_year = (*pos++ - 100) * 100;
                bound.tm_year += *pos++ - 2000;
                bound.tm_mon = *pos++ - 1;
                bound.tm_mday = *pos++;
                bound.tm_hour = *pos++ - 1;
                bound.tm_min = *pos++ - 1;
                bound.tm_sec = *pos++ - 1;

                if (original.tm_year != bound.tm_year ||
                    original.tm_mon != bound.tm_mon ||
                    original.tm_mday != bound.tm_mday ||
                    original.tm_hour != bound.tm_hour ||
                    original.tm_min != bound.tm_min ||
                    original.tm_sec != bound.tm_sec)
                {
                    if (readOnly_)
                    {
                        throw soci_error("Attempted modification of const use element");
                    }
                    else
                    {
                        original = bound;

                        // normalize and compute the remaining fields
                        std::mktime(&original);
                    }
                }
            }
            break;
        case x_statement:
            {
                statement *s = static_cast<statement *>(data_);
                s->define_and_bind();
            }
            break;
        case x_rowid:
        case x_blob:
            // nothing to do here
            break;
        }
    }

    if (ind != NULL)
    {
        if (gotData)
        {
            if (indOCIHolder_ == 0)
            {
                *ind = i_ok;
            }
            else if (indOCIHolder_ == -1)
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

void oracle_standard_use_type_backend::clean_up()
{
    if (bindp_ != NULL)
    {
        OCIHandleFree(bindp_, OCI_HTYPE_DEFINE);
        bindp_ = NULL;
    }

    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
