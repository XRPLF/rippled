//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_DB2_SOURCE
#include "soci/db2/soci-db2.h"
#include <cctype>

using namespace soci;
using namespace soci::details;

db2_statement_backend::db2_statement_backend(db2_session_backend &session)
    : session_(session),hasVectorUseElements(false),use_binding_method_(details::db2::BOUND_BY_NONE)
{
}

void db2_statement_backend::alloc()
{
    SQLRETURN cliRC = SQL_SUCCESS;

    cliRC = SQLAllocHandle(SQL_HANDLE_STMT,session_.hDbc,&hStmt);
    if (cliRC != SQL_SUCCESS) {
        throw db2_soci_error("Error while allocation statement handle",cliRC);
    }
}

void db2_statement_backend::clean_up()
{
    SQLRETURN cliRC = SQL_SUCCESS;

    cliRC=SQLFreeHandle(SQL_HANDLE_STMT,hStmt);
    if (cliRC != SQL_SUCCESS) {
        throw db2_soci_error(db2_soci_error::sqlState("Statement handle clean-up error",SQL_HANDLE_STMT,hStmt),cliRC);
    }
}

void db2_statement_backend::prepare(std::string const &  query ,
    statement_type /* eType */)
{
    // rewrite the query by transforming all named parameters into
    // the markers (:abc -> ?, etc.)

    enum { normal, in_quotes, in_name } state = normal;

    std::string name;

    for (std::string::const_iterator it = query.begin(), end = query.end();
         it != end; ++it)
    {
        switch (state)
        {
        case normal:
            if (*it == '\'')
            {
                query_ += *it;
                state = in_quotes;
            }
            else if (*it == ':')
            {
                // Check whether this is a cast operator (e.g. 23::float)
                // and treat it as a special case, not as a named binding
                const std::string::const_iterator next_it = it + 1;
                if ((next_it != end) && (*next_it == ':'))
                {
                    query_ += "::";
                    ++it;
                }
                else
                {
                    state = in_name;
                }
            }
            else // regular character, stay in the same state
            {
                query_ += *it;
            }
            break;
        case in_quotes:
            if (*it == '\'')
            {
                query_ += *it;
                state = normal;
            }
            else // regular quoted character
            {
                query_ += *it;
            }
            break;
        case in_name:
            if (std::isalnum(*it) || *it == '_')
            {
                name += *it;
            }
            else // end of name
            {
                names_.push_back(name);
                name.clear();
                std::ostringstream ss;
                ss << '?';
                query_ += ss.str();
                query_ += *it;
                state = normal;

            }
            break;
        }
    }

    if (state == in_name)
    {
        names_.push_back(name);
        std::ostringstream ss;
        ss << '?';
        query_ += ss.str();
    }

    SQLRETURN cliRC = SQLPrepare(hStmt, const_cast<SQLCHAR *>((const SQLCHAR *) query_.c_str()), SQL_NTS);
    if (cliRC!=SQL_SUCCESS) {
        throw db2_soci_error("Error while preparing query",cliRC);
    }
}

statement_backend::exec_fetch_result
db2_statement_backend::execute(int  number )
{
    SQLUINTEGER rows_processed = 0;
    SQLRETURN cliRC;

    if (hasVectorUseElements)
    {
        SQLSetStmtAttr(hStmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &rows_processed, 0);
    }

    // if we are called twice for the same statement we need to close the open
    // cursor or an "invalid cursor state" error will occur on execute
    cliRC = SQLFreeStmt(hStmt,SQL_CLOSE);
    if (cliRC != SQL_SUCCESS)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Statement execution error",SQL_HANDLE_STMT,hStmt),cliRC);
    }

    cliRC = SQLExecute(hStmt);
    if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO && cliRC != SQL_NO_DATA)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Statement execution error",SQL_HANDLE_STMT,hStmt),cliRC);
    }

    SQLSMALLINT colCount;
    SQLNumResultCols(hStmt, &colCount);

    if (number > 0 && colCount > 0)
    {
        return fetch(number);
    }

    return ef_success;
}

statement_backend::exec_fetch_result
db2_statement_backend::fetch(int  number )
{
    numRowsFetched = 0;

    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_BIND_TYPE, SQL_BIND_BY_COLUMN, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, db2::int_as_ptr(number), 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &numRowsFetched, 0);

    SQLRETURN cliRC = SQLFetch(hStmt);

    if (SQL_NO_DATA == cliRC)
    {
        return ef_no_data;
    }

    if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Error while fetching data", SQL_HANDLE_STMT, hStmt), cliRC);
    }

    return ef_success;
}

long long db2_statement_backend::get_affected_rows()
{
    SQLLEN rows;

    SQLRETURN cliRC = SQLRowCount(hStmt, &rows);
    if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Error while getting affected row count", SQL_HANDLE_STMT, hStmt), cliRC);
    }
    else if (rows == -1)
    {
        throw soci_error("Error getting affected row count: statement did not perform an update, insert, delete, or merge");
    }

    return rows;
}

int db2_statement_backend::get_number_of_rows()
{
    return numRowsFetched;
}

std::string db2_statement_backend::get_parameter_name(int index) const
{
    return names_.at(index);
}

std::string db2_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    return query;
}

int db2_statement_backend::prepare_for_describe()
{
    SQLSMALLINT numCols;
    SQLNumResultCols(hStmt, &numCols);
    return numCols;
}

void db2_statement_backend::describe_column(int  colNum,
    data_type &  type, std::string & columnName )
{
SQLCHAR colNameBuffer[2048];
    SQLSMALLINT colNameBufferOverflow;
    SQLSMALLINT dataType;
    SQLULEN colSize;
    SQLSMALLINT decDigits;
    SQLSMALLINT isNullable;

    SQLRETURN cliRC = SQLDescribeCol(hStmt, static_cast<SQLUSMALLINT>(colNum),
                                  colNameBuffer, 2048,
                                  &colNameBufferOverflow, &dataType,
                                  &colSize, &decDigits, &isNullable);

    if (cliRC != SQL_SUCCESS)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Error while describing column",SQL_HANDLE_STMT,hStmt),cliRC);
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

size_t db2_statement_backend::column_size(int col) {
    SQLCHAR colNameBuffer[2048];
    SQLSMALLINT colNameBufferOverflow;
    SQLSMALLINT dataType;
    SQLULEN colSize;
    SQLSMALLINT decDigits;
    SQLSMALLINT isNullable;

    SQLRETURN cliRC = SQLDescribeCol(hStmt, static_cast<SQLUSMALLINT>(col),
                                  colNameBuffer, 2048,
                                  &colNameBufferOverflow, &dataType,
                                  &colSize, &decDigits, &isNullable);

    if (cliRC != SQL_SUCCESS)
    {
        throw db2_soci_error(db2_soci_error::sqlState("Error while detecting column size",SQL_HANDLE_STMT,hStmt),cliRC);
    }

    return colSize;
}

db2_standard_into_type_backend * db2_statement_backend::make_into_type_backend()
{
    return new db2_standard_into_type_backend(*this);
}

db2_standard_use_type_backend * db2_statement_backend::make_use_type_backend()
{
    return new db2_standard_use_type_backend(*this);
}

db2_vector_into_type_backend *
db2_statement_backend::make_vector_into_type_backend()
{
    return new db2_vector_into_type_backend(*this);
}

db2_vector_use_type_backend * db2_statement_backend::make_vector_use_type_backend()
{
    hasVectorUseElements = true;
    return new db2_vector_use_type_backend(*this);
}
