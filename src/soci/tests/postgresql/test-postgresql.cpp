//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"
#include "soci/postgresql/soci-postgresql.h"
#include "common-tests.h"
#include <iostream>
#include <sstream>
#include <string>
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
    oid_table_creator(soci::session& sql)
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
TEST_CASE("PostgreSQL ROWID", "[postgresql][rowid][oid]")
{
    soci::session sql(backEnd, connectString);

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

    CHECK(id == 7);
    CHECK(name == "John");
}

TEST_CASE("PostgreSQL prepare error", "[postgresql][exception]")
{
    soci::session sql(backEnd, connectString);

    // Must not cause the application to crash.
    statement st(sql);
    st.prepare(""); // Throws an exception in some versions.
}

// function call test
class function_creator : function_creator_base
{
public:

    function_creator(soci::session & sql)
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

TEST_CASE("PostgreSQL function call", "[postgresql][function]")
{
    soci::session sql(backEnd, connectString);

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
    CHECK(out == in);

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
        CHECK(out == in);
    }
}

// BLOB test
struct blob_table_creator : public table_creator_base
{
    blob_table_creator(soci::session & sql)
    : table_creator_base(sql)
    {
        sql <<
             "create table soci_test ("
             "    id integer,"
             "    img oid"
             ")";
    }
};

TEST_CASE("PostgreSQL blob", "[postgresql][blob]")
{
    {
        soci::session sql(backEnd, connectString);

        blob_table_creator tableCreator(sql);

        char buf[] = "abcdefghijklmnopqrstuvwxyz";

        sql << "insert into soci_test(id, img) values(7, lo_creat(-1))";

        // in PostgreSQL, BLOB operations must be within transaction block
        transaction tr(sql);

        {
            blob b(sql);

            sql << "select img from soci_test where id = 7", into(b);
            CHECK(b.get_len() == 0);

            b.write(0, buf, sizeof(buf));
            CHECK(b.get_len() == sizeof(buf));

            b.append(buf, sizeof(buf));
            CHECK(b.get_len() == 2 * sizeof(buf));
        }
        {
            blob b(sql);
            sql << "select img from soci_test where id = 7", into(b);
            CHECK(b.get_len() == 2 * sizeof(buf));
            char buf2[100];
            b.read(0, buf2, 10);
            CHECK(std::strncmp(buf2, "abcdefghij", 10) == 0);
        }

        unsigned long oid;
        sql << "select img from soci_test where id = 7", into(oid);
        sql << "select lo_unlink(" << oid << ")";
    }

    // additional sibling test for read_from_start and write_from_start
    {
        soci::session sql(backEnd, connectString);

        blob_table_creator tableCreator(sql);

        char buf[] = "abcdefghijklmnopqrstuvwxyz";

        sql << "insert into soci_test(id, img) values(7, lo_creat(-1))";

        // in PostgreSQL, BLOB operations must be within transaction block
        transaction tr(sql);

        {
            blob b(sql);

            sql << "select img from soci_test where id = 7", into(b);
            CHECK(b.get_len() == 0);

            b.write_from_start(buf, sizeof(buf));
            CHECK(b.get_len() == sizeof(buf));

            b.append(buf, sizeof(buf));
            CHECK(b.get_len() == 2 * sizeof(buf));
        }
        {
            blob b(sql);
            sql << "select img from soci_test where id = 7", into(b);
            CHECK(b.get_len() == 2 * sizeof(buf));
            char buf2[100];
            b.read_from_start(buf2, 10);
            CHECK(std::strncmp(buf2, "abcdefghij", 10) == 0);
        }

        unsigned long oid;
        sql << "select img from soci_test where id = 7", into(oid);
        sql << "select lo_unlink(" << oid << ")";
    }
}

struct longlong_table_creator : table_creator_base
{
    longlong_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val int8)";
    }
};

// long long test
TEST_CASE("PostgreSQL long long", "[postgresql][longlong]")
{
    soci::session sql(backEnd, connectString);

    longlong_table_creator tableCreator(sql);

    long long v1 = 1000000000000LL;
    sql << "insert into soci_test(val) values(:val)", use(v1);

    long long v2 = 0LL;
    sql << "select val from soci_test", into(v2);

    CHECK(v2 == v1);
}

// vector<long long>
TEST_CASE("PostgreSQL vector long long", "[postgresql][vector][longlong]")
{
    soci::session sql(backEnd, connectString);

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

    REQUIRE(v2.size() == 5);
    CHECK(v2[0] == 1000000000004LL);
    CHECK(v2[1] == 1000000000003LL);
    CHECK(v2[2] == 1000000000002LL);
    CHECK(v2[3] == 1000000000001LL);
    CHECK(v2[4] == 1000000000000LL);
}

// unsigned long long test
TEST_CASE("PostgreSQL unsigned long long", "[postgresql][unsigned][longlong]")
{
    soci::session sql(backEnd, connectString);

    longlong_table_creator tableCreator(sql);

    unsigned long long v1 = 1000000000000ULL;
    sql << "insert into soci_test(val) values(:val)", use(v1);

    unsigned long long v2 = 0ULL;
    sql << "select val from soci_test", into(v2);

    CHECK(v2 == v1);
}

struct boolean_table_creator : table_creator_base
{
    boolean_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val boolean)";
    }
};

TEST_CASE("PostgreSQL boolean", "[postgresql][boolean]")
{
    soci::session sql(backEnd, connectString);

    boolean_table_creator tableCreator(sql);

    int i1 = 0;

    sql << "insert into soci_test(val) values(:val)", use(i1);

    int i2 = 7;
    sql << "select val from soci_test", into(i2);

    CHECK(i2 == i1);

    sql << "update soci_test set val = true";
    sql << "select val from soci_test", into(i2);
    CHECK(i2 == 1);
}

struct uuid_table_creator : table_creator_base
{
    uuid_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val uuid)";
    }
};

// uuid test
TEST_CASE("PostgreSQL uuid", "[postgresql][uuid]")
{
    soci::session sql(backEnd, connectString);

    uuid_table_creator tableCreator(sql);

    std::string v1("cd2dcb78-3817-442e-b12a-17c7e42669a0");
    sql << "insert into soci_test(val) values(:val)", use(v1);

    std::string v2;
    sql << "select val from soci_test", into(v2);

    CHECK(v2 == v1);
}

// dynamic backend test -- currently skipped by default
TEST_CASE("PostgreSQL dynamic backend", "[postgresql][backend][.]")
{
    try
    {
        soci::session sql("nosuchbackend://" + connectString);
        FAIL("expected exception not thrown");
    }
    catch (soci_error const & e)
    {
        CHECK(e.get_error_message() ==
            "Failed to open: libsoci_nosuchbackend.so");
    }

    {
        dynamic_backends::register_backend("pgsql", backEnd);

        std::vector<std::string> backends = dynamic_backends::list_all();
        REQUIRE(backends.size() == 1);
        CHECK(backends[0] == "pgsql");

        {
            soci::session sql("pgsql://" + connectString);
        }

        dynamic_backends::unload("pgsql");

        backends = dynamic_backends::list_all();
        CHECK(backends.empty());
    }

    {
        soci::session sql("postgresql://" + connectString);
    }
}

TEST_CASE("PostgreSQL literals", "[postgresql][into]")
{
    soci::session sql(backEnd, connectString);

    int i;
    sql << "select 123", into(i);
    CHECK(i == 123);

    try
    {
        sql << "select 'ABC'", into (i);
        FAIL("expected exception not thrown");
    }
    catch (soci_error const & e)
    {
        char const * expectedPrefix = "Cannot convert data";
        CAPTURE(e.what());
        CHECK(strncmp(e.what(), expectedPrefix, strlen(expectedPrefix)) == 0);
    }
}

TEST_CASE("PostgreSQL backend name", "[postgresql][backend]")
{
    soci::session sql(backEnd, connectString);

    CHECK(sql.get_backend_name() == "postgresql");
}

// test for double-colon cast in SQL expressions
TEST_CASE("PostgreSQL double colon cast", "[postgresql][cast]")
{
    soci::session sql(backEnd, connectString);

    int a = 123;
    int b = 0;
    sql << "select :a::integer", use(a), into(b);
    CHECK(b == a);
}

// test for date, time and timestamp parsing
TEST_CASE("PostgreSQL datetime", "[postgresql][datetime]")
{
    soci::session sql(backEnd, connectString);

    std::string someDate = "2009-06-17 22:51:03.123";
    std::tm t1 = std::tm(), t2 = std::tm(), t3 = std::tm();

    sql << "select :sd::date, :sd::time, :sd::timestamp",
        use(someDate, "sd"), into(t1), into(t2), into(t3);

    // t1 should contain only the date part
    CHECK(t1.tm_year == 2009 - 1900);
    CHECK(t1.tm_mon == 6 - 1);
    CHECK(t1.tm_mday == 17);
    CHECK(t1.tm_hour == 0);
    CHECK(t1.tm_min == 0);
    CHECK(t1.tm_sec == 0);

    // t2 should contain only the time of day part
    CHECK(t2.tm_year == 0);
    CHECK(t2.tm_mon == 0);
    CHECK(t2.tm_mday == 1);
    CHECK(t2.tm_hour == 22);
    CHECK(t2.tm_min == 51);
    CHECK(t2.tm_sec == 3);

    // t3 should contain all information
    CHECK(t3.tm_year == 2009 - 1900);
    CHECK(t3.tm_mon == 6 - 1);
    CHECK(t3.tm_mday == 17);
    CHECK(t3.tm_hour == 22);
    CHECK(t3.tm_min == 51);
    CHECK(t3.tm_sec == 3);
}

// test for number of affected rows

struct table_creator_for_test11 : table_creator_base
{
    table_creator_for_test11(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

TEST_CASE("PostgreSQL get affected rows", "[postgresql][affected-rows]")
{
    soci::session sql(backEnd, connectString);

    table_creator_for_test11 tableCreator(sql);

    for (int i = 0; i != 10; i++)
    {
        sql << "insert into soci_test(val) values(:val)", use(i);
    }

    statement st1 = (sql.prepare <<
        "update soci_test set val = val + 1");
    st1.execute(false);

    CHECK(st1.get_affected_rows() == 10);

    statement st2 = (sql.prepare <<
        "delete from soci_test where val <= 5");
    st2.execute(false);

    CHECK(st2.get_affected_rows() == 5);
}

// test INSERT INTO ... RETURNING syntax

struct table_creator_for_test12 : table_creator_base
{
    table_creator_for_test12(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(sid serial, txt text)";
    }
};

TEST_CASE("PostgreSQL insert into ... returning", "[postgresql]")
{
    soci::session sql(backEnd, connectString);

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
    CHECK(std::equal(ids.begin(), ids.end(), ids2.begin()));
}

struct bytea_table_creator : public table_creator_base
{
    bytea_table_creator(soci::session& sql)
        : table_creator_base(sql)
    {
        sql << "drop table if exists soci_test;";
        sql << "create table soci_test ( val bytea null )";
    }
};

TEST_CASE("PostgreSQL bytea", "[postgresql][bytea]")
{
    soci::session sql(backEnd, connectString);

    // PostgreSQL supports two different output formats for bytea values:
    // historical "escape" format, which is the only one supported until
    // PostgreSQL 9.0, and "hex" format used by default since 9.0, we need
    // to determine which one is actually in use.
    std::string bytea_output_format;
    sql << "select setting from pg_settings where name='bytea_output'",
           into(bytea_output_format);
    char const* expectedBytea;
    if (bytea_output_format.empty() || bytea_output_format == "escape")
      expectedBytea = "\\015\\014\\013\\012";
    else if (bytea_output_format == "hex")
      expectedBytea = "\\x0d0c0b0a";
    else
      throw std::runtime_error("Unknown PostgreSQL bytea_output \"" +
                               bytea_output_format + "\"");

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
        CHECK(bin1 == expectedBytea);

        // 2) Oid-to-dt_string mapped
        row r;
        sql << "select * from soci_test", into(r);

        REQUIRE(r.size() == 1);
        column_properties const& props = r.get_properties(0);
        CHECK(props.get_data_type() == soci::dt_string);
        std::string bin2 = r.get<std::string>(0);
        CHECK(bin2 == expectedBytea);
    }
}

// json
struct table_creator_json : public table_creator_base
{
    table_creator_json(soci::session& sql)
    : table_creator_base(sql)
    {
        sql << "drop table if exists soci_json_test;";
        sql << "create table soci_json_test(data json)";
    }
};

// Return 9,2 for 9.2.3
typedef std::pair<int,int> server_version;

server_version get_postgresql_version(soci::session& sql)
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
TEST_CASE("PostgreSQL JSON", "[postgresql][json]")
{
    soci::session sql(backEnd, connectString);
    server_version version = get_postgresql_version(sql);
    if ( version >= server_version(9,2))
    {
        std::string result;
        std::string valid_input = "{\"tool\":\"soci\",\"result\":42}";
        std::string invalid_input = "{\"tool\":\"other\",\"result\":invalid}";

        table_creator_json tableCreator(sql);

        sql << "insert into soci_json_test (data) values(:data)",use(valid_input);
        sql << "select data from  soci_json_test",into(result);
        CHECK(result == valid_input);

        CHECK_THROWS_AS((
            sql << "insert into soci_json_test (data) values(:data)",use(invalid_input)),
            soci_error
        );
    }
    else
    {
        WARN("JSON test skipped (PostgreSQL >= 9.2 required, found " << version.first << "." << version.second << ")");
    }
}

struct table_creator_text : public table_creator_base
{
    table_creator_text(soci::session& sql) : table_creator_base(sql)
    {
        sql << "drop table if exists soci_test;";
        sql << "create table soci_test(name varchar(20))";
    }
};

// Test deallocate_prepared_statement called for non-existing statement
// which creation failed due to invalid SQL syntax.
// https://github.com/SOCI/soci/issues/116
TEST_CASE("PostgreSQL statement prepare failure", "[postgresql][prepare]")
{
    soci::session sql(backEnd, connectString);
    table_creator_text tableCreator(sql);

    try
    {
        // types mismatch should lead to PQprepare failure
        statement get_trades =
            (sql.prepare
                << "select * from soci_test where name=9999");
        FAIL("expected exception not thrown");
    }
    catch(soci_error const& e)
    {
        std::string const msg(e.what());
        CAPTURE(msg);

        // poor-man heuristics
        CHECK(msg.find("prepared statement") == std::string::npos);
        CHECK(msg.find("operator does not exist") != std::string::npos);
    }
}

// Test the support of PostgreSQL-style casts with ORM
TEST_CASE("PostgreSQL ORM cast", "[postgresql][orm]")
{
    soci::session sql(backEnd, connectString);
    values v;
    v.set("a", 1);
    sql << "select :a::int", use(v); // Must not throw an exception!
}

// Test the DDL and metadata functionality
TEST_CASE("PostgreSQL DDL with metadata", "[postgresql][ddl]")
{
    soci::session sql(backEnd, connectString);

    // note: prepare_column_descriptions expects l-value
    std::string ddl_t1 = "ddl_t1";
    std::string ddl_t2 = "ddl_t2";
    std::string ddl_t3 = "ddl_t3";

    // single-expression variant:
    sql.create_table(ddl_t1).column("i", soci::dt_integer).column("j", soci::dt_integer);

    // check whether this table was created:

    bool ddl_t1_found = false;
    bool ddl_t2_found = false;
    bool ddl_t3_found = false;
    std::string table_name;
    soci::statement st = (sql.prepare_table_names(), into(table_name));
    st.execute();
    while (st.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found);
    CHECK(ddl_t2_found == false);
    CHECK(ddl_t3_found == false);

    // check whether ddl_t1 has the right structure:

    bool i_found = false;
    bool j_found = false;
    bool other_found = false;
    soci::column_info ci;
    soci::statement st1 = (sql.prepare_column_descriptions(ddl_t1), into(ci));
    st1.execute();
    while (st1.fetch())
    {
        if (ci.name == "i")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable);
            i_found = true;
        }
        else if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable);
            j_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found);
    CHECK(j_found);
    CHECK(other_found == false);

    // two more tables:

    // separately defined columns:
    // (note: statement is executed when ddl object goes out of scope)
    {
        soci::ddl_type ddl = sql.create_table(ddl_t2);
        ddl.column("i", soci::dt_integer);
        ddl.column("j", soci::dt_integer);
        ddl.column("k", soci::dt_integer)("not null");
        ddl.primary_key("t2_pk", "j");
    }

    sql.add_column(ddl_t1, "k", soci::dt_integer);
    sql.add_column(ddl_t1, "big", soci::dt_string, 0); // "unlimited" length -> text
    sql.drop_column(ddl_t1, "i");

    // or with constraint as in t2:
    sql.add_column(ddl_t2, "m", soci::dt_integer)("not null");

    // third table with a foreign key to the second one
    {
        soci::ddl_type ddl = sql.create_table(ddl_t3);
        ddl.column("x", soci::dt_integer);
        ddl.column("y", soci::dt_integer);
        ddl.foreign_key("t3_fk", "x", ddl_t2, "j");
    }

    // check if all tables were created:

    ddl_t1_found = false;
    ddl_t2_found = false;
    ddl_t3_found = false;
    soci::statement st2 = (sql.prepare_table_names(), into(table_name));
    st2.execute();
    while (st2.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found);
    CHECK(ddl_t2_found);
    CHECK(ddl_t3_found);

    // check if ddl_t1 has the right structure (it was altered):

    i_found = false;
    j_found = false;
    bool k_found = false;
    bool big_found = false;
    other_found = false;
    soci::statement st3 = (sql.prepare_column_descriptions(ddl_t1), into(ci));
    st3.execute();
    while (st3.fetch())
    {
        if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable);
            j_found = true;
        }
        else if (ci.name == "k")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable);
            k_found = true;
        }
        else if (ci.name == "big")
        {
            CHECK(ci.type == soci::dt_string);
            CHECK(ci.precision == 0); // "unlimited" for strings
            big_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found == false);
    CHECK(j_found);
    CHECK(k_found);
    CHECK(big_found);
    CHECK(other_found == false);

    // check if ddl_t2 has the right structure:

    i_found = false;
    j_found = false;
    k_found = false;
    bool m_found = false;
    other_found = false;
    soci::statement st4 = (sql.prepare_column_descriptions(ddl_t2), into(ci));
    st4.execute();
    while (st4.fetch())
    {
        if (ci.name == "i")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable);
            i_found = true;
        }
        else if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable == false); // primary key
            j_found = true;
        }
        else if (ci.name == "k")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable == false);
            k_found = true;
        }
        else if (ci.name == "m")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.nullable == false);
            m_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found);
    CHECK(j_found);
    CHECK(k_found);
    CHECK(m_found);
    CHECK(other_found == false);

    sql.drop_table(ddl_t1);
    sql.drop_table(ddl_t3); // note: this must be dropped before ddl_t2
    sql.drop_table(ddl_t2);

    // check if all tables were dropped:

    ddl_t1_found = false;
    ddl_t2_found = false;
    ddl_t3_found = false;
    st2 = (sql.prepare_table_names(), into(table_name));
    st2.execute();
    while (st2.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found == false);
    CHECK(ddl_t2_found == false);
    CHECK(ddl_t3_found == false);

    int i = -1;
    sql << "select lo_unlink(" + sql.empty_blob() + ")", into(i);
    CHECK(i == 1);
    sql << "select " + sql.nvl() + "(1, 2)", into(i);
    CHECK(i == 1);
    sql << "select " + sql.nvl() + "(NULL, 2)", into(i);
    CHECK(i == 2);
}

// Test the bulk iterators functionality
TEST_CASE("Bulk iterators", "[postgresql][bulkiters]")
{
    soci::session sql(backEnd, connectString);

    sql << "create table t (i integer)";

    // test bulk iterators with basic types
    {
        std::vector<int> v;
        v.push_back(10);
        v.push_back(20);
        v.push_back(30);
        v.push_back(40);
        v.push_back(50);

        std::size_t begin = 2;
        std::size_t end = 5;
        sql << "insert into t (i) values (:v)", soci::use(v, begin, end);

        v.clear();
        v.resize(20);
        begin = 5;
        end = 20;
        sql << "select i from t", soci::into(v, begin, end);

        CHECK(end == 8);
        for (std::size_t i = 0; i != 5; ++i)
        {
            CHECK(v[i] == 0);
        }
        CHECK(v[5] == 30);
        CHECK(v[6] == 40);
        CHECK(v[7] == 50);
        for (std::size_t i = end; i != 20; ++i)
        {
            CHECK(v[i] == 0);
        }
    }

    sql << "delete from t";

    // test bulk iterators with user types
    {
        std::vector<MyInt> v;
        v.push_back(MyInt(10));
        v.push_back(MyInt(20));
        v.push_back(MyInt(30));
        v.push_back(MyInt(40));
        v.push_back(MyInt(50));

        std::size_t begin = 2;
        std::size_t end = 5;
        sql << "insert into t (i) values (:v)", soci::use(v, begin, end);

        v.clear();
        for (std::size_t i = 0; i != 20; ++i)
        {
            v.push_back(MyInt(-1));
        }

        begin = 5;
        end = 20;
        sql << "select i from t", soci::into(v, begin, end);

        CHECK(end == 8);
        for (std::size_t i = 0; i != 5; ++i)
        {
            CHECK(v[i].get() == -1);
        }
        CHECK(v[5].get() == 30);
        CHECK(v[6].get() == 40);
        CHECK(v[7].get() == 50);
        for (std::size_t i = end; i != 20; ++i)
        {
            CHECK(v[i].get() == -1);
        }
    }

    sql << "drop table t";
}

//
// Support for soci Common Tests
//

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

// Common tests context
class test_context : public test_context_base
{
public:
    test_context(backend_factory const &backEnd, std::string const &connectString)
        : test_context_base(backEnd, connectString)
    {}

    table_creator_base* table_creator_1(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_one(s);
    }

    table_creator_base* table_creator_2(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_two(s);
    }

    table_creator_base* table_creator_3(soci::session& s) const SOCI_OVERRIDE
    {
        return new table_creator_three(s);
    }

    table_creator_base* table_creator_4(soci::session& s) const SOCI_OVERRIDE
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
        return false;
    }

    std::string sql_length(std::string const& s) const SOCI_OVERRIDE
    {
        return "char_length(" + s + ")";
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

    if (argc >= 2)
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
        std::cout << "usage: " << argv[0]
            << " connectstring [test-arguments...]\n"
            << "example: " << argv[0]
            << " \'connect_string_for_PostgreSQL\'\n";
        return EXIT_FAILURE;
    }

    test_context tc(backEnd, connectString);

    return Catch::Session().run(argc, argv);
}
