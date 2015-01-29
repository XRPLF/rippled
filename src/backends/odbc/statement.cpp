//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci-odbc.h"
#include <cctype>
#include <sstream>
#include <cstring>

#ifdef _MSC_VER
// disables the warning about converting int to void*.  This is a 64 bit compatibility
// warning, but odbc requires the value to be converted on this line
// SQLSetStmtAttr(hstmt_, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)number, 0);
#pragma warning(disable:4312)
#endif

using namespace soci;
using namespace soci::details;


odbc_statement_backend::odbc_statement_backend(odbc_session_backend &session)
    : session_(session), hstmt_(0), numRowsFetched_(0),
      hasVectorUseElements_(false), boundByName_(false), boundByPos_(false),
      rowsAffected_(-1LL)
{
}

void odbc_statement_backend::alloc()
{
    SQLRETURN rc;

    // Allocate environment handle
    rc = SQLAllocHandle(SQL_HANDLE_STMT, session_.hdbc_, &hstmt_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, session_.hdbc_,
            "Allocating statement");
    }
}

void odbc_statement_backend::clean_up()
{
    rowsAffected_ = -1LL;

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt_);
}


void odbc_statement_backend::prepare(std::string const & query,
    statement_type /* eType */)
{
    // rewrite the query by transforming all named parameters into
    // the ODBC numbers ones (:abc -> $1, etc.)

    enum { eNormal, eInQuotes, eInName, eInAccessDate } state = eNormal;

    std::string name;
    query_.reserve(query.length());

    for (std::string::const_iterator it = query.begin(), end = query.end();
         it != end; ++it)
    {
        switch (state)
        {
        case eNormal:
            if (*it == '\'')
            {
                query_ += *it;
                state = eInQuotes;
            }
            else if (*it == '#')
            {
                query_ += *it;
                state = eInAccessDate;
            }
            else if (*it == ':')
            {
                state = eInName;
            }
            else // regular character, stay in the same state
            {
                query_ += *it;
            }
            break;
        case eInQuotes:
            if (*it == '\'')
            {
                query_ += *it;
                state = eNormal;
            }
            else // regular quoted character
            {
                query_ += *it;
            }
            break;
        case eInName:
            if (std::isalnum(*it) || *it == '_')
            {
                name += *it;
            }
            else // end of name
            {
                names_.push_back(name);
                name.clear();
                query_ += "?";
                query_ += *it;
                state = eNormal;
            }
            break;
        case eInAccessDate:
            if (*it == '#')
            {
                query_ += *it;
                state = eNormal;
            }
            else // regular quoted character
            {
                query_ += *it;
            }
            break;
        }
    }

    if (state == eInName)
    {
        names_.push_back(name);
        query_ += "?";
    }

    SQLRETURN rc = SQLPrepare(hstmt_, (SQLCHAR*)query_.c_str(), (SQLINTEGER)query_.size());
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                         query_.c_str());
    }
}

statement_backend::exec_fetch_result
odbc_statement_backend::execute(int number)
{
    // Store the number of rows processed by this call.
    SQLULEN rows_processed = 0;
    if (hasVectorUseElements_)
    {
        SQLSetStmtAttr(hstmt_, SQL_ATTR_PARAMS_PROCESSED_PTR, &rows_processed, 0);
    }
    
    // if we are called twice for the same statement we need to close the open
    // cursor or an "invalid cursor state" error will occur on execute
    SQLCloseCursor(hstmt_);
    
    SQLRETURN rc = SQLExecute(hstmt_);
    if (is_odbc_error(rc))
    {
        // If executing bulk operation a partial 
        // number of rows affected may be available.
        if (hasVectorUseElements_)
        {
            rowsAffected_ = 0;

            do
            {
                SQLLEN res = 0;
                // SQLRowCount will return error after a partially executed statement.
                // SQL_DIAG_ROW_COUNT returns the same info but must be collected immediatelly after the execution.
                rc = SQLGetDiagField(SQL_HANDLE_STMT, hstmt_, 0, SQL_DIAG_ROW_COUNT, &res, 0, NULL);
                if (!is_odbc_error(rc) && res > 0) // 'res' will be -1 for the where the statement failed.
                {
                    rowsAffected_ += res;
                }
                --rows_processed; // Avoid unnecessary calls to SQLGetDiagField
            }
            // Move forward to the next result while there are rows processed.
            while (rows_processed > 0 && SQLMoreResults(hstmt_) == SQL_SUCCESS);
        }
        throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                         "Statement Execute");
    }
    // We should preserve the number of rows affected here 
    // where we know for sure that a bulk operation was executed.
    else
    {
        rowsAffected_ = 0;

        do {
            SQLLEN res = 0;
            SQLRETURN rc = SQLRowCount(hstmt_, &res);
            if (is_odbc_error(rc))
            {
                throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                                  "Getting number of affected rows");
            }
            rowsAffected_ += res;
        }
        // Move forward to the next result if executing a bulk operation.
        while (hasVectorUseElements_ && SQLMoreResults(hstmt_) == SQL_SUCCESS);
    }
    SQLSMALLINT colCount;
    SQLNumResultCols(hstmt_, &colCount);

    if (number > 0 && colCount > 0)
    {
        return fetch(number);
    }

    return ef_success;
}

statement_backend::exec_fetch_result
odbc_statement_backend::fetch(int number)
{
    numRowsFetched_ = 0;
    SQLULEN const row_array_size = static_cast<SQLULEN>(number);

    SQLSetStmtAttr(hstmt_, SQL_ATTR_ROW_BIND_TYPE, SQL_BIND_BY_COLUMN, 0);
    SQLSetStmtAttr(hstmt_, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)row_array_size, 0);
    SQLSetStmtAttr(hstmt_, SQL_ATTR_ROWS_FETCHED_PTR, &numRowsFetched_, 0);

    SQLRETURN rc = SQLFetch(hstmt_);

    if (SQL_NO_DATA == rc)
    {
        return ef_no_data;
    }

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                         "Statement Fetch");
    }

    return ef_success;
}

long long odbc_statement_backend::get_affected_rows()
{
    return rowsAffected_;
}

int odbc_statement_backend::get_number_of_rows()
{
    return numRowsFetched_;
}

std::string odbc_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    return query;
}

int odbc_statement_backend::prepare_for_describe()
{
    SQLSMALLINT numCols;
    SQLNumResultCols(hstmt_, &numCols);
    return numCols;
}

void odbc_statement_backend::describe_column(int colNum, data_type & type,
                                          std::string & columnName)
{
    SQLCHAR colNameBuffer[2048];
    SQLSMALLINT colNameBufferOverflow;
    SQLSMALLINT dataType;
    SQLULEN colSize;
    SQLSMALLINT decDigits;
    SQLSMALLINT isNullable;

    SQLRETURN rc = SQLDescribeCol(hstmt_, static_cast<SQLUSMALLINT>(colNum),
                                  colNameBuffer, 2048,
                                  &colNameBufferOverflow, &dataType,
                                  &colSize, &decDigits, &isNullable);

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                         "describe Column");
    }

    char const *name = reinterpret_cast<char const *>(colNameBuffer);
    columnName.assign(name, std::strlen(name));

    switch (dataType)
    {
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TYPE_TIMESTAMP:
        type = dt_date;
        break;
    case SQL_DOUBLE:
    case SQL_DECIMAL:
    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_NUMERIC:
        type = dt_double;
        break;
    case SQL_TINYINT:
    case SQL_SMALLINT:
    case SQL_INTEGER:
        type = dt_integer;
        break;
    case SQL_BIGINT:
        type = dt_long_long;
        break;
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    default:
        type = dt_string;
        break;
    }
}

std::size_t odbc_statement_backend::column_size(int colNum)
{
    SQLCHAR colNameBuffer[2048];
    SQLSMALLINT colNameBufferOverflow;
    SQLSMALLINT dataType;
    SQLULEN colSize;
    SQLSMALLINT decDigits;
    SQLSMALLINT isNullable;

    SQLRETURN rc = SQLDescribeCol(hstmt_, static_cast<SQLUSMALLINT>(colNum),
                                  colNameBuffer, 2048,
                                  &colNameBufferOverflow, &dataType,
                                  &colSize, &decDigits, &isNullable);

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, hstmt_,
                         "column size");
    }

    return colSize;
}

odbc_standard_into_type_backend * odbc_statement_backend::make_into_type_backend()
{
    return new odbc_standard_into_type_backend(*this);
}

odbc_standard_use_type_backend * odbc_statement_backend::make_use_type_backend()
{
    return new odbc_standard_use_type_backend(*this);
}

odbc_vector_into_type_backend *
odbc_statement_backend::make_vector_into_type_backend()
{
    return new odbc_vector_into_type_backend(*this);
}

odbc_vector_use_type_backend * odbc_statement_backend::make_vector_use_type_backend()
{
    hasVectorUseElements_ = true;
    return new odbc_vector_use_type_backend(*this);
}
