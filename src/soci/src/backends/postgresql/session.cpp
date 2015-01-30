//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci-postgresql.h"
#include "session.h"
#include <connection-parameters.h>
#include <libpq/libpq-fs.h> // libpq
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef SOCI_POSTGRESQL_NOPARAMS
#ifndef SOCI_POSTGRESQL_NOBINDBYNAME
#define SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOPARAMS

#ifdef _MSC_VER
#pragma warning(disable:4355 4996)
#endif

using namespace soci;
using namespace soci::details;

postgresql_session_backend::postgresql_session_backend(
    connection_parameters const& parameters)
    : statementCount_(0)
{
    PGconn* conn = PQconnectdb(parameters.get_connect_string().c_str());
    if (0 == conn || CONNECTION_OK != PQstatus(conn))
    {
        std::string msg = "Cannot establish connection to the database.";
        if (0 != conn)
        {
            msg += '\n';
            msg += PQerrorMessage(conn);
            PQfinish(conn);
        }

        throw soci_error(msg);
    }

    conn_ = conn;
}

postgresql_session_backend::~postgresql_session_backend()
{
    clean_up();
}

namespace // unnamed
{

// helper function for hardcoded queries
void hard_exec(PGconn * conn, char const * query, char const * errMsg)
{
    postgresql_result(PQexec(conn, query)).check_for_errors(errMsg);
}

} // namespace unnamed

void postgresql_session_backend::begin()
{
    hard_exec(conn_, "BEGIN", "Cannot begin transaction.");
}

void postgresql_session_backend::commit()
{
    hard_exec(conn_, "COMMIT", "Cannot commit transaction.");
}

void postgresql_session_backend::rollback()
{
    hard_exec(conn_, "ROLLBACK", "Cannot rollback transaction.");
}

void postgresql_session_backend::deallocate_prepared_statement(
    const std::string & statementName)
{
    const std::string & query = "DEALLOCATE " + statementName;

    hard_exec(conn_, query.c_str(),
        "Cannot deallocate prepared statement.");
}

bool postgresql_session_backend::get_next_sequence_value(
    session & s, std::string const & sequence, long & value)
{
    s << "select nextval('" + sequence + "')", into(value);

    return true;
}

void postgresql_session_backend::clean_up()
{
    if (0 != conn_)
    {
        PQfinish(conn_);
        conn_ = 0;
    }
}

std::string postgresql_session_backend::get_next_statement_name()
{
    char nameBuf[20] = { 0 }; // arbitrary length
    sprintf(nameBuf, "st_%d", ++statementCount_);
    return nameBuf;
}

postgresql_statement_backend * postgresql_session_backend::make_statement_backend()
{
    return new postgresql_statement_backend(*this);
}

postgresql_rowid_backend * postgresql_session_backend::make_rowid_backend()
{
    return new postgresql_rowid_backend(*this);
}

postgresql_blob_backend * postgresql_session_backend::make_blob_backend()
{
    return new postgresql_blob_backend(*this);
}
