//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"
#include "soci/odbc/soci-odbc.h"
#include "common-tests.h"
#include <iostream>
#include <string>
#include <cstdio>
#include <ctime>
#include <cmath>

using namespace soci;
using namespace soci::tests;

// A generic version class: we might want to factor it out later if it is
// needed elsewhere (it would probably also need to be renamed to something
// less generic then).
class odbc_version
{
public:
    odbc_version()
    {
        initialized_ = false;
    }

    odbc_version(unsigned major, unsigned minor, unsigned release)
        : major_(major), minor_(minor), release_(release)
    {
        initialized_ = true;
    }

    bool init_from_string(char const* s)
    {
        initialized_ = std::sscanf(s, "%u.%u.%u",
                                   &major_, &minor_, &release_) == 3;
        return initialized_;
    }

    bool is_initialized() const { return initialized_; }

    std::string as_string() const
    {
        if (initialized_)
        {
            char buf[128];
            // This uses the ODBC convention of padding the minor and release
            // versions with 0 and might be not appropriate in general.
            std::sprintf(buf, "%u.%02u.%04u", major_, minor_, release_);
            return buf;
        }
        else
        {
            return "(uninitialized)";
        }
    }

    // Compare versions using the lexicographical sort order, with
    // uninitialized version considered less than any initialized one.
    bool operator<(odbc_version const& v) const
    {
        if (!initialized_)
            return v.initialized_;

        return major_ < v.major_ ||
                (major_ == v.major_ && (minor_ < v.minor_ ||
                    (minor_ == v.minor_ && release_ < v.release_)));
    }

private:
    unsigned major_, minor_, release_;
    bool initialized_;
};

std::ostream& operator<<(std::ostream& os, odbc_version const& v)
{
    os << v.as_string();
    return os;
}

std::string connectString;
backend_factory const &backEnd = *soci::factory_odbc();

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, val integer, c char, "
                 "str varchar(20), sh int2, ul numeric(20), d float8, "
                 "num76 numeric(7,6), "
                 "tm timestamp, i1 integer, i2 integer, i3 integer, "
                 "name varchar(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(soci::session & sql)
        : table_creator_base(sql)
    {
        sql  << "create table soci_test(num_float float8, num_int integer,"
                     " name varchar(20), sometime timestamp, chr char)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(name varchar(100) not null, "
            "phone varchar(15))";
    }
};

struct table_creator_for_get_affected_rows : table_creator_base
{
    table_creator_for_get_affected_rows(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

struct table_creator_for_xml : table_creator_base
{
    table_creator_for_xml(soci::session& sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, x xml)";
    }
};

struct table_creator_for_clob : table_creator_base
{
    table_creator_for_clob(soci::session& sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, s text)";
    }
};

//
// Support for SOCI Common Tests
//

class test_context : public test_context_base
{
public:
    test_context(backend_factory const &backEnd,
                std::string const &connectString)
        : test_context_base(backEnd, connectString),
          m_verDriver(get_driver_version())
    {
        std::cout << "Using ODBC driver version " << m_verDriver << "\n";
    }

    table_creator_base * table_creator_1(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_one(s);
    }

    table_creator_base * table_creator_2(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_two(s);
    }

    table_creator_base * table_creator_3(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_three(s);
    }

    table_creator_base * table_creator_4(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_for_get_affected_rows(s);
    }

    table_creator_base* table_creator_xml(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_for_xml(s);
    }

    table_creator_base* table_creator_clob(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_for_clob(s);
    }

    bool has_real_xml_support() const SOCI_OVERRIDE
    {
        return true;
    }

    std::string to_date_time(std::string const &datdt_string) const SOCI_OVERRIDE
    {
        return "timestamptz(\'" + datdt_string + "\')";
    }

    bool has_fp_bug() const SOCI_OVERRIDE
    {
        // The bug with using insufficiently many digits for double values was
        // only fixed in 9.03.0400 version of the ODBC driver (see commit
        // a5fed2338b59ae16a2d3a8d2744b084949684775 in its repository), so we
        // need to check for its version here.
        //
        // Be pessimistic if we failed to retrieve the version at all.
        return !m_verDriver.is_initialized() || m_verDriver < odbc_version(9, 3, 400);
    }

    std::string sql_length(std::string const& s) const SOCI_OVERRIDE
    {
        return "char_length(" + s + ")";
    }

private:
    odbc_version get_driver_version() const
    {
        try
        {
            soci::session sql(get_backend_factory(), get_connect_string());
            odbc_session_backend* const
                odbc_session = static_cast<odbc_session_backend*>(sql.get_backend());
            if (!odbc_session)
            {
                std::cerr << "Failed to get odbc_session_backend?\n";
                return odbc_version();
            }

            char driver_ver[1024];
            SQLSMALLINT len = sizeof(driver_ver);
            SQLRETURN rc = SQLGetInfo(odbc_session->hdbc_, SQL_DRIVER_VER,
                                      driver_ver, len, &len);
            if (soci::is_odbc_error(rc))
            {
                std::cerr << "Retrieving ODBC driver version failed: "
                          << rc << "\n";
                return odbc_version();
            }

            odbc_version v;
            if (!v.init_from_string(driver_ver))
            {
                std::cerr << "Unknown ODBC driver version format: \""
                          << driver_ver << "\"\n";
            }

            return v;
        }
        catch ( ... )
        {
            // Failure getting the version is not fatal.
            return odbc_version();
        }
    }

    odbc_version const m_verDriver;
};

int main(int argc, char** argv)
{

#ifdef _MSC_VER
    // Redirect errors, unrecoverable problems, and assert() failures to STDERR,
    // instead of debug message window.
    // This hack is required to run assert()-driven tests by Buildbot.
    // NOTE: Comment this 2 lines for debugging with Visual C++ debugger to catch assertions inside.
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif //_MSC_VER

    if (argc >= 2 && argv[1][0] != '-')
    {
        connectString = argv[1];

        // Replace the connect string with the process name to ensure that
        // CATCH uses the correct name in its messages.
        argv[1] = argv[0];

        argc--;
        argv++;
    }
    else
    {
        connectString = "FILEDSN=./test-postgresql.dsn";
    }

    test_context tc(backEnd, connectString);

    return Catch::Session().run(argc, argv);
}
