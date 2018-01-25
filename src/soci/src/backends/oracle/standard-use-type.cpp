//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "soci/blob.h"
#include "error.h"
#include "soci/rowid.h"
#include "soci/statement.h"
#include "soci/type-wrappers.h"
#include "soci/soci-platform.h"

#include "soci-compiler.h"
#include "soci-exchange-cast.h"
#include "soci-mktime.h"

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
        oracleType = statement_.session_.get_double_sql_type();
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
        
    case x_xmltype:
    case x_longstring:
        {
            oracleType = SQLT_CLOB;
            
            // lazy initialization of the temporary LOB object,
            // actual creation of this object is in pre_exec, which
            // is called right before statement's execute
            
            OCILobLocator * lobp = NULL;

            size = sizeof(lobp);
            data = &ociData_;
            ociData_ = lobp;
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

void oracle_standard_use_type_backend::write_to_lob(OCILobLocator * lobp, const std::string & value)
{
    ub4 toWrite = value.size();
    ub4 offset = 1;
    sword res;

    if (toWrite != 0)
    {
        res = OCILobWrite(statement_.session_.svchp_, statement_.session_.errhp_,
            lobp, &toWrite, offset,
            reinterpret_cast<dvoid*>(const_cast<char*>(value.data())),
            toWrite, OCI_ONE_PIECE, 0, 0, 0, SQLCS_IMPLICIT);
        if (res != OCI_SUCCESS)
        {
            throw_oracle_soci_error(res, statement_.session_.errhp_);
        }
    }

    ub4 len;

    res = OCILobGetLength(statement_.session_.svchp_, statement_.session_.errhp_,
        lobp, &len);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }

    if (toWrite < len)
    {
        res = OCILobTrim(statement_.session_.svchp_, statement_.session_.errhp_,
            lobp, toWrite);
        if (res != OCI_SUCCESS)
        {
            throw_oracle_soci_error(res, statement_.session_.errhp_);
        }
    }
}

void oracle_standard_use_type_backend::lazy_temp_lob_init()
{
    OCILobLocator * lobp;
    sword res = OCIDescriptorAlloc(statement_.session_.envhp_,
        reinterpret_cast<dvoid**>(&lobp), OCI_DTYPE_LOB, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }
    
    res = OCILobCreateTemporary(statement_.session_.svchp_,
        statement_.session_.errhp_,
        lobp, 0, SQLCS_IMPLICIT,
        OCI_TEMP_CLOB, OCI_ATTR_NOCACHE, OCI_DURATION_SESSION);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }

    ociData_ = lobp;
}

void oracle_standard_use_type_backend::pre_exec(int /* num */)
{
    switch (type_)
    {
    case x_xmltype:
        {
            // lazy initialization of the temporary LOB object
            
            lazy_temp_lob_init();
            
            OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

            write_to_lob(lobp, exchange_type_cast<x_xmltype>(data_).value);
        }
        break;
    case x_longstring:
        {
            // lazy initialization of the temporary LOB object
            
            lazy_temp_lob_init();
            
            OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

            write_to_lob(lobp, exchange_type_cast<x_longstring>(data_).value);
        }
        break;
    default:
        // nothing to do
        break;
    }
}

void oracle_standard_use_type_backend::pre_use(indicator const *ind)
{
    // first deal with data
    switch (type_)
    {
    case x_char:
        if (readOnly_)
        {
            buf_[0] = exchange_type_cast<x_char>(data_);
        }
        break;
    case x_short:
        if (readOnly_)
        {
            exchange_type_cast<x_short>(buf_) = exchange_type_cast<x_short>(data_);
        }
        break;
    case x_integer:
        if (readOnly_)
        {
            exchange_type_cast<x_integer>(buf_) = exchange_type_cast<x_integer>(data_);
        }
        break;
    case x_long_long:
        {
            size_t const size = 100; // arbitrary, but consistent with prepare_for_bind
            snprintf(buf_, size, "%" LL_FMT_FLAGS "d", exchange_type_cast<x_long_long>(data_));
        }
        break;
    case x_unsigned_long_long:
        {
            size_t const size = 100; // arbitrary, but consistent with prepare_for_bind
            snprintf(buf_, size, "%" LL_FMT_FLAGS "u", exchange_type_cast<x_unsigned_long_long>(data_));
        }
        break;
    case x_double:
        if (readOnly_)
        {
            exchange_type_cast<x_double>(buf_) = exchange_type_cast<x_double>(data_);
        }
        break;
    case x_stdstring:
        {
            std::string const& s = exchange_type_cast<x_stdstring>(data_);

            // 4000 is Oracle max VARCHAR2 size; 32768 is max LONG size
            std::size_t const bufSize = 32769;
            std::size_t const sSize = s.size();
            std::size_t const toCopy =
                sSize < bufSize -1 ? sSize + 1 : bufSize - 1;
            strncpy(buf_, s.c_str(), toCopy);
            buf_[toCopy] = '\0';
        }
        break;
    case x_stdtm:
        {
            std::tm const& t = exchange_type_cast<x_stdtm>(data_);
            ub1* pos = reinterpret_cast<ub1*>(buf_);

            *pos++ = static_cast<ub1>(100 + (1900 + t.tm_year) / 100);
            *pos++ = static_cast<ub1>(100 + t.tm_year % 100);
            *pos++ = static_cast<ub1>(t.tm_mon + 1);
            *pos++ = static_cast<ub1>(t.tm_mday);
            *pos++ = static_cast<ub1>(t.tm_hour + 1);
            *pos++ = static_cast<ub1>(t.tm_min + 1);
            *pos = static_cast<ub1>(t.tm_sec + 1);
        }
        break;
    case x_statement:
        {
            statement *s = static_cast<statement *>(data_);

            s->undefine_and_bind();
        }
        break;
        
    case x_xmltype:
    case x_longstring:
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
                const char original = exchange_type_cast<x_char>(data_);
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
                const short original = exchange_type_cast<x_short>(data_);
                const short bound = exchange_type_cast<x_short>(buf_);

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_integer:
            if (readOnly_)
            {
                const int original = exchange_type_cast<x_integer>(data_);
                const int bound = exchange_type_cast<x_integer>(buf_);

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }
            }
            break;
        case x_long_long:
            if (readOnly_)
            {
                long long const original = exchange_type_cast<x_long_long>(data_);
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
                unsigned long long const original = exchange_type_cast<x_unsigned_long_long>(data_);
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
                const double original = exchange_type_cast<x_double>(data_);
                const double bound = exchange_type_cast<x_double>(buf_);

                // Exact comparison is fine here, they are really supposed to
                // be exactly the same.
                GCC_WARNING_SUPPRESS(float-equal)

                if (original != bound)
                {
                    throw soci_error("Attempted modification of const use element");
                }

                GCC_WARNING_RESTORE(float-equal)
            }
            break;
        case x_stdstring:
            {
                std::string& original = exchange_type_cast<x_stdstring>(data_);
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
                std::tm& original = exchange_type_cast<x_stdtm>(data_);

                ub1 *pos = reinterpret_cast<ub1*>(buf_);
                int year = (*pos++ - 100) * 100;
                year += *pos++ - 100;
                int const month = *pos++;
                int const day = *pos++;
                int const hour = *pos++ - 1;
                int const minute = *pos++ - 1;
                int const second = *pos++ - 1;

                std::tm bound;
                details::mktime_from_ymdhms(bound, year, month, day, hour, minute, second);

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
        case x_xmltype:
        case x_longstring:
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
    if (type_ == x_xmltype || type_ == x_longstring)
    {
        OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

        // ignore errors from this call
        (void) OCILobFreeTemporary(statement_.session_.svchp_, statement_.session_.errhp_,
            lobp);
    }
    
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
