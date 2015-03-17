//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#include "soci-sqlite3.h"
// std
#include <algorithm>
#include <sstream>
#include <string>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace sqlite_api;

sqlite3_statement_backend::sqlite3_statement_backend(
    sqlite3_session_backend &session)
    : session_(session)
    , stmt_(0)
    , dataCache_()
    , useData_(0)
    , databaseReady_(false)
    , boundByName_(false)
    , boundByPos_(false)
    , rowsAffectedBulk_(-1LL)
{
}

void sqlite3_statement_backend::alloc()
{
    // ...
}

void sqlite3_statement_backend::clean_up()
{
    rowsAffectedBulk_ = -1LL;

    if (stmt_)
    {
        sqlite3_finalize(stmt_);
        stmt_ = 0;
        databaseReady_ = false;
    }
}

void sqlite3_statement_backend::prepare(std::string const & query,
    statement_type /* eType */)
{
    clean_up();

    char const* tail = 0; // unused;
    int const res = sqlite3_prepare_v2(session_.conn_,
                              query.c_str(),
                              static_cast<int>(query.size()),
                              &stmt_,
                              &tail);
    if (res != SQLITE_OK)
    {
        char const* zErrMsg = sqlite3_errmsg(session_.conn_);

        std::ostringstream ss;
        ss << "sqlite3_statement_backend::prepare: "
           << zErrMsg;
        throw soci_error(ss.str());
    }
    databaseReady_ = true;
}

// sqlite3_reset needs to be called before a prepared statment can
// be executed a second time.
void sqlite3_statement_backend::reset_if_needed()
{
    if (stmt_ && databaseReady_ == false)
    {
        int const res = sqlite3_reset(stmt_);
        if (SQLITE_OK == res)
        {
            databaseReady_ = true;
        }
    }
}

// This is used by bulk operations
statement_backend::exec_fetch_result
sqlite3_statement_backend::load_rowset(int totalRows)
{
    statement_backend::exec_fetch_result retVal = ef_success;
    int numCols = -1;
    int i = 0;

    if (!databaseReady_)
    {
        retVal = ef_no_data;
    }
    else
    {
        // make the vector big enough to hold the data we need
        dataCache_.resize(totalRows);

        for (i = 0; i < totalRows && databaseReady_; ++i)
        {
            int const res = sqlite3_step(stmt_);

            if (SQLITE_DONE == res)
            {
                databaseReady_ = false;
                retVal = ef_no_data;
                break;
            }
            else if (SQLITE_ROW == res)
            {
                // only need to set the number of columns once
                if (-1 == numCols)
                {
                    numCols = sqlite3_column_count(stmt_);
                    for (sqlite3_recordset::iterator it = dataCache_.begin(),
                         end = dataCache_.end(); it != end; ++it)
                    {
                        (*it).resize(numCols);
                    }
                }
                for (int c = 0; c < numCols; ++c)
                {
                    char const* buf =
                        reinterpret_cast<char const*>(sqlite3_column_text(stmt_, c));
                    bool isNull = false;
                    if (0 == buf)
                    {
                        isNull = true;
                        buf = "";
                    }
                    dataCache_[i][c].data_ = buf;
                    dataCache_[i][c].isNull_ = isNull;
                }
            }
            else
            {
                clean_up();
                char const* zErrMsg = sqlite3_errmsg(session_.conn_);
                std::ostringstream ss;
                ss << "sqlite3_statement_backend::loadRS: "
                   << zErrMsg;
                throw soci_error(ss.str());
            }
        }
    }
    // if we read less than requested then shrink the vector
    dataCache_.resize(i);

    return retVal;
}

// This is used for non-bulk operations
statement_backend::exec_fetch_result
sqlite3_statement_backend::load_one()
{
    statement_backend::exec_fetch_result retVal = ef_success;

    int const res = sqlite3_step(stmt_);

    if (SQLITE_DONE == res)
    {
        databaseReady_ = false;
        retVal = ef_no_data;
    }
    else if (SQLITE_ROW == res)
    {
    }
    else
    {
        clean_up();

        char const* zErrMsg = sqlite3_errmsg(session_.conn_);

        std::ostringstream ss;
        ss << "sqlite3_statement_backend::loadOne: "
           << zErrMsg;
        throw soci_error(ss.str());
    }

    return retVal;
}

// Execute statements once for every row of useData
statement_backend::exec_fetch_result
sqlite3_statement_backend::bind_and_execute(int number)
{
    statement_backend::exec_fetch_result retVal = ef_no_data;

    long long rowsAffectedBulkTemp = 0;

    int const rows = static_cast<int>(useData_.size());
    for (int row = 0; row < rows; ++row)
    {
        sqlite3_reset(stmt_);

        int const totalPositions = static_cast<int>(useData_[0].size());
        for (int pos = 1; pos <= totalPositions; ++pos)
        {
            int bindRes = SQLITE_OK;
            const sqlite3_column& curCol = useData_[row][pos-1];
            if (curCol.isNull_)
            {
                bindRes = sqlite3_bind_null(stmt_, pos);
            }
            else if (curCol.blobBuf_)
            {
                bindRes = sqlite3_bind_blob(stmt_, pos,
                                            curCol.blobBuf_,
                                            static_cast<int>(curCol.blobSize_),
                                            SQLITE_STATIC);
            }
            else
            {
                bindRes = sqlite3_bind_text(stmt_, pos,
                                            curCol.data_.c_str(),
                                            static_cast<int>(curCol.data_.length()),
                                            SQLITE_STATIC);
            }

            if (SQLITE_OK != bindRes)
            {
                // preserve the number of rows affected so far.
                rowsAffectedBulk_ = rowsAffectedBulkTemp;
                throw soci_error("Failure to bind on bulk operations");
            }
        }

        // Handle the case where there are both into and use elements
        // in the same query and one of the into binds to a vector object.
        if (1 == rows && number != rows)
        {
            return load_rowset(number);
        }

        retVal = load_one(); //execute each bound line
        rowsAffectedBulkTemp += get_affected_rows();
    }
    rowsAffectedBulk_ = rowsAffectedBulkTemp;
    return retVal;
}

statement_backend::exec_fetch_result
sqlite3_statement_backend::execute(int number)
{
    if (stmt_ == NULL)
    {
        throw soci_error("No sqlite statement created");
    }

    sqlite3_reset(stmt_);
    databaseReady_ = true;

    statement_backend::exec_fetch_result retVal = ef_no_data;

    if (useData_.empty() == false)
    {
           retVal = bind_and_execute(number);
    }
    else
    {
        if (1 == number)
        {
            retVal = load_one();
        }
        else
        {
            retVal = load_rowset(number);
        }
    }

    return retVal;
}

statement_backend::exec_fetch_result
sqlite3_statement_backend::fetch(int number)
{
    return load_rowset(number);
}

long long sqlite3_statement_backend::get_affected_rows()
{
    if (rowsAffectedBulk_ >= 0)
    {
        return rowsAffectedBulk_;
    }
    return sqlite3_changes(session_.conn_);
}

int sqlite3_statement_backend::get_number_of_rows()
{
    return static_cast<int>(dataCache_.size());
}

std::string sqlite3_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    return query;
}

int sqlite3_statement_backend::prepare_for_describe()
{
    return sqlite3_column_count(stmt_);
}

void sqlite3_statement_backend::describe_column(int colNum, data_type & type,
                                                std::string & columnName)
{
    columnName = sqlite3_column_name(stmt_, colNum-1);

    // This is a hack, but the sqlite3 type system does not
    // have a date or time field.  Also it does not reliably
    // id other data types.  It has a tendency to see everything
    // as text.  sqlite3_column_decltype returns the text that is
    // used in the create table statement
    bool typeFound = false;

    char const* declType = sqlite3_column_decltype(stmt_, colNum-1);

    if ( declType == NULL )
    {
        static char const* s_char = "char";
        declType = s_char;
    }

    std::string dt = declType;

    // do all comparisons in lower case
    std::transform(dt.begin(), dt.end(), dt.begin(), tolower);

    if (dt.find("time", 0) != std::string::npos)
    {
        type = dt_date;
        typeFound = true;
    }
    if (dt.find("date", 0) != std::string::npos)
    {
        type = dt_date;
        typeFound = true;
    }

    if (dt.find("int8", 0) != std::string::npos || dt.find("bigint", 0) != std::string::npos)
    {
        type = dt_long_long;
        typeFound = true;
    }
    else if (dt.find("unsigned big int", 0) != std::string::npos)
    {
        type = dt_unsigned_long_long;
        typeFound = true;
    }
    else if (dt.find("int", 0) != std::string::npos)
    {
        type = dt_integer;
        typeFound = true;
    }

    if (dt.find("float", 0) != std::string::npos || dt.find("double", 0) != std::string::npos)
    {
        type = dt_double;
        typeFound = true;
    }
    if (dt.find("text", 0) != std::string::npos)
    {
        type = dt_string;
        typeFound = true;
    }
    if (dt.find("char", 0) != std::string::npos)
    {
        type = dt_string;
        typeFound = true;
    }
    if (dt.find("boolean", 0) != std::string::npos)
    {
        type = dt_integer;
        typeFound = true;
    }

    if (typeFound)
    {
        return;
    }

    // try to get it from the weak ass type system

    // total hack - execute the statment once to get the column types
    // then clear so it can be executed again
    sqlite3_step(stmt_);

    int const sqlite3_type = sqlite3_column_type(stmt_, colNum-1);
    switch (sqlite3_type)
    {
    case SQLITE_INTEGER:
        type = dt_integer;
        break;
    case SQLITE_FLOAT:
        type = dt_double;
        break;
    case SQLITE_BLOB:
    case SQLITE_TEXT:
        type = dt_string;
        break;
    default:
        type = dt_string;
        break;
    }

    sqlite3_reset(stmt_);
}

sqlite3_standard_into_type_backend *
sqlite3_statement_backend::make_into_type_backend()
{
    return new sqlite3_standard_into_type_backend(*this);
}

sqlite3_standard_use_type_backend * sqlite3_statement_backend::make_use_type_backend()
{
    return new sqlite3_standard_use_type_backend(*this);
}

sqlite3_vector_into_type_backend *
sqlite3_statement_backend::make_vector_into_type_backend()
{
    return new sqlite3_vector_into_type_backend(*this);
}

sqlite3_vector_use_type_backend *
sqlite3_statement_backend::make_vector_use_type_backend()
{
    return new sqlite3_vector_use_type_backend(*this);
}
