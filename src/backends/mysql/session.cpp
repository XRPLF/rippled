//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci-mysql.h"
#include <connection-parameters.h>
// std
#include <cctype>
#include <cerrno>
#include <ciso646>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;
using std::string;


namespace
{ // anonymous

void skip_white(std::string::const_iterator *i,
    std::string::const_iterator const & end, bool endok)
{
    for (;;)
    {
        if (*i == end)
        {
            if (endok)
            {
                return;
            }
            else
            {
                throw soci_error("Unexpected end of connection string.");
            }
        }
        if (std::isspace(**i))
        {
            ++*i;
        }
        else
        {
            return;
        }
    }
}

std::string param_name(std::string::const_iterator *i,
    std::string::const_iterator const & end)
{
    std::string val("");
    for (;;)
    {
        if (*i == end or (not std::isalpha(**i) and **i != '_'))
        {
            break;
        }
        val += **i;
        ++*i;
    }
    return val;
}

string param_value(string::const_iterator *i,
    string::const_iterator const & end)
{
    string err = "Malformed connection string.";
    bool quot;
    if (**i == '\'')
    {
        quot = true;
        ++*i;
    }
    else
    {
        quot = false;
    }
    string val("");
    for (;;)
    {
        if (*i == end)
        {
            if (quot)
            {
                throw soci_error(err);
            }
            else
            {
                break;
            }
        }
        if (**i == '\'')
        {
            if (quot)
            {
                ++*i;
                break;
            }
            else
            {
                throw soci_error(err);
            }
        }
        if (not quot and std::isspace(**i))
        {
            break;
        }
        if (**i == '\\')
        {
            ++*i;
            if (*i == end)
            {
                throw soci_error(err);
            }
        }
        val += **i;
        ++*i;
    }
    return val;
}

bool valid_int(const string & s)
{
    char *tail;
    const char *cstr = s.c_str();
    errno = 0;
    long n = std::strtol(cstr, &tail, 10);
    if (errno != 0 or n > INT_MAX or n < INT_MIN)
    {
        return false;
    }
    if (*tail != '\0')
    {
        return false;
    }
    return true;
}

void parse_connect_string(const string & connectString,
    string *host, bool *host_p,
    string *user, bool *user_p,
    string *password, bool *password_p,
    string *db, bool *db_p,
    string *unix_socket, bool *unix_socket_p,
    int *port, bool *port_p, string *ssl_ca, bool *ssl_ca_p,
    string *ssl_cert, bool *ssl_cert_p, string *ssl_key, bool *ssl_key_p,
    int *local_infile, bool *local_infile_p,
    string *charset, bool *charset_p)
{
    *host_p = false;
    *user_p = false;
    *password_p = false;
    *db_p = false;
    *unix_socket_p = false;
    *port_p = false;
    *ssl_ca_p = false;
    *ssl_cert_p = false;
    *ssl_key_p = false;
    *local_infile_p = false;
    *charset_p = false;
    string err = "Malformed connection string.";
    string::const_iterator i = connectString.begin(),
        end = connectString.end();
    while (i != end)
    {
        skip_white(&i, end, true);
        if (i == end)
        {
            return;
        }
        string par = param_name(&i, end);
        skip_white(&i, end, false);
        if (*i == '=')
        {
            ++i;
        }
        else
        {
            throw soci_error(err);
        }
        skip_white(&i, end, false);
        string val = param_value(&i, end);
        if (par == "port" and not *port_p)
        {
            if (not valid_int(val))
            {
                throw soci_error(err);
            }
            *port = std::atoi(val.c_str());
            if (port < 0)
            {
                throw soci_error(err);
            }
            *port_p = true;
        }
        else if (par == "host" and not *host_p)
        {
            *host = val;
            *host_p = true;
        }
        else if (par == "user" and not *user_p)
        {
            *user = val;
            *user_p = true;
        }
        else if ((par == "pass" or par == "password") and not *password_p)
        {
            *password = val;
            *password_p = true;
        }
        else if ((par == "db" or par == "dbname" or par == "service") and
                 not *db_p)
        {
            *db = val;
            *db_p = true;
        }
        else if (par == "unix_socket" and not *unix_socket_p)
        {
            *unix_socket = val;
            *unix_socket_p = true;
        }
        else if (par == "sslca" and not *ssl_ca_p)
        {
            *ssl_ca = val;
            *ssl_ca_p = true;
        }
        else if (par == "sslcert" and not *ssl_cert_p)
        {
            *ssl_cert = val;
            *ssl_cert_p = true;
        }
        else if (par == "sslkey" and not *ssl_key_p)
        {
            *ssl_key = val;
            *ssl_key_p = true;
        }
        else if (par == "local_infile" and not *local_infile_p)
        {
            if (not valid_int(val))
            {
                throw soci_error(err);
            }
            *local_infile = std::atoi(val.c_str());
            if (*local_infile != 0 and *local_infile != 1)
            {
                throw soci_error(err);
            }
            *local_infile_p = true;
        } else if (par == "charset" and not *charset_p)
        {
            *charset = val;
            *charset_p = true;
        }
        else
        {
            throw soci_error(err);
        }
    }
}

} // namespace anonymous

mysql_session_backend::mysql_session_backend(
    connection_parameters const & parameters)
{
    string host, user, password, db, unix_socket, ssl_ca, ssl_cert, ssl_key,
        charset;
    int port, local_infile;
    bool host_p, user_p, password_p, db_p, unix_socket_p, port_p,
        ssl_ca_p, ssl_cert_p, ssl_key_p, local_infile_p, charset_p;
    parse_connect_string(parameters.get_connect_string(), &host, &host_p, &user, &user_p,
        &password, &password_p, &db, &db_p,
        &unix_socket, &unix_socket_p, &port, &port_p,
        &ssl_ca, &ssl_ca_p, &ssl_cert, &ssl_cert_p, &ssl_key, &ssl_key_p,
        &local_infile, &local_infile_p, &charset, &charset_p);
    conn_ = mysql_init(NULL);
    if (conn_ == NULL)
    {
        throw soci_error("mysql_init() failed.");
    }
    if (charset_p)
    {
        if (0 != mysql_options(conn_, MYSQL_SET_CHARSET_NAME, charset.c_str()))
        {
            clean_up();
            throw soci_error("mysql_options(MYSQL_SET_CHARSET_NAME) failed.");
        }
    }
    if (ssl_ca_p)
    {
        mysql_ssl_set(conn_, ssl_key_p ? ssl_key.c_str() : NULL,
                      ssl_cert_p ? ssl_cert.c_str() : NULL,
                      ssl_ca_p ? ssl_ca.c_str() : NULL, 0, 0);
    }
    if (local_infile_p and local_infile == 1)
    {
        if (0 != mysql_options(conn_, MYSQL_OPT_LOCAL_INFILE, NULL))
        {
            clean_up();
            throw soci_error(
                "mysql_options() failed when trying to set local-infile.");
        }
    }
    if (mysql_real_connect(conn_,
            host_p ? host.c_str() : NULL,
            user_p ? user.c_str() : NULL,
            password_p ? password.c_str() : NULL,
            db_p ? db.c_str() : NULL,
            port_p ? port : 0,
            unix_socket_p ? unix_socket.c_str() : NULL,
            CLIENT_FOUND_ROWS | CLIENT_MULTI_RESULTS) == NULL)
    {
        string errMsg = mysql_error(conn_);
        unsigned int errNum = mysql_errno(conn_);
        clean_up();
        throw mysql_soci_error(errMsg, errNum);
    }
}

mysql_session_backend::~mysql_session_backend()
{
    clean_up();
}

namespace // unnamed
{

// helper function for hardcoded queries
void hard_exec(MYSQL *conn, const string & query)
{
    if (0 != mysql_real_query(conn, query.c_str(),
            static_cast<unsigned long>(query.size())))
    {
        throw soci_error(mysql_error(conn));
    }
}

} // namespace unnamed

void mysql_session_backend::begin()
{
    hard_exec(conn_, "BEGIN");
}

void mysql_session_backend::commit()
{
    hard_exec(conn_, "COMMIT");
}

void mysql_session_backend::rollback()
{
    hard_exec(conn_, "ROLLBACK");
}

void mysql_session_backend::clean_up()
{
    if (conn_ != NULL)
    {
        mysql_close(conn_);
        conn_ = NULL;
    }
}

mysql_statement_backend * mysql_session_backend::make_statement_backend()
{
    return new mysql_statement_backend(*this);
}

mysql_rowid_backend * mysql_session_backend::make_rowid_backend()
{
    return new mysql_rowid_backend(*this);
}

mysql_blob_backend * mysql_session_backend::make_blob_backend()
{
    return new mysql_blob_backend(*this);
}
