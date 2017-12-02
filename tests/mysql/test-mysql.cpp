//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"
#include "mysql/test-mysql.h"
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <ciso646>
#include <cstdlib>
#include <mysqld_error.h>
#include <errmsg.h>

std::string connectString;
backend_factory const &backEnd = *soci::factory_mysql();

// procedure call test
TEST_CASE("MySQL stored procedures", "[mysql][stored-procedure]")
{
    soci::session sql(backEnd, connectString);

    mysql_session_backend *sessionBackEnd
        = static_cast<mysql_session_backend *>(sql.get_backend());
    std::string version = mysql_get_server_info(sessionBackEnd->conn_);
    int v;
    std::istringstream iss(version);
    if ((iss >> v) && v < 5)
    {
        WARN("MySQL server version " << v
                << " does not support stored procedures, skipping test.");
        return;
    }

    try { sql << "drop function myecho"; }
    catch (soci_error const &) {}

    sql <<
        "create function myecho(msg text) "
        "returns text deterministic "
        "  return msg; ";

    std::string in("my message");
    std::string out;

    statement st = (sql.prepare <<
        "select myecho(:input)",
        into(out),
        use(in, "input"));

    st.execute(1);
    CHECK(out == in);

    // explicit procedure syntax
    {
        std::string in("my message2");
        std::string out;

        procedure proc = (sql.prepare <<
            "myecho(:input)",
            into(out), use(in, "input"));

        proc.execute(1);
        CHECK(out == in);
    }

    sql << "drop function myecho";
}

// MySQL error reporting test.
TEST_CASE("MySQL error reporting", "[mysql][exception]")
{
    {
        try
        {
            soci::session sql(backEnd, "host=test.soci.invalid");
        }
        catch (mysql_soci_error const &e)
        {
            if (e.err_num_ != CR_UNKNOWN_HOST &&
                   e.err_num_ != CR_CONN_HOST_ERROR)
            {
                CAPTURE(e.err_num_);
                FAIL("Unexpected error trying to connect to invalid host.");
            }
        }
    }

    {
        soci::session sql(backEnd, connectString);
        sql << "create table soci_test (id integer)";
        try
        {
            int n;
            sql << "select id from soci_test_nosuchtable", into(n);
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_NO_SUCH_TABLE);
        }
        try
        {
            sql << "insert into soci_test (invalid) values (256)";
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_BAD_FIELD_ERROR);
        }
        // A bulk operation.
        try
        {
            std::vector<int> v(3, 5);
            sql << "insert into soci_test_nosuchtable values (:n)", use(v);
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_NO_SUCH_TABLE);
        }
        sql << "drop table soci_test";
    }
}

struct bigint_table_creator : table_creator_base
{
    bigint_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val bigint)";
    }
};

struct bigint_unsigned_table_creator : table_creator_base
{
    bigint_unsigned_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val bigint unsigned)";
    }
};

TEST_CASE("MySQL long long", "[mysql][longlong]")
{
    {
        soci::session sql(backEnd, connectString);

        bigint_table_creator tableCreator(sql);

        long long v1 = 1000000000000LL;
        sql << "insert into soci_test(val) values(:val)", use(v1);

        long long v2 = 0LL;
        sql << "select val from soci_test", into(v2);

        CHECK(v2 == v1);
    }

    // vector<long long>
    {
        soci::session sql(backEnd, connectString);

        bigint_table_creator tableCreator(sql);

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

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        sql << "insert into soci_test set val = 18446744073709551615";
        row v;
        sql << "select * from soci_test", into(v);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        const char* source = "18446744073709551615";
        sql << "insert into soci_test set val = " << source;
        unsigned long long vv = 0;
        sql << "select val from soci_test", into(vv);
        std::stringstream buf;
        buf << vv;
        CHECK(buf.str() == source);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        const char* source = "18446744073709551615";
        sql << "insert into soci_test set val = " << source;
        std::vector<unsigned long long> v(1);
        sql << "select val from soci_test", into(v);
        std::stringstream buf;
        buf << v.at(0);
        CHECK(buf.str() == source);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        unsigned long long n = 18446744073709551615ULL;
        sql << "insert into soci_test(val) values (:n)", use(n);
        unsigned long long m = 0;
        sql << "select val from soci_test", into(m);
        CHECK(n == m);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        std::vector<unsigned long long> v1;
        v1.push_back(18446744073709551615ULL);
        v1.push_back(18446744073709551614ULL);
        v1.push_back(18446744073709551613ULL);
        sql << "insert into soci_test(val) values(:val)", use(v1);

        std::vector<unsigned long long> v2(10);
        sql << "select val from soci_test order by val", into(v2);

        REQUIRE(v2.size() == 3);
        CHECK(v2[0] == 18446744073709551613ULL);
        CHECK(v2[1] == 18446744073709551614ULL);
        CHECK(v2[2] == 18446744073709551615ULL);
    }
}

template <typename T>
void test_num(const char* s, bool valid, T value)
{
    try
    {
        soci::session sql(backEnd, connectString);
        T val;
        sql << "select \'" << s << "\'", into(val);
        if (valid)
        {
            double v1 = static_cast<double>(value);
            double v2 = static_cast<double>(val);
            double d = std::fabs(v1 - v2);
            double epsilon = 0.001;
            if (d >= epsilon &&
                   d >= epsilon * (std::fabs(v1) + std::fabs(v2)))
            {
                FAIL("Difference between " << value
                       << " and " << val << " is too big.");
            }
        }
        else
        {
            FAIL("string \"" << s << "\" parsed as " << val
                      << " but should have failed.");
        }
    }
    catch (soci_error const& e)
    {
        if (valid)
        {
            FAIL("couldn't parse number: \"" << s << "\"");
        }
        else
        {
            char const * expectedPrefix = "Cannot convert data";
            CAPTURE(e.what());
            CHECK(strncmp(e.what(), expectedPrefix, strlen(expectedPrefix)) == 0);
        }
    }
}

// Number conversion test.
TEST_CASE("MySQL number conversion", "[mysql][float][int]")
{
    test_num<double>("", false, 0);
    test_num<double>("foo", false, 0);
    test_num<double>("1", true, 1);
    test_num<double>("12", true, 12);
    test_num<double>("123", true, 123);
    test_num<double>("12345", true, 12345);
    test_num<double>("12341234123412341234123412341234123412341234123412341",
        true, 1.23412e+52);
    test_num<double>("99999999999999999999999912222222222222222222222222223"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333",
        false, 0);
    test_num<double>("1e3", true, 1000);
    test_num<double>("1.2", true, 1.2);
    test_num<double>("1.2345e2", true, 123.45);
    test_num<double>("1 ", false, 0);
    test_num<double>("     123", true, 123);
    test_num<double>("1,2", false, 0);
    test_num<double>("123abc", false, 0);
    test_num<double>("-0", true, 0);

    test_num<short>("123", true, 123);
    test_num<short>("100000", false, 0);

    test_num<int>("123", true, 123);
    test_num<int>("2147483647", true, 2147483647);
    test_num<int>("2147483647a", false, 0);
    test_num<int>("2147483648", false, 0);
    // -2147483648 causes a warning because it is interpreted as
    // 2147483648 (which doesn't fit in an integer) to which a negation
    // is applied.
    test_num<int>("-2147483648", true, -2147483647 - 1);
    test_num<int>("-2147483649", false, 0);
    test_num<int>("-0", true, 0);
    test_num<int>("1.1", false, 0);

    test_num<long long>("123", true, 123);
    test_num<long long>("9223372036854775807", true, 9223372036854775807LL);
    test_num<long long>("9223372036854775808", false, 0);
}

TEST_CASE("MySQL datetime", "[mysql][datetime]")
{
    soci::session sql(backEnd, connectString);
    std::tm t = std::tm();
    sql << "select maketime(19, 54, 52)", into(t);
    CHECK(t.tm_year == 0);
    CHECK(t.tm_mon == 0);
    CHECK(t.tm_mday == 1);
    CHECK(t.tm_hour == 19);
    CHECK(t.tm_min == 54);
    CHECK(t.tm_sec == 52);
}

// TEXT and BLOB types support test.
TEST_CASE("MySQL text and blob", "[mysql][text][blob]")
{
    soci::session sql(backEnd, connectString);
    std::string a("asdfg\0hjkl", 10);
    std::string b("lkjhg\0fd\0\0sa\0", 13);
    std::string c("\\0aa\\0bb\\0cc\\0", 10);
    // The maximum length for TEXT and BLOB is 65536.
    std::string x(60000, 'X');
    std::string y(60000, 'Y');
    // The default max_allowed_packet value for a MySQL server is 1M,
    // so let's limit ourselves to 800k, even though the maximum length
    // for LONGBLOB is 4G.
    std::string z(800000, 'Z');

    sql << "create table soci_test (id int, text_value text, "
        "blob_value blob, longblob_value longblob)";
    sql << "insert into soci_test values (1, \'foo\', \'bar\', \'baz\')";
    sql << "insert into soci_test "
        << "values (2, \'qwerty\\0uiop\', \'zxcv\\0bnm\', "
        << "\'qwerty\\0uiop\\0zxcvbnm\\0\')";
    sql << "insert into soci_test values (3, :a, :b, :c)",
           use(a), use(b), use(c);
    sql << "insert into soci_test values (4, :x, :y, :z)",
           use(x), use(y), use(z);

    std::vector<std::string> text_vec(100);
    std::vector<std::string> blob_vec(100);
    std::vector<std::string> longblob_vec(100);
    sql << "select text_value, blob_value, longblob_value "
        << "from soci_test order by id",
           into(text_vec), into(blob_vec), into(longblob_vec);
    REQUIRE(text_vec.size() == 4);
    REQUIRE(blob_vec.size() == 4);
    REQUIRE(longblob_vec.size() == 4);
    CHECK(text_vec[0] == "foo");
    CHECK(blob_vec[0] == "bar");
    CHECK(longblob_vec[0] == "baz");
    CHECK(text_vec[1] == std::string("qwerty\0uiop", 11));
    CHECK(blob_vec[1] == std::string("zxcv\0bnm", 8));
    CHECK(longblob_vec[1] == std::string("qwerty\0uiop\0zxcvbnm\0", 20));
    CHECK(text_vec[2] == a);
    CHECK(blob_vec[2] == b);
    CHECK(longblob_vec[2] == c);
    CHECK(text_vec[3] == x);
    CHECK(blob_vec[3] == y);
    CHECK(longblob_vec[3] == z);

    std::string text, blob, longblob;
    sql << "select text_value, blob_value, longblob_value "
        << "from soci_test where id = 1",
           into(text), into(blob), into(longblob);
    CHECK(text == "foo");
    CHECK(blob == "bar");
    CHECK(longblob == "baz");
    sql << "select text_value, blob_value, longblob_value "
        << "from soci_test where id = 2",
           into(text), into(blob), into(longblob);
    CHECK(text == std::string("qwerty\0uiop", 11));
    CHECK(blob == std::string("zxcv\0bnm", 8));
    CHECK(longblob == std::string("qwerty\0uiop\0zxcvbnm\0", 20));
    sql << "select text_value, blob_value, longblob_value "
        << "from soci_test where id = 3",
           into(text), into(blob), into(longblob);
    CHECK(text == a);
    CHECK(blob == b);
    CHECK(longblob == c);
    sql << "select text_value, blob_value, longblob_value "
        << "from soci_test where id = 4",
           into(text), into(blob), into(longblob);
    CHECK(text == x);
    CHECK(blob == y);
    CHECK(longblob == z);

    rowset<row> rs =
        (sql.prepare << "select text_value, blob_value, longblob_value "
                        "from soci_test order by id");
    rowset<row>::const_iterator r = rs.begin();
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get<std::string>(0) == "foo");
    CHECK(r->get_properties(1).get_data_type() == dt_string);
    CHECK(r->get<std::string>(1) == "bar");
    CHECK(r->get_properties(2).get_data_type() == dt_string);
    CHECK(r->get<std::string>(2) == "baz");
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get<std::string>(0) == std::string("qwerty\0uiop", 11));
    CHECK(r->get_properties(1).get_data_type() == dt_string);
    CHECK(r->get<std::string>(1) == std::string("zxcv\0bnm", 8));
    CHECK(r->get_properties(2).get_data_type() == dt_string);
    CHECK(r->get<std::string>(2) ==
           std::string("qwerty\0uiop\0zxcvbnm\0", 20));
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get<std::string>(0) == a);
    CHECK(r->get_properties(1).get_data_type() == dt_string);
    CHECK(r->get<std::string>(1) == b);
    CHECK(r->get_properties(2).get_data_type() == dt_string);
    CHECK(r->get<std::string>(2) == c);
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get<std::string>(0) == x);
    CHECK(r->get_properties(1).get_data_type() == dt_string);
    CHECK(r->get<std::string>(1) == y);
    CHECK(r->get_properties(2).get_data_type() == dt_string);
    CHECK(r->get<std::string>(2) == z);
    ++r;
    CHECK(r == rs.end());

    sql << "drop table soci_test";
}

// test for number of affected rows

struct integer_value_table_creator : table_creator_base
{
    integer_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

TEST_CASE("MySQL get affected rows", "[mysql][affected-rows]")
{
    soci::session sql(backEnd, connectString);

    integer_value_table_creator tableCreator(sql);

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


// The prepared statements should survive session::reconnect().
// However currently it doesn't and attempting to use it results in crashes due
// to accessing the already destroyed session backend, so disable this test.
TEST_CASE("MySQL statements after reconnect", "[mysql][connect][.]")
{
    soci::session sql(backEnd, connectString);

    integer_value_table_creator tableCreator(sql);

    int i;
    statement st = (sql.prepare
        << "insert into soci_test(val) values(:val)", use(i));
    i = 5;
    st.execute(true);

    sql.reconnect();

    i = 6;
    st.execute(true);

    sql.close();
    sql.reconnect();

    i = 7;
    st.execute(true);

    std::vector<int> v(5);
    sql << "select val from soci_test order by val", into(v);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 5);
    CHECK(v[1] == 6);
    CHECK(v[2] == 7);
}

struct unsigned_value_table_creator : table_creator_base
{
    unsigned_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val int unsigned)";
    }
};

// rowset<> should be able to take INT UNSIGNED.
TEST_CASE("MySQL unsigned int", "[mysql][int]")
{
    soci::session sql(backEnd, connectString);

    unsigned_value_table_creator tableCreator(sql);

    unsigned int mask = 0xffffff00;
    sql << "insert into soci_test set val = " << mask;
    soci::rowset<> rows(sql.prepare << "select val from soci_test");
    int cnt = 0;
    for (soci::rowset<>::iterator it = rows.begin(), end = rows.end();
         it != end; ++it)
    {
        cnt++;
    }
    CHECK(cnt == 1);
}

TEST_CASE("MySQL function call", "[mysql][function]")
{
    soci::session sql(backEnd, connectString);

    row r;

    sql << "set @day = '5'";
    sql << "set @mm = 'december'";
    sql << "set @year = '2012'";
    sql << "select concat(@day,' ',@mm,' ',@year)", into(r);
}

struct double_value_table_creator : table_creator_base
{
    double_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val double)";
    }
};

TEST_CASE("MySQL special floating point values", "[mysql][float]")
{
    static bool is_iec559 = std::numeric_limits<double>::is_iec559;
    if (!is_iec559)
    {
        WARN("C++ double type is not IEC-559, skipping test.");
        return;
    }

  const std::string expectedError =
      "Use element used with infinity or NaN, which are "
      "not supported by the MySQL server.";
  {
    soci::session sql(backEnd, connectString);

    double x = std::numeric_limits<double>::quiet_NaN();
    statement st = (sql.prepare << "SELECT :x", use(x, "x"));
    try {
        st.execute(true);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);

    double x = std::numeric_limits<double>::infinity();
    statement st = (sql.prepare << "SELECT :x", use(x, "x"));
    try {
        st.execute(true);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);
    double_value_table_creator tableCreator(sql);

    std::vector<double> v(1, std::numeric_limits<double>::quiet_NaN());
    try {
        sql << "insert into soci_test (val) values (:val)", use(v);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);
    double_value_table_creator tableCreator(sql);

    std::vector<double> v(1, std::numeric_limits<double>::infinity());
    try {
        sql << "insert into soci_test (val) values (:val)", use(v);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
}

struct tinyint_value_table_creator : table_creator_base
{
    tinyint_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val tinyint)";
    }
};

struct tinyint_unsigned_value_table_creator : table_creator_base
{
    tinyint_unsigned_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val tinyint unsigned)";
    }
};

TEST_CASE("MySQL tinyint", "[mysql][int][tinyint]")
{
  {
    soci::session sql(backEnd, connectString);
    unsigned_value_table_creator tableCreator(sql);
    unsigned int mask = 0xffffff00;
    sql << "insert into soci_test set val = " << mask;
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_long_long);
    CHECK(r.get<long long>("val") == 0xffffff00);
    CHECK(r.get<unsigned>("val") == 0xffffff00);
  }
  {
    soci::session sql(backEnd, connectString);
    tinyint_value_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = -123";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_integer);
    CHECK(r.get<int>("val") == -123);
  }
  {
    soci::session sql(backEnd, connectString);
    tinyint_unsigned_value_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = 123";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_integer);
    CHECK(r.get<int>("val") == 123);
  }
  {
    soci::session sql(backEnd, connectString);
    bigint_unsigned_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = 123456789012345";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_unsigned_long_long);
    CHECK(r.get<unsigned long long>("val") == 123456789012345ULL);
  }
  {
    soci::session sql(backEnd, connectString);
    bigint_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = -123456789012345";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_long_long);
    CHECK(r.get<long long>("val") == -123456789012345LL);
  }
}

struct strings_table_creator : table_creator_base
{
    strings_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(s1 char(20), s2 varchar(20), "
            "s3 tinytext, s4 mediumtext, s5 text, s6 longtext, "
            "b1 binary(20), b2 varbinary(20), b3 tinyblob, b4 mediumblob, "
            "b5 blob, b6 longblob, e1 enum ('foo', 'bar', 'baz'))";
    }
};

TEST_CASE("MySQL strings", "[mysql][string]")
{
    soci::session sql(backEnd, connectString);
    strings_table_creator tableCreator(sql);
    std::string text = "Ala ma kota.";
    std::string binary("Ala\0ma\0kota.........", 20);
    sql << "insert into soci_test "
        "(s1, s2, s3, s4, s5, s6, b1, b2, b3, b4, b5, b6, e1) values "
        "(:s1, :s2, :s3, :s4, :d5, :s6, :b1, :b2, :b3, :b4, :b5, :b6, "
        "\'foo\')",
        use(text), use(text), use(text), use(text), use(text), use(text),
        use(binary), use(binary), use(binary), use(binary), use(binary),
        use(binary);
    row r;
    sql << "select s1, s2, s3, s4, s5, s6, b1, b2, b3, b4, b5, b6, e1 "
        "from soci_test", into(r);
    REQUIRE(r.size() == 13);
    for (int i = 0; i < 13; i++) {
        CHECK(r.get_properties(i).get_data_type() == dt_string);
        if (i < 6) {
            CHECK(r.get<std::string>(i) == text);
        } else if (i < 12) {
            CHECK(r.get<std::string>(i) == binary);
        } else {
            CHECK(r.get<std::string>(i) == "foo");
        }
    }
}

struct table_creator_for_get_last_insert_id : table_creator_base
{
    table_creator_for_get_last_insert_id(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer not null auto_increment, "
            "primary key (id))";
        sql << "alter table soci_test auto_increment = 42";
    }
};

TEST_CASE("MySQL last insert id", "[mysql][last-insert-id]")
{
    soci::session sql(backEnd, connectString);
    table_creator_for_get_last_insert_id tableCreator(sql);
    sql << "insert into soci_test () values ()";
    long id;
    bool result = sql.get_last_insert_id("soci_test", id);
    CHECK(result == true);
    CHECK(id == 42);
}

std::string escape_string(soci::session& sql, const std::string& s)
{
    mysql_session_backend* backend = static_cast<mysql_session_backend*>(
        sql.get_backend());
    char* escaped = new char[2 * s.size() + 1];
    mysql_real_escape_string(backend->conn_, escaped, s.data(), static_cast<unsigned long>(s.size()));
    std::string retv = escaped;
    delete [] escaped;
    return retv;
}

void test14()
{
    {
        soci::session sql(backEnd, connectString);
        strings_table_creator tableCreator(sql);
        std::string s = "word1'word2:word3";
        std::string escaped = escape_string(sql, s);
        std::string query = "insert into soci_test (s5) values ('";
        query.append(escaped);
        query.append("')");
        sql << query;
        std::string s2;
        sql << "select s5 from soci_test", into(s2);
        CHECK(s == s2);
    }

    std::cout << "test 14 passed" << std::endl;
}

void test15()
{
    {
        soci::session sql(backEnd, connectString);
        int n;
        sql << "select @a := 123", into(n);
        CHECK(n == 123);
    }

    std::cout << "test 15 passed" << std::endl;
}

int main(int argc, char** argv)
{
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
            << " \"dbname=test user=root password=\'Ala ma kota\'\"\n";
        std::exit(1);
    }

    test_context tc(backEnd, connectString);

    return Catch::Session().run(argc, argv);
}
