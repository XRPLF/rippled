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
#include <ctime>
#include <cmath>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_odbc();

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, val integer, c char, "
                 "str varchar(20), sh integer, ul number, d float, "
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
        sql  << "create table soci_test(num_float float, num_int integer,"
                     " name varchar(20), sometime datetime, chr char)";
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

//
// Support for SOCI Common Tests
//

class test_context : public test_context_base
{
public:

test_context(backend_factory const &backEnd, std::string const &connectString)
        : test_context_base(backEnd, connectString) {}

    table_creator_base * table_creator_1(soci::session& s) const
    {
        return new table_creator_one(s);
    }

    table_creator_base * table_creator_2(soci::session& s) const
    {
        return new table_creator_two(s);
    }

    table_creator_base * table_creator_3(soci::session& s) const
    {
        return new table_creator_three(s);
    }

    table_creator_base * table_creator_4(soci::session& s) const
    {
        return new table_creator_for_get_affected_rows(s);
    }

    std::string fromDual(std::string const &sql) const
    {
        return sql;
    }

    std::string toDate(std::string const &datdt_string) const
    {
        return "#" + datdt_string + "#";
    }

    std::string to_date_time(std::string const &datdt_string) const
    {
        return "#" + datdt_string + "#";
    }

    virtual std::string sql_length(std::string const& s) const
    {
        return "len(" + s + ")";
    }
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
        connectString = "FILEDSN=./test-access.dsn";
    }

    test_context tc(backEnd, connectString);

    return Catch::Session().run(argc, argv);
}
