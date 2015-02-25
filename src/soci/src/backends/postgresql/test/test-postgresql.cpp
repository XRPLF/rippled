//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-postgresql.h"
#include "common-tests.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdlib>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_postgresql();

// Postgres-specific tests

struct oid_table_creator : public table_creator_base
{
    oid_table_creator(session& sql)
    : table_creator_base(sql)
    {
        sql << "create table soci_test ("
                " id integer,"
                " name varchar(100)"
                ") with oids";
    }
};

// ROWID test
// Note: in PostgreSQL, there is no ROWID, there is OID.
// It is still provided as a separate type for "portability",
// whatever that means.
void test1()
{
    try
    {
        session sql(backEnd, connectString);

        oid_table_creator tableCreator(sql);

        sql << "insert into soci_test(id, name) values(7, \'John\')";

        rowid rid(sql);
        sql << "select oid from soci_test where id = 7", into(rid);

        int id;
        std::string name;

#ifndef SOCI_POSTGRESQL_NOPARAMS

        sql << "select id, name from soci_test where oid = :rid",
            into(id), into(name), use(rid);

#else
        // Older PostgreSQL does not support use elements.

        postgresql_rowid_backend *rbe
            = static_cast<postgresql_rowid_backend *>(rid.get_backend());

        unsigned long oid = rbe->value_;

        sql << "select id, name from soci_test where oid = " << oid,
            into(id), into(name);

#endif // SOCI_POSTGRESQL_NOPARAMS

        assert(id == 7);
        assert(name == "John");
        
    	// Must not cause the application to crash.
		statement st(sql);
		st.prepare(""); // Throws an exception in some versions.
    }
    catch(...)
    {
    }
    std::cout << "test 1 passed" << std::endl;
}

// function call test
class function_creator : function_creator_base
{
public:

    function_creator(session & sql)
    : function_creator_base(sql)
    {
        // before a language can be used it must be defined
        // if it has already been defined then an error will occur
        try { sql << "create language plpgsql"; }
        catch (soci_error const &) {} // ignore if error

#ifndef SOCI_POSTGRESQL_NOPARAMS

        sql  <<
            "create or replace function soci_test(msg varchar) "
            "returns varchar as $$ "
            "declare x int := 1;"
            "begin "
            "  return msg; "
            "end $$ language plpgsql";
#else

       sql <<
            "create or replace function soci_test(varchar) "
            "returns varchar as \' "
            "declare x int := 1;"
            "begin "
            "  return $1; "
            "end \' language plpgsql";
#endif
    }

protected:

    std::string drop_statement()
    {
        return "drop function soci_test(varchar)";
    }
};

void test2()
{
    {
        session sql(backEnd, connectString);

        function_creator functionCreator(sql);

        std::string in("my message");
        std::string out;

#ifndef SOCI_POSTGRESQL_NOPARAMS

        statement st = (sql.prepare <<
            "select soci_test(:input)",
            into(out),
            use(in, "input"));

#else
        // Older PostgreSQL does not support use elements.

        statement st = (sql.prepare <<
            "select soci_test(\'" << in << "\')",
            into(out));

#endif // SOCI_POSTGRESQL_NOPARAMS

        st.execute(true);
        assert(out == in);

        // explicit procedure syntax
        {
            std::string in("my message2");
            std::string out;

#ifndef SOCI_POSTGRESQL_NOPARAMS

            procedure proc = (sql.prepare <<
                "soci_test(:input)",
                into(out), use(in, "input"));

#else
        // Older PostgreSQL does not support use elements.

            procedure proc = (sql.prepare <<
                "soci_test(\'" << in << "\')", into(out));

#endif // SOCI_POSTGRESQL_NOPARAMS

            proc.execute(true);
            assert(out == in);
        }
    }

    std::cout << "test 2 passed" << std::endl;
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
             "    img oid"
             ")";
    }
};

void test3()
{
    {
        session sql(backEnd, connectString);

        blob_table_creator tableCreator(sql);

        char buf[] = "abcdefghijklmnopqrstuvwxyz";

        sql << "insert into soci_test(id, img) values(7, lo_creat(-1))";

        // in PostgreSQL, BLOB operations must be within transaction block
        transaction tr(sql);

        {
            blob b(sql);

            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == 0);

            b.write(0, buf, sizeof(buf));
            assert(b.get_len() == sizeof(buf));

            b.append(buf, sizeof(buf));
            assert(b.get_len() == 2 * sizeof(buf));
        }
        {
            blob b(sql);
            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == 2 * sizeof(buf));
            char buf2[100];
            b.read(0, buf2, 10);
            assert(std::strncmp(buf2, "abcdefghij", 10) == 0);
        }

        unsigned long oid;
        sql << "select img from soci_test where id = 7", into(oid);
        sql << "select lo_unlink(" << oid << ")";
    }

    std::cout << "test 3 passed" << std::endl;
}

struct longlong_table_creator : table_creator_base
{
    longlong_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val int8)";
    }
};

// long long test
void test4()
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

    std::cout << "test 4 passed" << std::endl;
}

// unsigned long long test
void test4ul()
{
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        unsigned long long v1 = 1000000000000ULL;
        assert(v1 / 1000000 == 1000000);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        unsigned long long v2 = 0ULL;
        sql << "select val from soci_test", into(v2);

        assert(v2 == v1);
    }
}

struct boolean_table_creator : table_creator_base
{
    boolean_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val boolean)";
    }
};

void test5()
{
    {
        session sql(backEnd, connectString);

        boolean_table_creator tableCreator(sql);

        int i1 = 0;

        sql << "insert into soci_test(val) values(:val)", use(i1);

        int i2 = 7;
        sql << "select val from soci_test", into(i2);

        assert(i2 == i1);

        sql << "update soci_test set val = true";
        sql << "select val from soci_test", into(i2);
        assert(i2 == 1);
    }

    std::cout << "test 5 passed" << std::endl;
}

// dynamic backend test
void test6()
{
    try
    {
        session sql("nosuchbackend://" + connectString);
        assert(false);
    }
    catch (soci_error const & e)
    {
        assert(e.what() == std::string("Failed to open: libsoci_nosuchbackend.so"));
    }

    {
        dynamic_backends::register_backend("pgsql", backEnd);

        std::vector<std::string> backends = dynamic_backends::list_all();
        assert(backends.size() == 1);
        assert(backends[0] == "pgsql");

        {
            session sql("pgsql://" + connectString);
        }

        dynamic_backends::unload("pgsql");

        backends = dynamic_backends::list_all();
        assert(backends.empty());
    }

    {
        session sql("postgresql://" + connectString);
    }

    std::cout << "test 6 passed" << std::endl;
}

void test7()
{
    {
        session sql(backEnd, connectString);

        int i;
        sql << "select 123", into(i);
        assert(i == 123);

        try
        {
            sql << "select 'ABC'", into (i);
            assert(false);
        }
        catch (soci_error const & e)
        {
            assert(e.what() == std::string("Cannot convert data."));
        }
    }

    std::cout << "test 7 passed" << std::endl;
}

void test8()
{
    {
        session sql(backEnd, connectString);

        assert(sql.get_backend_name() == "postgresql");
    }

    std::cout << "test 8 passed" << std::endl;
}

// test for double-colon cast in SQL expressions
void test9()
{
    {
        session sql(backEnd, connectString);

        int a = 123;
        int b = 0;
        sql << "select :a::integer", use(a), into(b);
        assert(b == a);
    }

    std::cout << "test 9 passed" << std::endl;
}

// test for date, time and timestamp parsing
void test10()
{
    {
        session sql(backEnd, connectString);

        std::string someDate = "2009-06-17 22:51:03.123";
        std::tm t1, t2, t3;

        sql << "select :sd::date, :sd::time, :sd::timestamp",
            use(someDate, "sd"), into(t1), into(t2), into(t3);

        // t1 should contain only the date part
        assert(t1.tm_year == 2009 - 1900);
        assert(t1.tm_mon == 6 - 1);
        assert(t1.tm_mday == 17);
        assert(t1.tm_hour == 0);
        assert(t1.tm_min == 0);
        assert(t1.tm_sec == 0);

        // t2 should contain only the time of day part
        assert(t2.tm_year == 0);
        assert(t2.tm_mon == 0);
        assert(t2.tm_mday == 1);
        assert(t2.tm_hour == 22);
        assert(t2.tm_min == 51);
        assert(t2.tm_sec == 3);

        // t3 should contain all information
        assert(t3.tm_year == 2009 - 1900);
        assert(t3.tm_mon == 6 - 1);
        assert(t3.tm_mday == 17);
        assert(t3.tm_hour == 22);
        assert(t3.tm_min == 51);
        assert(t3.tm_sec == 3);
    }

    std::cout << "test 10 passed" << std::endl;
}

// test for number of affected rows

struct table_creator_for_test11 : table_creator_base
{
    table_creator_for_test11(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

void test11()
{
    {
        session sql(backEnd, connectString);

        table_creator_for_test11 tableCreator(sql);

        for (int i = 0; i != 10; i++)
        {
            sql << "insert into soci_test(val) values(:val)", use(i);
        }

        statement st1 = (sql.prepare <<
            "update soci_test set val = val + 1");
        st1.execute(false);

        assert(st1.get_affected_rows() == 10);

        statement st2 = (sql.prepare <<
            "delete from soci_test where val <= 5");
        st2.execute(false);

        assert(st2.get_affected_rows() == 5);
    }

    std::cout << "test 11 passed" << std::endl;
}

// test INSERT INTO ... RETURNING syntax

struct table_creator_for_test12 : table_creator_base
{
    table_creator_for_test12(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(sid serial, txt text)";
    }
};

void test12()
{
    {
        session sql(backEnd, connectString);

        table_creator_for_test12 tableCreator(sql);

        std::vector<long> ids(10);
        for (std::size_t i = 0; i != ids.size(); i++)
        {
            long sid(0);
            std::string txt("abc");
            sql << "insert into soci_test(txt) values(:txt) returning sid", use(txt, "txt"), into(sid);
            ids[i] = sid;
        }
        
        std::vector<long> ids2(ids.size());
        sql << "select sid from soci_test order by sid", into(ids2);
        assert(std::equal(ids.begin(), ids.end(), ids2.begin()));
    }

    std::cout << "test 12 passed" << std::endl;
}

struct bytea_table_creator : public table_creator_base
{
    bytea_table_creator(session& sql)
        : table_creator_base(sql)
    {
        sql << "drop table if exists soci_test;";
        sql << "create table soci_test ( val bytea null )";
    }
};

void test_bytea()
{
    {
        session sql(backEnd, connectString);
        bytea_table_creator tableCreator(sql);

        int v = 0x0A0B0C0D;
        unsigned char* b = reinterpret_cast<unsigned char*>(&v);
        std::string data;
        std::copy(b, b + sizeof(v), std::back_inserter(data));
        {

            sql << "insert into soci_test(val) values(:val)", use(data);

            // 1) into string, no Oid mapping
            std::string bin1;
            sql << "select val from soci_test", into(bin1);
            assert(bin1 == "\\x0d0c0b0a");

            // 2) Oid-to-dt_string mapped
            row r;
            sql << "select * from soci_test", into(r);

            assert(r.size() == 1);
            column_properties const& props = r.get_properties(0);
            assert(props.get_data_type() == soci::dt_string);
            std::string bin2 = r.get<std::string>(0);
            assert(bin2 == "\\x0d0c0b0a");
        }
    }
    std::cout << "test bytea passed" << std::endl;
}

// json
struct table_creator_json : public table_creator_base
{
    table_creator_json(session& sql)
    : table_creator_base(sql)
    {
        sql << "drop table if exists soci_json_test;";
        sql << "create table soci_json_test(data json)";
    }
};

// Return 9,2 for 9.2.3
typedef std::pair<int,int> server_version;

server_version get_postgresql_version(session& sql)
{
    std::string version;
    std::pair<int,int> result;
    sql << "select version()",into(version);
    if (sscanf(version.c_str(),"PostgreSQL %i.%i", &result.first, &result.second) < 2)
    {
        throw std::runtime_error("Failed to retrieve PostgreSQL version number");
    }
    return result;
}

// Test JSON. Only valid for PostgreSQL Server 9.2++
void test_json()
{
    session sql(backEnd, connectString);
    server_version version = get_postgresql_version(sql);
    if ( version >= server_version(9,2))
    {
        bool exception = false;
        std::string result;
        std::string valid_input = "{\"tool\":\"soci\",\"result\":42}";
        std::string invalid_input = "{\"tool\":\"other\",\"result\":invalid}";

        table_creator_json tableCreator(sql);

        sql << "insert into soci_json_test (data) values(:data)",use(valid_input);
        sql << "select data from  soci_json_test",into(result);
        assert(result == valid_input);

        try
        {
            sql << "insert into soci_json_test (data) values(:data)",use(invalid_input);
        }
        catch(soci_error& e)
        {
            (void)e;
            exception = true;
        }
        assert(exception);
        std::cout << "test json passed" << std::endl;
    }
    else
    {
    std::cout << "test json skipped (PostgreSQL >= 9.2 required, found " << version.first << "." << version.second << ")" << std::endl;
    }
}

struct table_creator_text : public table_creator_base
{
    table_creator_text(session& sql) : table_creator_base(sql)
    {
        sql << "drop table if exists soci_test;";
        sql << "create table soci_test(name varchar(20))";
    }
};

// Test deallocate_prepared_statement called for non-existing statement
// which creation failed due to invalid SQL syntax.
// https://github.com/SOCI/soci/issues/116
void test_statement_prepare_failure()
{
    {
        session sql(backEnd, connectString);
        table_creator_text tableCreator(sql);

        try
        {
            // types mismatch should lead to PQprepare failure
            statement get_trades =
                (sql.prepare 
                    << "select * from soci_test where name=9999");
            assert(false);
        }
        catch(soci_error const& e)
        {
            std::string const msg(e.what());
            // poor-man heuristics
            assert(msg.find("prepared statement") == std::string::npos);
            assert(msg.find("operator does not exist") != std::string::npos);
        }
    }
    std::cout << "test_statement_prepare_failure passed" << std::endl;
}

// Test the support of PostgreSQL-style casts with ORM
void test_orm_cast()
{
    session sql(backEnd, connectString);
    values v;
    v.set("a", 1);
    sql << "select :a::int", use(v); // Must not throw an exception!
}

//
// Support for soci Common Tests
//

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer, val integer, c char, "
                 "str varchar(20), sh int2, ul numeric(20), d float8, "
                 "tm timestamp, i1 integer, i2 integer, i3 integer, "
                 "name varchar(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(session & sql)
        : table_creator_base(sql)
    {
        sql  << "create table soci_test(num_float float8, num_int integer,"
                     " name varchar(20), sometime timestamp, chr char)";
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

struct table_creator_for_get_affected_rows : table_creator_base
{
    table_creator_for_get_affected_rows(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

// Common tests context
class test_context : public test_context_base
{
public:
    test_context(backend_factory const &backEnd, std::string const &connectString)
        : test_context_base(backEnd, connectString)
    {}

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
        return "timestamptz(\'" + datdt_string + "\')";
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
        std::cout << "usage: " << argv[0]
            << " connectstring\n"
            << "example: " << argv[0]
            << " \'connect_string_for_PostgreSQL\'\n";
        return EXIT_FAILURE;
    }

    try
    {
        test_context tc(backEnd, connectString);
        common_tests tests(tc);
        tests.run();

        std::cout << "\nSOCI PostgreSQL Tests:\n\n";
        test1();
        test2();
        test3();
        test4();
        test4ul();
        test5();
        //test6();
        std::cout << "test 6 skipped (dynamic backend)\n";
        test7();
        test8();
        test9();
        test10();
        test11();
        test12();
        test_bytea();
        test_json();
        test_statement_prepare_failure();
        test_orm_cast();

        std::cout << "\nOK, all tests passed.\n\n";

        return EXIT_SUCCESS;
    }
    catch (std::exception const & e)
    {
        std::cout << e.what() << '\n';
    }
    return EXIT_FAILURE;
}
