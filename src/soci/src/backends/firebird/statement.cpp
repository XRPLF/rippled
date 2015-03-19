//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"
#include "error-firebird.h"
#include <cctype>
#include <sstream>
#include <iostream>

using namespace soci;
using namespace soci::details;
using namespace soci::details::firebird;

firebird_statement_backend::firebird_statement_backend(firebird_session_backend &session)
    : session_(session), stmtp_(0), sqldap_(NULL), sqlda2p_(NULL),
        boundByName_(false), boundByPos_(false), rowsFetched_(0), endOfRowSet_(false), rowsAffectedBulk_(-1LL), 
            intoType_(eStandard), useType_(eStandard), procedure_(false)
{}

void firebird_statement_backend::prepareSQLDA(XSQLDA ** sqldap, int size)
{
    if (*sqldap != NULL)
    {
        *sqldap = reinterpret_cast<XSQLDA*>(realloc(*sqldap, XSQLDA_LENGTH(size)));
    }
    else
    {
        *sqldap = reinterpret_cast<XSQLDA*>(malloc(XSQLDA_LENGTH(size)));
    }

    (*sqldap)->sqln = size;
    (*sqldap)->version = 1;
}

void firebird_statement_backend::alloc()
{
    ISC_STATUS stat[stat_size];

    if (isc_dsql_allocate_statement(stat, &session_.dbhp_, &stmtp_))
    {
        throw_iscerror(stat);
    }
}

void firebird_statement_backend::clean_up()
{
    rowsAffectedBulk_ = -1LL;

    ISC_STATUS stat[stat_size];

    if (stmtp_ != NULL)
    {
        if (isc_dsql_free_statement(stat, &stmtp_, DSQL_drop))
        {
            throw_iscerror(stat);
        }
        stmtp_ = NULL;
    }

    if (sqldap_ != NULL)
    {
        free(sqldap_);
        sqldap_ = NULL;
    }

    if (sqlda2p_ != NULL)
    {
        free(sqlda2p_);
        sqlda2p_ = NULL;
    }
}

void firebird_statement_backend::rewriteParameters(
    std::string const & src, std::vector<char> & dst)
{
    std::vector<char>::iterator dst_it = dst.begin();

    // rewrite the query by transforming all named parameters into
    // the Firebird question marks (:abc -> ?, etc.)

    enum { eNormal, eInQuotes, eInName } state = eNormal;

    std::string name;
    int position = 0;

    for (std::string::const_iterator it = src.begin(), end = src.end();
        it != end; ++it)
    {
        switch (state)
        {
        case eNormal:
            if (*it == '\'')
            {
                *dst_it++ = *it;
                state = eInQuotes;
            }
            else if (*it == ':')
            {
                state = eInName;
            }
            else // regular character, stay in the same state
            {
                *dst_it++ = *it;
            }
            break;
        case eInQuotes:
            if (*it == '\'')
            {
                *dst_it++ = *it;
                state = eNormal;
            }
            else // regular quoted character
            {
                *dst_it++ = *it;
            }
            break;
        case eInName:
            if (std::isalnum(*it) || *it == '_')
            {
                name += *it;
            }
            else // end of name
            {
                names_.insert(std::pair<std::string, int>(name, position++));
                name.clear();
                *dst_it++ = '?';
                *dst_it++ = *it;
                state = eNormal;
            }
            break;
        }
    }

    if (state == eInName)
    {
        names_.insert(std::pair<std::string, int>(name, position++));
        *dst_it++ = '?';
    }

    *dst_it = '\0';
}

namespace
{
    int statementType(isc_stmt_handle stmt)
    {
        int stype;
        int length;
        char type_item[] = {isc_info_sql_stmt_type};
        char res_buffer[8];

        ISC_STATUS stat[stat_size];

        if (isc_dsql_sql_info(stat, &stmt, sizeof(type_item),
            type_item, sizeof(res_buffer), res_buffer))
        {
            throw_iscerror(stat);
        }

        if (res_buffer[0] == isc_info_sql_stmt_type)
        {
            length = isc_vax_integer(res_buffer+1, 2);
            stype = isc_vax_integer(res_buffer+3, length);
        }
        else
        {
            throw soci_error("Can't determine statement type.");
        }

        return stype;
    }
}

void firebird_statement_backend::rewriteQuery(
    std::string const &query, std::vector<char> &buffer)
{
    // buffer for temporary query
    std::vector<char> tmpQuery;
    std::vector<char>::iterator qItr;

    // buffer for query with named parameters changed to standard ones
    std::vector<char> rewQuery(query.size() + 1);

    // take care of named parameters in original query
    rewriteParameters(query, rewQuery);

    std::string const prefix("execute procedure ");
    std::string const prefix2("select * from ");

    // for procedures, we are preparing statement to determine
    // type of procedure.
    if (procedure_)
    {
        tmpQuery.resize(prefix.size() + rewQuery.size());
        qItr = tmpQuery.begin();
        std::copy(prefix.begin(), prefix.end(), qItr);
        qItr += prefix.size();
    }
    else
    {
        tmpQuery.resize(rewQuery.size());
        qItr = tmpQuery.begin();
    }

    // prepare temporary query
    std::copy(rewQuery.begin(), rewQuery.end(), qItr);

    // preparing buffers for output parameters
    if (sqldap_ == NULL)
    {
        prepareSQLDA(&sqldap_);
    }

    ISC_STATUS stat[stat_size];
    isc_stmt_handle tmpStmtp = 0;

    // allocate temporary statement to determine its type
    if (isc_dsql_allocate_statement(stat, &session_.dbhp_, &tmpStmtp))
    {
        throw_iscerror(stat);
    }

    // prepare temporary statement
    if (isc_dsql_prepare(stat, &(session_.trhp_), &tmpStmtp, 0,
        &tmpQuery[0], SQL_DIALECT_V6, sqldap_))
    {
        throw_iscerror(stat);
    }

    // get statement type
    int stType = statementType(tmpStmtp);

    // free temporary prepared statement
    if (isc_dsql_free_statement(stat, &tmpStmtp, DSQL_drop))
    {
        throw_iscerror(stat);
    }

    // take care of special cases
    if (procedure_)
    {
        // for procedures that return values, we need to use correct syntax
        if (sqldap_->sqld != 0)
        {
            // this is "select" procedure, so we have to change syntax
            buffer.resize(prefix2.size() + rewQuery.size());
            qItr = buffer.begin();
            std::copy(prefix2.begin(), prefix2.end(), qItr);
            qItr += prefix2.size();
            std::copy(rewQuery.begin(), rewQuery.end(), qItr);

            // that won't be needed anymore
            procedure_ = false;

            return;
        }
    }
    else
    {
        // this is not procedure, so syntax is ok except for named
        // parameters in ddl
        if (stType == isc_info_sql_stmt_ddl)
        {
            // this statement is a DDL - we can't rewrite named parameters
            // so, we will use original query
            buffer.resize(query.size() + 1);
            std::copy(query.begin(), query.end(), buffer.begin());

            // that won't be needed anymore
            procedure_ = false;

            return;
        }
    }

    // here we know, that temporary query is OK, so we leave it as is
    buffer.resize(tmpQuery.size());
    std::copy(tmpQuery.begin(), tmpQuery.end(), buffer.begin());

    // that won't be needed anymore
    procedure_ = false;
}

void firebird_statement_backend::prepare(std::string const & query,
                                         statement_type /* eType */)
{
    //std::cerr << "prepare: query=" << query << std::endl;
    // clear named parametes
    names_.clear();

    std::vector<char> queryBuffer;

    // modify query's syntax and prepare buffer for use with
    // firebird's api
    rewriteQuery(query, queryBuffer);

    ISC_STATUS stat[stat_size];

    // prepare real statement
    if (isc_dsql_prepare(stat, &(session_.trhp_), &stmtp_, 0,
        &queryBuffer[0], SQL_DIALECT_V6, sqldap_))
    {
        throw_iscerror(stat);
    }

    if (sqldap_->sqln < sqldap_->sqld)
    {
        // sqlda is too small for all columns. it must be reallocated
        prepareSQLDA(&sqldap_, sqldap_->sqld);

        if (isc_dsql_describe(stat, &stmtp_, SQL_DIALECT_V6, sqldap_))
        {
            throw_iscerror(stat);
        }
    }

    // preparing input parameters
    if (sqlda2p_ == NULL)
    {
        prepareSQLDA(&sqlda2p_);
    }

    if (isc_dsql_describe_bind(stat, &stmtp_, SQL_DIALECT_V6, sqlda2p_))
    {
        throw_iscerror(stat);
    }

    if (sqlda2p_->sqln < sqlda2p_->sqld)
    {
        // sqlda is too small for all columns. it must be reallocated
        prepareSQLDA(&sqlda2p_, sqlda2p_->sqld);

        if (isc_dsql_describe_bind(stat, &stmtp_, SQL_DIALECT_V6, sqlda2p_))
        {
            throw_iscerror(stat);
        }
    }

    // prepare buffers for indicators
    inds_.clear();
    inds_.resize(sqldap_->sqld);

    // reset types of into buffers
    intoType_ = eStandard;
    intos_.resize(0);

    // reset types of use buffers
    useType_ = eStandard;
    uses_.resize(0);
}


namespace
{
    void checkSize(std::size_t actual, std::size_t expected,
        std::string const & name)
    {
        if (actual != expected)
        {
            std::ostringstream msg;
            msg << "Incorrect number of " << name << " variables. "
                << "Expected " << expected << ", got " << actual;
            throw soci_error(msg.str());
        }
    }
}

statement_backend::exec_fetch_result
firebird_statement_backend::execute(int number)
{
    ISC_STATUS stat[stat_size];
    XSQLDA *t = NULL;

    std::size_t usize = uses_.size();

    // do we have enough into variables ?
    checkSize(intos_.size(), sqldap_->sqld, "into");
    // do we have enough use variables ?
    checkSize(usize, sqlda2p_->sqld, "use");

    // do we have parameters ?
    if (sqlda2p_->sqld)
    {
        t = sqlda2p_;

        if (useType_ == eStandard)
        {
            for (std::size_t col=0; col<usize; ++col)
            {
                static_cast<firebird_standard_use_type_backend*>(uses_[col])->exchangeData();
            }
        }
    }

    // make sure there is no active cursor
    if (isc_dsql_free_statement(stat, &stmtp_, DSQL_close))
    {
        // ignore attempt to close already closed cursor
        if (check_iscerror(stat, isc_dsql_cursor_close_err) == false)
        {
            throw_iscerror(stat);
        }
    }

    if (useType_ == eVector)
    {
        long long rowsAffectedBulkTemp = 0;

        // Here we have to explicitly loop to achieve the
        // effect of inserting or updating with vector use elements.
        std::size_t rows = static_cast<firebird_vector_use_type_backend*>(uses_[0])->size();
        for (std::size_t row=0; row < rows; ++row)
        {
            // first we have to prepare input parameters
            for (std::size_t col=0; col<usize; ++col)
            {
                static_cast<firebird_vector_use_type_backend*>(uses_[col])->exchangeData(row);
            }

            // then execute query
            if (isc_dsql_execute(stat, &session_.trhp_, &stmtp_, SQL_DIALECT_V6, t))
            {
                // preserve the number of rows affected so far.
                rowsAffectedBulk_ = rowsAffectedBulkTemp;
                throw_iscerror(stat);
            }
            else
            {
                rowsAffectedBulkTemp += get_affected_rows();
            }
            // soci does not allow bulk insert/update and bulk select operations
            // in same query. So here, we know that into elements are not
            // vectors. So, there is no need to fetch data here.
        }
        rowsAffectedBulk_ = rowsAffectedBulkTemp;
    }
    else
    {
        // use elements aren't vectors
        if (isc_dsql_execute(stat, &session_.trhp_, &stmtp_, SQL_DIALECT_V6, t))
        {
            throw_iscerror(stat);
        }
    }

    // Successfully re-executing the statement must reset the "end of rowset"
    // flag, we might be able to fetch data again now.
    endOfRowSet_ = false;

    if (sqldap_->sqld)
    {
        // query may return some data
        if (number > 0)
        {
            // number contains size of input variables, so we may fetch() data here
            return fetch(number);
        }
        else
        {
            // execute(0) was meant to only perform the query
            return ef_success;
        }
    }
    else
    {
        // query can't return any data
        return ef_no_data;
    }
}

statement_backend::exec_fetch_result
firebird_statement_backend::fetch(int number)
{
    if (endOfRowSet_)
        return ef_no_data;

    ISC_STATUS stat[stat_size];

    for (size_t i = 0; i<static_cast<unsigned int>(sqldap_->sqld); ++i)
    {
        inds_[i].resize(number > 0 ? number : 1);
    }

    // Here we have to explicitly loop to achieve the effect of fetching
    // vector into elements. After each fetch, we have to exchange data
    // with into buffers.
    rowsFetched_ = 0;
    for (int i = 0; i < number; ++i)
    {
        long fetch_stat = isc_dsql_fetch(stat, &stmtp_, SQL_DIALECT_V6, sqldap_);

        // there is more data to read
        if (fetch_stat == 0)
        {
            ++rowsFetched_;
            exchangeData(true, i);
        }
        else if (fetch_stat == 100L)
        {
            endOfRowSet_ = true;
            return ef_no_data;
        }
        else
        {
            // error
            endOfRowSet_ = true;
            throw_iscerror(stat);
            return ef_no_data; // unreachable, for compiler only
        }
    } // for

    return ef_success;
}

// here we put data fetched from database into user buffers
void firebird_statement_backend::exchangeData(bool gotData, int row)
{
    if (gotData)
    {
        for (size_t i = 0; i < static_cast<unsigned int>(sqldap_->sqld); ++i)
        {
            // first save indicators
            if (((sqldap_->sqlvar+i)->sqltype & 1) == 0)
            {
                // there is no indicator for this column
                inds_[i][row] = i_ok;
            }
            else if (*((sqldap_->sqlvar+i)->sqlind) == 0)
            {
                inds_[i][row] = i_ok;
            }
            else if (*((sqldap_->sqlvar+i)->sqlind) == -1)
            {
                inds_[i][row] = i_null;
            }
            else
            {
                throw soci_error("Unknown state in firebird_statement_backend::exchangeData()");
            }

            // then deal with data
            if (inds_[i][row] != i_null)
            {
                if (intoType_ == eVector)
                {
                    static_cast<firebird_vector_into_type_backend*>(
                        intos_[i])->exchangeData(row);
                }
                else
                {
                    static_cast<firebird_standard_into_type_backend*>(
                        intos_[i])->exchangeData();
                }
            }
        }
    }
}

long long firebird_statement_backend::get_affected_rows()
{
    if (rowsAffectedBulk_ >= 0)
    {
        return rowsAffectedBulk_;
    }

    ISC_STATUS_ARRAY stat;
    char type_item[] = { isc_info_sql_records };
    char res_buffer[256];

    if (isc_dsql_sql_info(stat, &stmtp_, sizeof(type_item), type_item,
                          sizeof(res_buffer), res_buffer))
    {
        throw_iscerror(stat);
    }

    // We must get back a isc_info_sql_records block, that we parse below,
    // followed by isc_info_end.
    if (res_buffer[0] != isc_info_sql_records)
    {
        throw soci_error("Can't determine the number of affected rows");
    }

    char* sql_rec_buf = res_buffer + 1;
    const int length = isc_vax_integer(sql_rec_buf, 2);
    sql_rec_buf += 2;

    if (sql_rec_buf[length] != isc_info_end)
    {
        throw soci_error("Unexpected isc_info_sql_records return format");
    }

    // Examine the 4 sub-blocks each of which has a header indicating the block
    // type, its value length in bytes and the value itself.
    long long row_count = 0;

    for ( char* p = sql_rec_buf; !row_count && p < sql_rec_buf + length; )
    {
        switch (*p++)
        {
            case isc_info_req_select_count:
            case isc_info_req_insert_count:
            case isc_info_req_update_count:
            case isc_info_req_delete_count:
                {
                    int len = isc_vax_integer(p, 2);
                    p += 2;

                    row_count += isc_vax_integer(p, len);
                    p += len;
                }
                break;

            case isc_info_end:
                break;

            default:
                throw soci_error("Unknown record counter");
        }
    }

    return row_count;
}

int firebird_statement_backend::get_number_of_rows()
{
    return rowsFetched_;
}

std::string firebird_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    procedure_ = true;
    return query;
}

int firebird_statement_backend::prepare_for_describe()
{
    return static_cast<int>(sqldap_->sqld);
}

void firebird_statement_backend::describe_column(int colNum,
                                                data_type & type, std::string & columnName)
{
    XSQLVAR * var = sqldap_->sqlvar+(colNum-1);

    columnName.assign(var->aliasname, var->aliasname_length);

    switch (var->sqltype & ~1)
    {
    case SQL_TEXT:
    case SQL_VARYING:
        type = dt_string;
        break;
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TIMESTAMP:
        type = dt_date;
        break;
    case SQL_FLOAT:
    case SQL_DOUBLE:
        type = dt_double;
        break;
    case SQL_SHORT:
    case SQL_LONG:
        if (var->sqlscale < 0)
        {
            if (session_.get_option_decimals_as_strings())
                type = dt_string;
            else
                type = dt_double;
        }
        else
        {
            type = dt_integer;
        }
        break;
    case SQL_INT64:
        if (var->sqlscale < 0)
        {
            if (session_.get_option_decimals_as_strings())
                type = dt_string;
            else
                type = dt_double;
        }
        else
        {
            type = dt_long_long;
        }
        break;
        /* case SQL_BLOB:
        case SQL_ARRAY:*/
    default:
        std::ostringstream msg;
        msg << "Type of column ["<< colNum << "] \"" << columnName
            << "\" is not supported for dynamic queries";
        throw soci_error(msg.str());
        break;
    }
}

firebird_standard_into_type_backend * firebird_statement_backend::make_into_type_backend()
{
    return new firebird_standard_into_type_backend(*this);
}

firebird_standard_use_type_backend * firebird_statement_backend::make_use_type_backend()
{
    return new firebird_standard_use_type_backend(*this);
}

firebird_vector_into_type_backend * firebird_statement_backend::make_vector_into_type_backend()
{
    return new firebird_vector_into_type_backend(*this);
}

firebird_vector_use_type_backend * firebird_statement_backend::make_vector_use_type_backend()
{
    return new firebird_vector_use_type_backend(*this);
}
