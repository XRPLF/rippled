//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "soci/blob.h"
#include "error.h"
#include "soci/rowid.h"
#include "soci/statement.h"
#include "soci/soci-platform.h"
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
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

oracle_standard_into_type_backend *
oracle_statement_backend::make_into_type_backend()
{
    return new oracle_standard_into_type_backend(*this);
}

oracle_standard_use_type_backend *
oracle_statement_backend::make_use_type_backend()
{
    return new oracle_standard_use_type_backend(*this);
}

oracle_vector_into_type_backend *
oracle_statement_backend::make_vector_into_type_backend()
{
    return new oracle_vector_into_type_backend(*this);
}

oracle_vector_use_type_backend *
oracle_statement_backend::make_vector_use_type_backend()
{
    return new oracle_vector_use_type_backend(*this);
}

void oracle_standard_into_type_backend::define_by_pos(
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
        oracleType = SQLT_AFC;
        size = sizeof(char);
        break;
    case x_short:
        oracleType = SQLT_INT;
        size = sizeof(short);
        break;
    case x_integer:
        oracleType = SQLT_INT;
        size = sizeof(int);
        break;
    case x_double:
        oracleType = statement_.session_.get_double_sql_type();
        size = sizeof(double);
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
        size = 32769;  // support selecting strings from LONG columns
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

    sword res = OCIDefineByPos(statement_.stmtp_, &defnp_,
            statement_.session_.errhp_,
            position++, data, size, oracleType,
            &indOCIHolder_, 0, &rCode_, OCI_DEFAULT);

    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }
}

void oracle_standard_into_type_backend::pre_exec(int /* num */)
{
    if (type_ == x_xmltype || type_ == x_longstring)
    {
        // lazy initialization of the temporary LOB object
        
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
}

void oracle_standard_into_type_backend::pre_fetch()
{
    // nothing to do except with Statement into objects

    if (type_ == x_statement)
    {
        statement *st = static_cast<statement *>(data_);
        st->undefine_and_bind();
    }
}

void oracle_standard_into_type_backend::read_from_lob(OCILobLocator * lobp, std::string & value)
{
    ub4 len;

    sword res = OCILobGetLength(statement_.session_.svchp_, statement_.session_.errhp_,
        lobp, &len);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, statement_.session_.errhp_);
    }

    std::vector<char> buf(len);

    if (len != 0)
    {
        ub4 offset = 1;

        res = OCILobRead(statement_.session_.svchp_, statement_.session_.errhp_,
            lobp, &len,
            offset, reinterpret_cast<dvoid*>(&buf[0]),
            len, 0, 0, 0, 0);
        if (res != OCI_SUCCESS)
        {
            throw_oracle_soci_error(res, statement_.session_.errhp_);
        }
    }
    
    value.assign(buf.begin(), buf.end());
}

void oracle_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator *ind)
{
    // first, deal with data
    if (gotData)
    {
        // only std::string, std::tm and Statement need special handling
        if (type_ == x_stdstring)
        {
            if (indOCIHolder_ != -1)
            {
                exchange_type_cast<x_stdstring>(data_) = buf_;
            }
        }
        else if (type_ == x_long_long)
        {
            if (indOCIHolder_ != -1)
            {
                exchange_type_cast<x_long_long>(data_) = std::strtoll(buf_, NULL, 10);
            }
        }
        else if (type_ == x_unsigned_long_long)
        {
            if (indOCIHolder_ != -1)
            {
                exchange_type_cast<x_unsigned_long_long>(data_) = std::strtoull(buf_, NULL, 10);
            }
        }
        else if (type_ == x_stdtm)
        {
            if (indOCIHolder_ != -1)
            {
                std::tm& t = exchange_type_cast<x_stdtm>(data_);

                ub1 *pos = reinterpret_cast<ub1*>(buf_);
                int year = (*pos++ - 100) * 100;
                year += *pos++ - 100;
                int const month = *pos++;
                int const day = *pos++;
                int const hour = *pos++ - 1;
                int const minute = *pos++ - 1;
                int const second = *pos++ - 1;

                details::mktime_from_ymdhms(t, year, month, day, hour, minute, second);
            }
        }
        else if (type_ == x_statement)
        {
            statement *st = static_cast<statement *>(data_);
            st->define_and_bind();
        }
        else if (type_ == x_xmltype)
        {
            if (indOCIHolder_ != -1)
            {
                OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

                read_from_lob(lobp, exchange_type_cast<x_xmltype>(data_).value);
            }
        }
        else if (type_ == x_longstring)
        {
            if (indOCIHolder_ != -1)
            {
                OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

                read_from_lob(lobp, exchange_type_cast<x_longstring>(data_).value);
            }
        }
    }

    // then - deal with indicators
    if (calledFromFetch == true && gotData == false)
    {
        // this is a normal end-of-rowset condition,
        // no need to set anything (fetch() will return false)
        return;
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
    else
    {
        if (indOCIHolder_ == -1)
        {
            // fetched null and no indicator - programming error!
            throw soci_error("Null value fetched and no indicator defined.");
        }
    }
}

void oracle_standard_into_type_backend::clean_up()
{
    if (type_ == x_xmltype || type_ == x_longstring)
    {
        OCILobLocator * lobp = static_cast<OCILobLocator *>(ociData_);

        // ignore errors from this call
        (void) OCILobFreeTemporary(statement_.session_.svchp_, statement_.session_.errhp_,
            lobp);
    }
    
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
