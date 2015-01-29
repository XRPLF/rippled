//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//


#include "soci-sqlite3.h"

#include <connection-parameters.h>

#include <sstream>
#include <string>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using namespace sqlite_api;

namespace // anonymous
{

// helper function for hardcoded queries
void execude_hardcoded(sqlite_api::sqlite3* conn, char const* const query, char const* const errMsg)
{
    char *zErrMsg = 0;
    int const res = sqlite3_exec(conn, query, 0, 0, &zErrMsg);
    if (res != SQLITE_OK)
    {
        std::ostringstream ss;
        ss << errMsg << " " << zErrMsg;
        sqlite3_free(zErrMsg);
        throw soci_error(ss.str());
    }
}

void check_sqlite_err(sqlite_api::sqlite3* conn, int res, char const* const errMsg)
{
    if (SQLITE_OK != res)
    {
        const char *zErrMsg = sqlite3_errmsg(conn);
        std::ostringstream ss;
        ss << errMsg << zErrMsg;
        throw soci_error(ss.str());
    }
}

} // namespace anonymous


sqlite3_session_backend::sqlite3_session_backend(
    connection_parameters const & parameters)
{
    int timeout = 0;
    int connection_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    std::string synchronous;
    std::string const & connectString = parameters.get_connect_string();
    std::string dbname(connectString);
    std::stringstream ssconn(connectString);
    while (!ssconn.eof() && ssconn.str().find('=') != std::string::npos)
    {
        std::string key, val;
        std::getline(ssconn, key, '=');
        std::getline(ssconn, val, ' ');

        if (val.size()>0 && val[0]=='\"')
        {
            std::string quotedVal = val.erase(0, 1);

            if (quotedVal[quotedVal.size()-1] ==  '\"')
            {
                quotedVal.erase(val.size()-1);
            }
            else // space inside value string
            {
                std::getline(ssconn, val, '\"');
                quotedVal = quotedVal + " " + val;
                std::string keepspace;
                std::getline(ssconn, keepspace, ' ');
            }     

            val = quotedVal;
        }

        if ("dbname" == key || "db" == key)
        {
            dbname = val;
        }
        else if ("timeout" == key)
        {
            std::istringstream converter(val);
            converter >> timeout;
        }
        else if ("synchronous" == key)
        {
            synchronous = val;
        }
        else if ("shared_cache" == key && "true" == val)
        {
            connection_flags |=  SQLITE_OPEN_SHAREDCACHE;
        }
    }

    int res = sqlite3_open_v2(dbname.c_str(), &conn_, connection_flags, NULL);
    check_sqlite_err(conn_, res, "Cannot establish connection to the database. ");

    if (!synchronous.empty())
    {
        std::string const query("pragma synchronous=" + synchronous);
        std::string const errMsg("Query failed: " + query);
        execude_hardcoded(conn_, query.c_str(), errMsg.c_str());
    }

    res = sqlite3_busy_timeout(conn_, timeout * 1000);
    check_sqlite_err(conn_, res, "Failed to set busy timeout for connection. ");

}

sqlite3_session_backend::~sqlite3_session_backend()
{
    clean_up();
}

void sqlite3_session_backend::begin()
{
    execude_hardcoded(conn_, "BEGIN", "Cannot begin transaction.");
}

void sqlite3_session_backend::commit()
{
    execude_hardcoded(conn_, "COMMIT", "Cannot commit transaction.");
}

void sqlite3_session_backend::rollback()
{
    execude_hardcoded(conn_, "ROLLBACK", "Cannot rollback transaction.");
}

void sqlite3_session_backend::clean_up()
{
    sqlite3_close(conn_);
}

sqlite3_statement_backend * sqlite3_session_backend::make_statement_backend()
{
    return new sqlite3_statement_backend(*this);
}

sqlite3_rowid_backend * sqlite3_session_backend::make_rowid_backend()
{
    return new sqlite3_rowid_backend(*this);
}

sqlite3_blob_backend * sqlite3_session_backend::make_blob_backend()
{
    return new sqlite3_blob_backend(*this);
}
