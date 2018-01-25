//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci/soci-platform.h"
#include "soci/postgresql/soci-postgresql.h"
#include "soci/session.h"
#include "soci/connection-parameters.h"
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

using namespace soci;
using namespace soci::details;

namespace // unnamed
{

// helper function for hardcoded queries
void hard_exec(postgresql_session_backend & session_backend,
    PGconn * conn, char const * query, char const * errMsg)
{
    postgresql_result(session_backend, PQexec(conn, query)).check_for_errors(errMsg);
}

} // namespace unnamed

postgresql_session_backend::postgresql_session_backend(
    connection_parameters const& parameters, bool single_row_mode)
    : statementCount_(0)
{
    single_row_mode_ = single_row_mode;

    connect(parameters);
}

void postgresql_session_backend::connect(
    connection_parameters const& parameters)
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

    // Increase the number of digits used for floating point values to ensure
    // that the conversions to/from text round trip correctly, which is not the
    // case with the default value of 0. Use the maximal supported value, which
    // was 2 until 9.x and is 3 since it.
    int const version = PQserverVersion(conn);
    hard_exec(*this, conn,
        version >= 90000 ? "SET extra_float_digits = 3"
                         : "SET extra_float_digits = 2",
        "Cannot set extra_float_digits parameter");

    conn_ = conn;
}

postgresql_session_backend::~postgresql_session_backend()
{
    clean_up();
}

void postgresql_session_backend::begin()
{
    hard_exec(*this, conn_, "BEGIN", "Cannot begin transaction.");
}

void postgresql_session_backend::commit()
{
    hard_exec(*this, conn_, "COMMIT", "Cannot commit transaction.");
}

void postgresql_session_backend::rollback()
{
    hard_exec(*this, conn_, "ROLLBACK", "Cannot rollback transaction.");
}

void postgresql_session_backend::deallocate_prepared_statement(
    const std::string & statementName)
{
    const std::string & query = "DEALLOCATE " + statementName;

    hard_exec(*this, conn_, query.c_str(),
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
    return new postgresql_statement_backend(*this, single_row_mode_);
}

postgresql_rowid_backend * postgresql_session_backend::make_rowid_backend()
{
    return new postgresql_rowid_backend(*this);
}

postgresql_blob_backend * postgresql_session_backend::make_blob_backend()
{
    return new postgresql_blob_backend(*this);
}
