//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-sqlite3.h"
#include "common-tests.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_sqlite3();

// ROWID test
// In sqlite3 the row id can be called ROWID, _ROWID_ or oid
void test1()
{
    {
        session sql(backEnd, connectString);

        try { sql << "drop table test1"; }
        catch (soci_error const &) {} // ignore if error

        sql <<
        "create table test1 ("
        "    id integer,"
        "    name varchar(100)"
        ")";

        sql << "insert into test1(id, name) values(7, \'John\')";

        rowid rid(sql);
        sql << "select oid from test1 where id = 7", into(rid);

        int id;
        std::string name;

        sql << "select id, name from test1 where oid = :rid",
        into(id), into(name), use(rid);

        assert(id == 7);
        assert(name == "John");

        sql << "drop table test1";
    }

    std::cout << "test 1 passed" << std::endl;
}

// BLOB test
struct blob_table_creator : public table_creator_base
{
    blob_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql <<
            "create table soci_test ("
            "    id integer,"
            "    img blob"
            ")";
    }
};

void test2()
{
    {
        session sql(backEnd, connectString);

        blob_table_creator tableCreator(sql);

        char buf[] = "abcdefghijklmnopqrstuvwxyz";

        sql << "insert into soci_test(id, img) values(7, '')";

        {
            blob b(sql);

            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == 0);

            b.write(0, buf, sizeof(buf));
            assert(b.get_len() == sizeof(buf));
            sql << "update soci_test set img=? where id = 7", use(b);

            b.append(buf, sizeof(buf));
            assert(b.get_len() == 2 * sizeof(buf));
            sql << "insert into soci_test(id, img) values(8, ?)", use(b);
        }
        {
            blob b(sql);
            sql << "select img from soci_test where id = 8", into(b);
            assert(b.get_len() == 2 * sizeof(buf));
            char buf2[100];
            b.read(0, buf2, 10);
            assert(std::strncmp(buf2, "abcdefghij", 10) == 0);

            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == sizeof(buf));

        }
    }

    std::cout << "test 2 passed" << std::endl;
}

// This test was put in to fix a problem that occurs when there are both
// into and use elements in the same query and one of them (into) binds
// to a vector object.

struct test3_table_creator : table_creator_base
{
    test3_table_creator(session & sql) : table_creator_base(sql)
    {
        sql << "create table soci_test( id integer, name varchar, subname varchar);";
    }
};

void test3()
{
    {
        session sql(backEnd, connectString);

        test3_table_creator tableCreator(sql);

        sql << "insert into soci_test(id,name,subname) values( 1,'john','smith')";
        sql << "insert into soci_test(id,name,subname) values( 2,'george','vals')";
        sql << "insert into soci_test(id,name,subname) values( 3,'ann','smith')";
        sql << "insert into soci_test(id,name,subname) values( 4,'john','grey')";
        sql << "insert into soci_test(id,name,subname) values( 5,'anthony','wall')";

        {
            std::vector<int> v(10);

            statement s(sql.prepare << "Select id from soci_test where name = :name");

            std::string name = "john";

            s.exchange(use(name, "name"));
            s.exchange(into(v));

            s.define_and_bind();
            s.execute(true);

            assert(v.size() == 2);
        }
    }
    std::cout << "test 3 passed" << std::endl;
}


// Test case from Amnon David 11/1/2007
// I've noticed that table schemas in SQLite3 can sometimes have typeless
// columns. One (and only?) example is the sqlite_sequence that sqlite
// creates for autoincrement . Attempting to traverse this table caused
// SOCI to crash. I've made the following code change in statement.cpp to
// create a workaround:

struct test4_table_creator : table_creator_base
{
    test4_table_creator(session & sql) : table_creator_base(sql)
    {
        sql << "create table soci_test (col INTEGER PRIMARY KEY AUTOINCREMENT, name char)";
    }
};

void test4()
{
    {
        // we need to have an table that uses autoincrement to test this.
        session sql(backEnd, connectString);

        test4_table_creator tableCreator(sql);

        sql << "insert into soci_test(name) values('john')";
        sql << "insert into soci_test(name) values('james')";

        {
            int key;
            std::string name;
            sql << "select * from soci_test", into(key), into(name);
            assert(name == "john");

            rowset<row> rs = (sql.prepare << "select * from sqlite_sequence");
            rowset<row>::const_iterator it = rs.begin();
            row const& r1 = (*it);
            assert(r1.get<std::string>(0) == "soci_test");
            assert(r1.get<std::string>(1) == "2");
        }
    }
    std::cout << "test 4 passed" << std::endl;
}

struct longlong_table_creator : table_creator_base
{
    longlong_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val number(20))";
    }
};

// long long test
void test5()
{
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        long long v1 = 1000000000000LL;
        assert(v1 / 1000000 == 1000000);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        long long v2 = 0LL;
        sql << "select val from soci_test", into(v2);

        assert(v2 == v1);
    }

    // vector<long long>
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        std::vector<long long> v1;
        v1.push_back(1000000000000LL);
        v1.push_back(1000000000001LL);
        v1.push_back(1000000000002LL);
        v1.push_back(1000000000003LL);
        v1.push_back(1000000000004LL);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        std::vector<long long> v2(10);
        sql << "select val from soci_test order by val desc", into(v2);

        assert(v2.size() == 5);
        assert(v2[0] == 1000000000004LL);
        assert(v2[1] == 1000000000003LL);
        assert(v2[2] == 1000000000002LL);
        assert(v2[3] == 1000000000001LL);
        assert(v2[4] == 1000000000000LL);
    }

    std::cout << "test 5 passed" << std::endl;
}

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, val integer, c char, "
                 "str varchar(20), sh smallint, ul numeric(20), d float, "
                 "tm datetime, i1 integer, i2 integer, i3 integer, "
                 "name varchar(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(session & sql)
        : table_creator_base(sql)
    {
        sql  << "create table soci_test(num_float float, num_int integer,"
                     " name varchar(20), sometime datetime, chr char)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(name varchar(100) not null, "
            "phone varchar(15))";
    }
};

// Originally, submitted to SQLite3 backend and later moved to common test.
// Test commit b394d039530f124802d06c3b1a969c3117683152
// Author: Mika Fischer <mika.fischer@zoopnet.de>
// Date:   Thu Nov 17 13:28:07 2011 +0100
// Implement get_affected_rows for SQLite3 backend
struct table_creator_for_get_affected_rows : table_creator_base
{
    table_creator_for_get_affected_rows(session & sql)
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
    test_context(backend_factory const &backEnd,
                std::string const &connectString)
        : test_context_base(backEnd, connectString) {}

    table_creator_base* table_creator_1(session& s) const
    {
        return new table_creator_one(s);
    }

    table_creator_base* table_creator_2(session& s) const
    {
        return new table_creator_two(s);
    }

    table_creator_base* table_creator_3(session& s) const
    {
        return new table_creator_three(s);
    }

    table_creator_base* table_creator_4(session& s) const
    {
        return new table_creator_for_get_affected_rows(s);
    }

    std::string to_date_time(std::string const &datdt_string) const
    {
        return "datetime(\'" + datdt_string + "\')";
    }
};

int main(int argc, char** argv)
{

#ifdef _MSC_VER
    // Redirect errors, unrecoverable problems, and assert() failures to STDERR,
    // instead of debug message window.
    // This hack is required to run asser()-driven tests by Buildbot.
    // NOTE: Comment this 2 lines for debugging with Visual C++ debugger to catch assertions inside.
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif //_MSC_VER

    if (argc == 2)
    {
        connectString = argv[1];
    }
    else
    {
        // If no file name is specfied then work in-memory
        connectString = ":memory:";
    }

    try
    {
        test_context tc(backEnd, connectString);
        common_tests tests(tc);
        tests.run();

        std::cout << "\nSOCI sqlite3 Tests:\n\n";

        test1();
        test2();
        test3();
        test4();
        test5();

        std::cout << "\nOK, all tests passed.\n\n";

        return EXIT_SUCCESS;
    }
    catch (soci::soci_error const & e)
    {
        std::cout << "SOCIERROR: " << e.what() << '\n';
    }
    catch (std::exception const & e)
    {
        std::cout << "EXCEPTION: " << e.what() << '\n';
    }

    return EXIT_FAILURE;
}
