//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define soci_ORACLE_SOURCE

#include "soci-oracle.h"
#include "error.h"
#include <soci-backend.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace soci::details::oracle;

oracle_statement_backend::oracle_statement_backend(oracle_session_backend &session)
    : session_(session), stmtp_(NULL), boundByName_(false), boundByPos_(false),
      noData_(false)
{
}

void oracle_statement_backend::alloc()
{
    sword res = OCIHandleAlloc(session_.envhp_,
        reinterpret_cast<dvoid**>(&stmtp_),
        OCI_HTYPE_STMT, 0, 0);
    if (res != OCI_SUCCESS)
    {
        throw soci_error("Cannot allocate statement handle");
    }
}

void oracle_statement_backend::clean_up()
{
    // deallocate statement handle
    if (stmtp_ != NULL)
    {
        OCIHandleFree(stmtp_, OCI_HTYPE_STMT);
        stmtp_ = NULL;
    }

    boundByName_ = false;
    boundByPos_ = false;
}

void oracle_statement_backend::prepare(std::string const &query,
    statement_type /* eType */)
{
    sb4 stmtLen = static_cast<sb4>(query.size());
    sword res = OCIStmtPrepare(stmtp_,
        session_.errhp_,
        reinterpret_cast<text*>(const_cast<char*>(query.c_str())),
        stmtLen, OCI_V7_SYNTAX, OCI_DEFAULT);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }
}

statement_backend::exec_fetch_result oracle_statement_backend::execute(int number)
{
    sword res = OCIStmtExecute(session_.svchp_, stmtp_, session_.errhp_,
        static_cast<ub4>(number), 0, 0, 0, OCI_DEFAULT);

    if (res == OCI_SUCCESS || res == OCI_SUCCESS_WITH_INFO)
    {
        noData_ = false;
        return ef_success;
    }
    else if (res == OCI_NO_DATA)
    {
        noData_ = true;
        return ef_no_data;
    }
    else
    {
        throw_oracle_soci_error(res, session_.errhp_);
        return ef_no_data; // unreachable dummy return to please the compiler
    }
}

statement_backend::exec_fetch_result oracle_statement_backend::fetch(int number)
{
    if (noData_)
    {
        return ef_no_data;
    }

    sword res = OCIStmtFetch(stmtp_, session_.errhp_,
        static_cast<ub4>(number), OCI_FETCH_NEXT, OCI_DEFAULT);

    if (res == OCI_SUCCESS || res == OCI_SUCCESS_WITH_INFO)
    {
        return ef_success;
    }
    else if (res == OCI_NO_DATA)
    {
        noData_ = true;
        return ef_no_data;
    }
    else
    {
        throw_oracle_soci_error(res, session_.errhp_);
        return ef_no_data; // unreachable dummy return to please the compiler
    }
}

long long oracle_statement_backend::get_affected_rows()
{
    ub4 row_count;
    sword res = OCIAttrGet(static_cast<dvoid*>(stmtp_),
        OCI_HTYPE_STMT, &row_count,
        0, OCI_ATTR_ROW_COUNT, session_.errhp_);

    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return row_count;
}

int oracle_statement_backend::get_number_of_rows()
{
    int rows;
    sword res = OCIAttrGet(static_cast<dvoid*>(stmtp_),
        OCI_HTYPE_STMT, static_cast<dvoid*>(&rows),
        0, OCI_ATTR_ROWS_FETCHED, session_.errhp_);

    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return rows;
}

std::string oracle_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    std::string newQuery("begin ");
    newQuery += query;
    newQuery += "; end;";
    return newQuery;
}

int oracle_statement_backend::prepare_for_describe()
{
    sword res = OCIStmtExecute(session_.svchp_, stmtp_, session_.errhp_,
        1, 0, 0, 0, OCI_DESCRIBE_ONLY);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    int cols;
    res = OCIAttrGet(static_cast<dvoid*>(stmtp_),
        static_cast<ub4>(OCI_HTYPE_STMT), static_cast<dvoid*>(&cols),
        0, static_cast<ub4>(OCI_ATTR_PARAM_COUNT), session_.errhp_);

    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return cols;
}

void oracle_statement_backend::describe_column(int colNum, data_type &type,
    std::string &columnName)
{
    int size;
    int precision;
    int scale;

    ub2 dbtype;
    text* dbname;
    ub4 nameLength;

    ub2 dbsize;
    sb2 dbprec;
    ub1 dbscale; //sb2 in some versions of Oracle?

    // Get the column handle
    OCIParam* colhd;
    sword res = OCIParamGet(reinterpret_cast<dvoid*>(stmtp_),
        static_cast<ub4>(OCI_HTYPE_STMT),
        reinterpret_cast<OCIError*>(session_.errhp_),
        reinterpret_cast<dvoid**>(&colhd),
        static_cast<ub4>(colNum));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // Get the column name
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
        static_cast<ub4>(OCI_DTYPE_PARAM),
        reinterpret_cast<dvoid**>(&dbname),
        reinterpret_cast<ub4*>(&nameLength),
        static_cast<ub4>(OCI_ATTR_NAME),
        reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // Get the column type
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
        static_cast<ub4>(OCI_DTYPE_PARAM),
        reinterpret_cast<dvoid*>(&dbtype),
        0,
        static_cast<ub4>(OCI_ATTR_DATA_TYPE),
        reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // get the data size
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
        static_cast<ub4>(OCI_DTYPE_PARAM),
        reinterpret_cast<dvoid*>(&dbsize),
        0,
        static_cast<ub4>(OCI_ATTR_DATA_SIZE),
        reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // get the precision
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
        static_cast<ub4>(OCI_DTYPE_PARAM),
        reinterpret_cast<dvoid*>(&dbprec),
        0,
        static_cast<ub4>(OCI_ATTR_PRECISION),
        reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // get the scale
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
        static_cast<ub4>(OCI_DTYPE_PARAM),
        reinterpret_cast<dvoid*>(&dbscale),
        0,
        static_cast<ub4>(OCI_ATTR_SCALE),
        reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    columnName.assign(dbname, dbname + nameLength);
    size = static_cast<int>(dbsize);
    precision = static_cast<int>(dbprec);
    scale = static_cast<int>(dbscale);

    switch (dbtype)
    {
    case SQLT_CHR:
    case SQLT_AFC:
        type = dt_string;
        break;
    case SQLT_NUM:
        if (scale > 0)
        {
            if (session_.get_option_decimals_as_strings())
                type = dt_string;
            else
                type = dt_double;
        }
        else if (precision <= std::numeric_limits<int>::digits10)
        {
            type = dt_integer;
        }
        else
        {
            type = dt_long_long;
        }
        break;
    case SQLT_DAT:
        type = dt_date;
        break;
    }
}

std::size_t oracle_statement_backend::column_size(int position)
{
    // Note: we may want to optimize so that the OCI_DESCRIBE_ONLY call
    // happens only once per statement.
    // Possibly use existing statement::describe() / make column prop
    // access lazy at same time

    int colSize(0);

    sword res = OCIStmtExecute(session_.svchp_, stmtp_,
         session_.errhp_, 1, 0, 0, 0, OCI_DESCRIBE_ONLY);
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    // Get The Column Handle
    OCIParam* colhd;
    res = OCIParamGet(reinterpret_cast<dvoid*>(stmtp_),
         static_cast<ub4>(OCI_HTYPE_STMT),
         reinterpret_cast<OCIError*>(session_.errhp_),
         reinterpret_cast<dvoid**>(&colhd),
         static_cast<ub4>(position));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

     // Get The Data Size
    res = OCIAttrGet(reinterpret_cast<dvoid*>(colhd),
         static_cast<ub4>(OCI_DTYPE_PARAM),
         reinterpret_cast<dvoid*>(&colSize),
         0,
         static_cast<ub4>(OCI_ATTR_DATA_SIZE),
         reinterpret_cast<OCIError*>(session_.errhp_));
    if (res != OCI_SUCCESS)
    {
        throw_oracle_soci_error(res, session_.errhp_);
    }

    return static_cast<std::size_t>(colSize);
}
