//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci-odbc.h"
#include "session.h"

#include <cstdio>

using namespace soci;
using namespace soci::details;

char const * soci::odbc_option_driver_complete = "odbc.driver_complete";

odbc_session_backend::odbc_session_backend(
    connection_parameters const & parameters)
    : henv_(0), hdbc_(0), product_(prod_uninitialized)
{
    SQLRETURN rc;

    // Allocate environment handle
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    if (is_odbc_error(rc))
    {
        throw soci_error("Unable to get environment handle");
    }

    // Set the ODBC version environment attribute
    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_ENV, henv_,
                         "Setting ODBC version");
    }

    // Allocate connection handle
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                         "Allocating connection handle");
    }

    SQLCHAR outConnString[1024];
    SQLSMALLINT strLength;

    // Prompt the user for any missing information (typically UID/PWD) in the
    // connection string by default but allow overriding this using "prompt"
    // option.
    SQLHWND hwnd_for_prompt = NULL;
    unsigned completion = SQL_DRIVER_COMPLETE;
    std::string completionString;
    if (parameters.get_option(odbc_option_driver_complete, completionString))
    {
      // The value of the option is supposed to be just the integer value of
      // one of SQL_DRIVER_XXX constants but don't check for the exact value in
      // case more of them are added in the future, the ODBC driver will return
      // an error if we pass it an invalid value anyhow.
      if (std::sscanf(completionString.c_str(), "%u", &completion) != 1)
      {
        throw soci_error("Invalid non-numeric driver completion option value \"" +
                          completionString + "\".");
      }
    }

#ifdef _WIN32
    if (completion != SQL_DRIVER_NOPROMPT)
      hwnd_for_prompt = ::GetDesktopWindow();
#endif // _WIN32

    std::string const & connectString = parameters.get_connect_string();
    rc = SQLDriverConnect(hdbc_, hwnd_for_prompt,
                          (SQLCHAR *)connectString.c_str(),
                          (SQLSMALLINT)connectString.size(),
                          outConnString, 1024, &strLength,
                          static_cast<SQLUSMALLINT>(completion));

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                         "Error Connecting to database");
    }

    connection_string_.assign((const char*)outConnString, strLength);

    reset_transaction();
}

odbc_session_backend::~odbc_session_backend()
{
    clean_up();
}

void odbc_session_backend::begin()
{
    SQLRETURN rc = SQLSetConnectAttr( hdbc_, SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0 );
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                         "Begin Transaction");
    }
}

void odbc_session_backend::commit()
{
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                         "Committing");
    }
    reset_transaction();
}

void odbc_session_backend::rollback()
{
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                         "Rolling back");
    }
    reset_transaction();
}

bool odbc_session_backend::get_next_sequence_value(
    session & s, std::string const & sequence, long & value)
{
    std::string query;

    switch ( get_database_product() )
    {
        case prod_firebird:
            query = "select next value for " + sequence + " from rdb$database";
            break;

        case prod_oracle:
            query = "select " + sequence + ".nextval from dual";
            break;

        case prod_postgresql:
            query = "select nextval('" + sequence + "')";
            break;

        case prod_mssql:
        case prod_mysql:
        case prod_sqlite:
            // These RDBMS implement get_last_insert_id() instead.
            return false;

        case prod_unknown:
            // For this one we can't do anything at all.
            return false;

        case prod_uninitialized:
            // This is not supposed to happen at all but still cover this case
            // here to avoid gcc warnings about unhandled enum values in a
            // switch.
            return false;
    }

    s << query, into(value);

    return true;
}

bool odbc_session_backend::get_last_insert_id(
    session & s, std::string const & table, long & value)
{
    std::string query;

    switch ( get_database_product() )
    {
        case prod_mssql:
            query = "select ident_current('" + table + "')";
            break;

        case prod_mysql:
            query = "select last_insert_id()";
            break;

        case prod_sqlite:
            query = "select last_insert_rowid()";
            break;

        case prod_firebird:
        case prod_oracle:
        case prod_postgresql:
            // For these RDBMS get_next_sequence_value() should have been used.
            return false;


        case prod_unknown:
            // For this one we can't do anything at all.
            return false;

        case prod_uninitialized:
            // As above, this is not supposed to happen but put it here to
            // mollify gcc.
            return false;
    }

    s << query, into(value);

    return true;
}

void odbc_session_backend::reset_transaction()
{
    SQLRETURN rc = SQLSetConnectAttr( hdbc_, SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0 );
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                            "Set Auto Commit");
    }
}


void odbc_session_backend::clean_up()
{
    SQLRETURN rc = SQLDisconnect(hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                            "SQLDisconnect");
    }

    rc = SQLFreeHandle(SQL_HANDLE_DBC, hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                            "SQLFreeHandle DBC");
    }

    rc = SQLFreeHandle(SQL_HANDLE_ENV, henv_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_ENV, henv_,
                            "SQLFreeHandle ENV");
    }
}

odbc_statement_backend * odbc_session_backend::make_statement_backend()
{
    return new odbc_statement_backend(*this);
}

odbc_rowid_backend * odbc_session_backend::make_rowid_backend()
{
    return new odbc_rowid_backend(*this);
}

odbc_blob_backend * odbc_session_backend::make_blob_backend()
{
    return new odbc_blob_backend(*this);
}

odbc_session_backend::database_product
odbc_session_backend::get_database_product()
{
    // Cache the product type, it's not going to change during our life time.
    if (product_ != prod_uninitialized)
        return product_;

    char product_name[1024];
    SQLSMALLINT len = sizeof(product_name);
    SQLRETURN rc = SQLGetInfo(hdbc_, SQL_DBMS_NAME, product_name, len, &len);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, henv_,
                            "SQLGetInfo(SQL_DBMS_NAME)");
    }

    if (strcmp(product_name, "Firebird") == 0)
        product_ = prod_firebird;
    else if (strcmp(product_name, "Microsoft SQL Server") == 0)
        product_ = prod_mssql;
    else if (strcmp(product_name, "MySQL") == 0)
        product_ = prod_mysql;
    else if (strcmp(product_name, "Oracle") == 0)
        product_ = prod_oracle;
    else if (strcmp(product_name, "PostgreSQL") == 0)
        product_ = prod_postgresql;
    else if (strcmp(product_name, "SQLite") == 0)
        product_ = prod_sqlite;
    else
        product_ = prod_unknown;

    return product_;
}
