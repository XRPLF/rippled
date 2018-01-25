//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_COMMON_TESTS_H_INCLUDED
#define SOCI_COMMON_TESTS_H_INCLUDED

#include "soci/soci.h"

#ifdef SOCI_HAVE_BOOST
// explicitly pull conversions for Boost's optional, tuple and fusion:
#include <boost/version.hpp>
#include "soci/boost-optional.h"
#include "soci/boost-tuple.h"
#include "soci/boost-gregorian-date.h"
#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500
#include "soci/boost-fusion.h"
#endif // BOOST_VERSION
#endif // SOCI_HAVE_BOOST

#include "soci-compiler.h"

#define CATCH_CONFIG_RUNNER
#include <catch.hpp>

#if defined(_MSC_VER) && (_MSC_VER < 1500)
#undef SECTION
#define SECTION(name) INTERNAL_CATCH_SECTION(name, "dummy-for-vc8")
#endif

#include <algorithm>
#include <cassert>
#include <clocale>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <typeinfo>

// Although SQL standard mandates right padding CHAR(N) values to their length
// with spaces, some backends don't confirm to it:
//
//  - Firebird does pad the string but to the byte-size (not character size) of
//  the column (i.e. CHAR(10) NONE is padded to 10 bytes but CHAR(10) UTF8 --
//  to 40).
//  - For MySql PAD_CHAR_TO_FULL_LENGTH option must be set, otherwise the value
//  is trimmed.
//  - SQLite never behaves correctly at all.
//
// This method will check result string from column defined as fixed char It
// will check only bytes up to the original string size. If padded string is
// bigger than expected string then all remaining chars must be spaces so if
// any non-space character is found it will fail.
void
checkEqualPadded(const std::string& padded_str, const std::string& expected_str)
{
    size_t const len = expected_str.length();
    std::string const start_str(padded_str, 0, len);

    if (start_str != expected_str)
    {
        throw soci::soci_error(
                "Expected string \"" + expected_str + "\" "
                "is different from the padded string \"" + padded_str + "\""
              );
    }

    if (padded_str.length() > len)
    {
        std::string const end_str(padded_str, len);
        if (end_str != std::string(padded_str.length() - len, ' '))
        {
            throw soci::soci_error(
                  "\"" + padded_str + "\" starts with \"" + padded_str +
                  "\" but non-space characater(s) are found aftewards"
                );
        }
    }
}

#define CHECK_EQUAL_PADDED(padded_str, expected_str) \
    CHECK_NOTHROW(checkEqualPadded(padded_str, expected_str));

// Objects used later in tests 14,15
struct PhonebookEntry
{
    std::string name;
    std::string phone;
};

struct PhonebookEntry2 : public PhonebookEntry
{
};

class PhonebookEntry3
{
public:
    void setName(std::string const & n) { name_ = n; }
    std::string getName() const { return name_; }

    void setPhone(std::string const & p) { phone_ = p; }
    std::string getPhone() const { return phone_; }

public:
    std::string name_;
    std::string phone_;
};

// user-defined object for test26 and test28
class MyInt
{
public:
    MyInt() : i_() {}
    MyInt(int i) : i_(i) {}
    void set(int i) { i_ = i; }
    int get() const { return i_; }
private:
    int i_;
};

namespace soci
{

// basic type conversion for user-defined type with single base value
template<> struct type_conversion<MyInt>
{
    typedef int base_type;

    static void from_base(int i, indicator ind, MyInt &mi)
    {
        if (ind == i_ok)
        {
            mi.set(i);
        }
    }

    static void to_base(MyInt const &mi, int &i, indicator &ind)
    {
        i = mi.get();
        ind = i_ok;
    }
};

// basic type conversion on many values (ORM)
template<> struct type_conversion<PhonebookEntry>
{
    typedef soci::values base_type;

    static void from_base(values const &v, indicator /* ind */, PhonebookEntry &pe)
    {
        // here we ignore the possibility the the whole object might be NULL
        pe.name = v.get<std::string>("NAME");
        pe.phone = v.get<std::string>("PHONE", "<NULL>");
    }

    static void to_base(PhonebookEntry const &pe, values &v, indicator &ind)
    {
        v.set("NAME", pe.name);
        v.set("PHONE", pe.phone, pe.phone.empty() ? i_null : i_ok);
        ind = i_ok;
    }
};

// type conversion which directly calls values::get_indicator()
template<> struct type_conversion<PhonebookEntry2>
{
    typedef soci::values base_type;

    static void from_base(values const &v, indicator /* ind */, PhonebookEntry2 &pe)
    {
        // here we ignore the possibility the the whole object might be NULL

        pe.name = v.get<std::string>("NAME");
        indicator ind = v.get_indicator("PHONE"); //another way to test for null
        pe.phone = ind == i_null ? "<NULL>" : v.get<std::string>("PHONE");
    }

    static void to_base(PhonebookEntry2 const &pe, values &v, indicator &ind)
    {
        v.set("NAME", pe.name);
        v.set("PHONE", pe.phone, pe.phone.empty() ? i_null : i_ok);
        ind = i_ok;
    }
};

template<> struct type_conversion<PhonebookEntry3>
{
    typedef soci::values base_type;

    static void from_base(values const &v, indicator /* ind */, PhonebookEntry3 &pe)
    {
        // here we ignore the possibility the the whole object might be NULL

        pe.setName(v.get<std::string>("NAME"));
        pe.setPhone(v.get<std::string>("PHONE", "<NULL>"));
    }

    static void to_base(PhonebookEntry3 const &pe, values &v, indicator &ind)
    {
        v.set("NAME", pe.getName());
        v.set("PHONE", pe.getPhone(), pe.getPhone().empty() ? i_null : i_ok);
        ind = i_ok;
    }
};

} // namespace soci

namespace soci
{
namespace tests
{

// TODO: improve cleanup capabilities by subtypes, soci_test name may be omitted --mloskot
//       i.e. optional ctor param accepting custom table name
class table_creator_base
{
public:
    table_creator_base(session& sql)
        : msession(sql) { drop(); }

    virtual ~table_creator_base() { drop();}
private:
    void drop()
    {
        try
        {
            msession << "drop table soci_test";
        }
        catch (soci_error const& e)
        {
            //std::cerr << e.what() << std::endl;
            e.what();
        }
    }
    session& msession;

    SOCI_NOT_COPYABLE(table_creator_base)
};

class procedure_creator_base
{
public:
    procedure_creator_base(session& sql)
        : msession(sql) { drop(); }

    virtual ~procedure_creator_base() { drop();}
private:
    void drop()
    {
        try { msession << "drop procedure soci_test"; } catch (soci_error&) {}
    }
    session& msession;

    SOCI_NOT_COPYABLE(procedure_creator_base)
};

class function_creator_base
{
public:
    function_creator_base(session& sql)
        : msession(sql) { drop(); }

    virtual ~function_creator_base() { drop();}

protected:
    virtual std::string dropstatement()
    {
        return "drop function soci_test";
    }

private:
    void drop()
    {
        try { msession << dropstatement(); } catch (soci_error&) {}
    }
    session& msession;

    SOCI_NOT_COPYABLE(function_creator_base)
};

// This is a singleton class, at any given time there is at most one test
// context alive and common_tests fixture class uses it.
class test_context_base
{
public:
    test_context_base(backend_factory const &backEnd,
                    std::string const &connectString)
        : backEndFactory_(backEnd),
          connectString_(connectString)
    {
        // This can't be a CHECK() because the test context is constructed
        // outside of any test.
        assert(!the_test_context_);

        the_test_context_ = this;

        // To allow running tests in non-default ("C") locale, the following
        // environment variable can be set and then the current default locale
        // (which can itself be changed by setting LC_ALL environment variable)
        // will then be used.
        if (std::getenv("SOCI_TEST_USE_LC_ALL"))
            std::setlocale(LC_ALL, "");
    }

    static test_context_base const& get_instance()
    {
        REQUIRE(the_test_context_);

        return *the_test_context_;
    }

    backend_factory const & get_backend_factory() const
    {
        return backEndFactory_;
    }

    std::string get_connect_string() const
    {
        return connectString_;
    }

    virtual std::string to_date_time(std::string const &dateTime) const = 0;

    virtual table_creator_base* table_creator_1(session&) const = 0;
    virtual table_creator_base* table_creator_2(session&) const = 0;
    virtual table_creator_base* table_creator_3(session&) const = 0;
    virtual table_creator_base* table_creator_4(session&) const = 0;

    // Override this to return the table creator for a simple table containing
    // an integer "id" column and CLOB "s" one.
    //
    // Returns null by default to indicate that CLOB is not supported.
    virtual table_creator_base* table_creator_clob(session&) const { return NULL; }

    // Override this to return the table creator for a simple table containing
    // an integer "id" column and XML "x" one.
    //
    // Returns null by default to indicate that XML is not supported.
    virtual table_creator_base* table_creator_xml(session&) const { return NULL; }

    // Return the casts that must be used to convert the between the database
    // XML type and the query parameters.
    //
    // By default no special casts are done.
    virtual std::string to_xml(std::string const& x) const { return x; }
    virtual std::string from_xml(std::string const& x) const { return x; }

    // Override this if the backend not only supports working with XML values
    // (and so returns a non-null value from table_creator_xml()), but the
    // database itself has real XML support instead of just allowing to store
    // and retrieve XML as text. "Real" support means at least preventing the
    // application from storing malformed XML in the database.
    virtual bool has_real_xml_support() const { return false; }

    // Override this if the backend doesn't handle floating point values
    // correctly, i.e. writing a value and reading it back doesn't return
    // *exactly* the same value.
    virtual bool has_fp_bug() const { return false; }

    // Override this if the backend doesn't handle multiple active select
    // statements at the same time, i.e. a result set must be entirely consumed
    // before creating a new one (this is the case of MS SQL without MARS).
    virtual bool has_multiple_select_bug() const { return false; }

    // Override this if the backend may not have transactions support.
    virtual bool has_transactions_support(session&) const { return true; }

    // Override this if the backend silently truncates string values too long
    // to fit by default.
    virtual bool has_silent_truncate_bug(session&) const { return false; }

    // Override this to call commit() if it's necessary for the DDL statements
    // to be taken into account (currently this is only the case for Firebird).
    virtual void on_after_ddl(session&) const { }

    // Put the database in SQL-complient mode for CHAR(N) values, return false
    // if it's impossible, i.e. if the database doesn't behave correctly
    // whatever we do.
    virtual bool enable_std_char_padding(session&) const { return true; }

    // Return the SQL expression giving the length of the specified string,
    // i.e. "char_length(s)" in standard SQL but often "len(s)" or "length(s)"
    // in practice and sometimes even worse (thanks Oracle).
    virtual std::string sql_length(std::string const& s) const = 0;

    virtual ~test_context_base()
    {
        the_test_context_ = NULL;
    }

private:
    backend_factory const &backEndFactory_;
    std::string const connectString_;

    static test_context_base* the_test_context_;

    SOCI_NOT_COPYABLE(test_context_base)
};

// Currently all tests consist of just a single source file, so we can define
// this member here because this header is included exactly once.
tests::test_context_base* tests::test_context_base::the_test_context_ = NULL;


// Compare doubles for approximate equality. This has to be used everywhere
// where we write "3.14" (or "6.28") to the database as a string and then
// compare the value read back with the literal 3.14 floating point constant
// because they are not the same.
//
// It is also used for the backends which currently don't handle doubles
// correctly.
//
// Notice that this function is normally not used directly but rather from the
// macro below.
inline bool are_doubles_approx_equal(double const a, double const b)
{
    // The formula taken from CATCH test framework
    // https://github.com/philsquared/Catch/
    // Thanks to Richard Harris for his help refining this formula
    double const epsilon(std::numeric_limits<float>::epsilon() * 100);
    double const scale(1.0);
    return std::fabs(a - b) < epsilon * (scale + (std::max)(std::fabs(a), std::fabs(b)));
}

// This is a macro to ensure we use the correct line numbers. The weird
// do/while construction is used to make this a statement and the even weirder
// condition in while ensures that the loop is executed exactly once without
// triggering warnings from MSVC about the condition being always false.
#define ASSERT_EQUAL_APPROX(a, b) \
    do { \
      if (!are_doubles_approx_equal((a), (b))) { \
        FAIL( "Approximate equality check failed: " \
                  << std::fixed \
                  << std::setprecision(std::numeric_limits<double>::digits10 + 1) \
                  << (a) << " != " << (b) ); \
      } \
    } while ( (void)0, 0 )


// Exact double comparison function. We need one, instead of writing "a == b",
// only in order to have some place to put the pragmas disabling gcc warnings.
inline bool
are_doubles_exactly_equal(double a, double b)
{
    // Avoid g++ warnings: we do really want the exact equality here.
    GCC_WARNING_SUPPRESS(float-equal)

    return a == b;

    GCC_WARNING_RESTORE(float-equal)
}

#define ASSERT_EQUAL_EXACT(a, b) \
    do { \
      if (!are_doubles_exactly_equal((a), (b))) { \
        FAIL( "Exact equality check failed: " \
                  << std::fixed \
                  << std::setprecision(std::numeric_limits<double>::digits10 + 1) \
                  << (a) << " != " << (b) ); \
      } \
    } while ( (void)0, 0 )


// Compare two floating point numbers either exactly or approximately depending
// on test_context::has_fp_bug() return value.
inline bool
are_doubles_equal(test_context_base const& tc, double a, double b)
{
    return tc.has_fp_bug()
                ? are_doubles_approx_equal(a, b)
                : are_doubles_exactly_equal(a, b);
}

// This macro should be used when where we don't have any problems with string
// literals vs floating point literals mismatches described above and would
// ideally compare the numbers exactly but, unfortunately, currently can't do
// this unconditionally because at least some backends are currently buggy and
// don't handle the floating point values correctly.
//
// This can be only used from inside the common_tests class as it relies on
// having an accessible "tc_" variable to determine whether exact or
// approximate comparison should be used.
#define ASSERT_EQUAL(a, b) \
    do { \
      if (!are_doubles_equal(tc_, (a), (b))) { \
        FAIL( "Equality check failed: " \
                  << std::fixed \
                  << std::setprecision(std::numeric_limits<double>::digits10 + 1) \
                  << (a) << " != " << (b) ); \
      } \
    } while ( (void)0, 0 )


class common_tests
{
public:
    common_tests()
    : tc_(test_context_base::get_instance()),
      backEndFactory_(tc_.get_backend_factory()),
      connectString_(tc_.get_connect_string())
    {}

protected:
    test_context_base const & tc_;
    backend_factory const &backEndFactory_;
    std::string const connectString_;

    SOCI_NOT_COPYABLE(common_tests)
};

typedef cxx_details::auto_ptr<table_creator_base> auto_table_creator;

// Define the test cases in their own namespace to avoid clashes with the test
// cases defined in individual backend tests: as only line number is used for
// building the name of the "anonymous" function by the TEST_CASE macro, we
// could have a conflict between a test defined here and in some backend if
// they happened to start on the same line.
namespace test_cases
{

TEST_CASE_METHOD(common_tests, "Exception on not connected", "[core][exception]")
{
    soci::session sql; // no connection

    // ensure connection is checked, no crash occurs
    CHECK_THROWS_AS(sql.begin(), soci_error);
    CHECK_THROWS_AS(sql.commit(), soci_error);
    CHECK_THROWS_AS(sql.rollback(), soci_error);
    CHECK_THROWS_AS(sql.get_backend_name(), soci_error);
    CHECK_THROWS_AS(sql.make_statement_backend(), soci_error);
    CHECK_THROWS_AS(sql.make_rowid_backend(), soci_error);
    CHECK_THROWS_AS(sql.make_blob_backend(), soci_error);

    std::string s;
    long l;
    CHECK_THROWS_AS(sql.get_next_sequence_value(s, l), soci_error);
    CHECK_THROWS_AS(sql.get_last_insert_id(s, l), soci_error);
}

TEST_CASE_METHOD(common_tests, "Basic functionality", "[core][basics]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    CHECK_THROWS_AS(sql << "drop table soci_test_nosuchtable", soci_error);

    sql << "insert into soci_test (id) values (" << 123 << ")";
    int id;
    sql << "select id from soci_test", into(id);
    CHECK(id == 123);

    sql << "insert into soci_test (id) values (" << 234 << ")";
    sql << "insert into soci_test (id) values (" << 345 << ")";
    // Test prepare, execute, fetch correctness
    statement st = (sql.prepare << "select id from soci_test", into(id));
    st.execute();
    int count = 0;
    while(st.fetch())
        count++;
    CHECK(count == 3 );
    bool fetchEnd = st.fetch(); // All the data has been read here so additional fetch must return false
    CHECK(fetchEnd == false);
}

// "into" tests, type conversions, etc.
TEST_CASE_METHOD(common_tests, "Use and into", "[core][into]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    SECTION("Round trip works for char")
    {
        char c('a');
        sql << "insert into soci_test(c) values(:c)", use(c);
        sql << "select c from soci_test", into(c);
        CHECK(c == 'a');
    }

    SECTION("Round trip works for string")
    {
        std::string helloSOCI("Hello, SOCI!");
        sql << "insert into soci_test(str) values(:s)", use(helloSOCI);
        std::string str;
        sql << "select str from soci_test", into(str);
        CHECK(str == "Hello, SOCI!");
    }

    SECTION("Round trip works for short")
    {
        short three(3);
        sql << "insert into soci_test(sh) values(:id)", use(three);
        short sh(0);
        sql << "select sh from soci_test", into(sh);
        CHECK(sh == 3);
    }

    SECTION("Round trip works for int")
    {
        int five(5);
        sql << "insert into soci_test(id) values(:id)", use(five);
        int i(0);
        sql << "select id from soci_test", into(i);
        CHECK(i == 5);
    }

    SECTION("Round trip works for unsigned long")
    {
        unsigned long seven(7);
        sql << "insert into soci_test(ul) values(:ul)", use(seven);
        unsigned long ul(0);
        sql << "select ul from soci_test", into(ul);
        CHECK(ul == 7);
    }

    SECTION("Round trip works for double")
    {
        double pi(3.14159265);
        sql << "insert into soci_test(d) values(:d)", use(pi);
        double d(0.0);
        sql << "select d from soci_test", into(d);
        ASSERT_EQUAL(d, pi);
    }

    SECTION("Round trip works for date without time")
    {
        std::tm nov15 = std::tm();
        nov15.tm_year = 105;
        nov15.tm_mon = 10;
        nov15.tm_mday = 15;
        nov15.tm_hour = 0;
        nov15.tm_min = 0;
        nov15.tm_sec = 0;

        sql << "insert into soci_test(tm) values(:tm)", use(nov15);

        std::tm t = std::tm();
        sql << "select tm from soci_test", into(t);
        CHECK(t.tm_year == 105);
        CHECK(t.tm_mon  == 10);
        CHECK(t.tm_mday == 15);
        CHECK(t.tm_hour == 0);
        CHECK(t.tm_min  == 0);
        CHECK(t.tm_sec  == 0);
    }

    SECTION("Round trip works for date with time")
    {
        std::tm nov15 = std::tm();
        nov15.tm_year = 105;
        nov15.tm_mon = 10;
        nov15.tm_mday = 15;
        nov15.tm_hour = 22;
        nov15.tm_min = 14;
        nov15.tm_sec = 17;

        sql << "insert into soci_test(tm) values(:tm)", use(nov15);

        std::tm t = std::tm();
        sql << "select tm from soci_test", into(t);
        CHECK(t.tm_year == 105);
        CHECK(t.tm_mon  == 10);
        CHECK(t.tm_mday == 15);
        CHECK(t.tm_hour == 22);
        CHECK(t.tm_min  == 14);
        CHECK(t.tm_sec  == 17);
    }

    SECTION("Indicator is filled correctly in the simplest case")
    {
        int id(1);
        std::string str("Hello");
        sql << "insert into soci_test(id, str) values(:id, :str)",
            use(id), use(str);

        int i;
        indicator ind;
        sql << "select id from soci_test", into(i, ind);
        CHECK(ind == i_ok);
    }

    SECTION("Indicators work correctly more generally")
    {
        sql << "insert into soci_test(id,tm) values(NULL,NULL)";
        int i;
        indicator ind;
        sql << "select id from soci_test", into(i, ind);
        CHECK(ind == i_null);

        // additional test for NULL with std::tm
        std::tm t = std::tm();
        sql << "select tm from soci_test", into(t, ind);
        CHECK(ind == i_null);

        try
        {
            // expect error
            sql << "select id from soci_test", into(i);
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(e.get_error_message() ==
                "Null value fetched and no indicator defined.");
        }

        sql << "select id from soci_test where id = 1000", into(i, ind);
        CHECK(sql.got_data() == false);

        // no data expected
        sql << "select id from soci_test where id = 1000", into(i);
        CHECK(sql.got_data() == false);

        // no data expected, test correct behaviour with use
        int id = 1000;
        sql << "select id from soci_test where id = :id", use(id), into(i);
        CHECK(sql.got_data() == false);
    }
}

// repeated fetch and bulk fetch
TEST_CASE_METHOD(common_tests, "Repeated and bulk fetch", "[core][bulk]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    SECTION("char")
    {
        char c;
        for (c = 'a'; c <= 'z'; ++c)
        {
            sql << "insert into soci_test(c) values(\'" << c << "\')";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 'z' - 'a' + 1);

        {
            char c2 = 'a';

            statement st = (sql.prepare <<
                "select c from soci_test order by c", into(c));

            st.execute();
            while (st.fetch())
            {
                CHECK(c == c2);
                ++c2;
            }
            CHECK(c2 == 'a' + count);
        }
        {
            char c2 = 'a';

            std::vector<char> vec(10);
            statement st = (sql.prepare <<
                "select c from soci_test order by c", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t i = 0; i != vec.size(); ++i)
                {
                    CHECK(c2 == vec[i]);
                    ++c2;
                }

                vec.resize(10);
            }
            CHECK(c2 == 'a' + count);
        }

        {
            // verify an exception is thrown when empty vector is used
            std::vector<char> vec;
            try
            {
                sql << "select c from soci_test", into(vec);
                FAIL("expected exception not thrown");
            }
            catch (soci_error const &e)
            {
                 CHECK(e.get_error_message() ==
                     "Vectors of size 0 are not allowed.");
            }
        }

    }

    // repeated fetch and bulk fetch of std::string
    SECTION("std::string")
    {
        int const rowsToTest = 10;
        for (int i = 0; i != rowsToTest; ++i)
        {
            std::ostringstream ss;
            ss << "Hello_" << i;

            sql << "insert into soci_test(str) values(\'"
                << ss.str() << "\')";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == rowsToTest);

        {
            int i = 0;
            std::string s;
            statement st = (sql.prepare <<
                "select str from soci_test order by str", into(s));

            st.execute();
            while (st.fetch())
            {
                std::ostringstream ss;
                ss << "Hello_" << i;
                CHECK(s == ss.str());
                ++i;
            }
            CHECK(i == rowsToTest);
        }
        {
            int i = 0;

            std::vector<std::string> vec(4);
            statement st = (sql.prepare <<
                "select str from soci_test order by str", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t j = 0; j != vec.size(); ++j)
                {
                    std::ostringstream ss;
                    ss << "Hello_" << i;
                    CHECK(ss.str() == vec[j]);
                    ++i;
                }

                vec.resize(4);
            }
            CHECK(i == rowsToTest);
        }
    }

    SECTION("short")
    {
        short const rowsToTest = 100;
        short sh;
        for (sh = 0; sh != rowsToTest; ++sh)
        {
            sql << "insert into soci_test(sh) values(" << sh << ")";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == rowsToTest);

        {
            short sh2 = 0;

            statement st = (sql.prepare <<
                "select sh from soci_test order by sh", into(sh));

            st.execute();
            while (st.fetch())
            {
                CHECK(sh == sh2);
                ++sh2;
            }
            CHECK(sh2 == rowsToTest);
        }
        {
            short sh2 = 0;

            std::vector<short> vec(8);
            statement st = (sql.prepare <<
                "select sh from soci_test order by sh", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t i = 0; i != vec.size(); ++i)
                {
                    CHECK(sh2 == vec[i]);
                    ++sh2;
                }

                vec.resize(8);
            }
            CHECK(sh2 == rowsToTest);
        }
    }

    SECTION("int")
    {
        int const rowsToTest = 100;
        int i;
        for (i = 0; i != rowsToTest; ++i)
        {
            sql << "insert into soci_test(id) values(" << i << ")";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == rowsToTest);

        {
            int i2 = 0;

            statement st = (sql.prepare <<
                "select id from soci_test order by id", into(i));

            st.execute();
            while (st.fetch())
            {
                CHECK(i == i2);
                ++i2;
            }
            CHECK(i2 == rowsToTest);
        }
        {
            // additional test with the use element

            int i2 = 0;
            int cond = 0; // this condition is always true

            statement st = (sql.prepare <<
                "select id from soci_test where id >= :cond order by id",
                use(cond), into(i));

            st.execute();
            while (st.fetch())
            {
                CHECK(i == i2);
                ++i2;
            }
            CHECK(i2 == rowsToTest);
        }
        {
            int i2 = 0;

            std::vector<int> vec(8);
            statement st = (sql.prepare <<
                "select id from soci_test order by id", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t n = 0; n != vec.size(); ++n)
                {
                    CHECK(i2 == vec[n]);
                    ++i2;
                }

                vec.resize(8);
            }
            CHECK(i2 == rowsToTest);
        }
    }

    SECTION("unsigned int")
    {
        unsigned int const rowsToTest = 100;
        unsigned int ul;
        for (ul = 0; ul != rowsToTest; ++ul)
        {
            sql << "insert into soci_test(ul) values(" << ul << ")";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == static_cast<int>(rowsToTest));

        {
            unsigned int ul2 = 0;

            statement st = (sql.prepare <<
                "select ul from soci_test order by ul", into(ul));

            st.execute();
            while (st.fetch())
            {
                CHECK(ul == ul2);
                ++ul2;
            }
            CHECK(ul2 == rowsToTest);
        }
        {
            unsigned int ul2 = 0;

            std::vector<unsigned int> vec(8);
            statement st = (sql.prepare <<
                "select ul from soci_test order by ul", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t i = 0; i != vec.size(); ++i)
                {
                    CHECK(ul2 == vec[i]);
                    ++ul2;
                }

                vec.resize(8);
            }
            CHECK(ul2 == rowsToTest);
        }
    }

    SECTION("double")
    {
        int const rowsToTest = 100;
        double d = 0.0;

        statement sti = (sql.prepare <<
            "insert into soci_test(d) values(:d)", use(d));
        for (int i = 0; i != rowsToTest; ++i)
        {
            sti.execute(true);
            d += 0.6;
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == rowsToTest);

        {
            double d2 = 0.0;
            int i = 0;

            statement st = (sql.prepare <<
                "select d from soci_test order by d", into(d));

            st.execute();
            while (st.fetch())
            {
                ASSERT_EQUAL(d, d2);
                d2 += 0.6;
                ++i;
            }
            CHECK(i == rowsToTest);
        }
        {
            double d2 = 0.0;
            int i = 0;

            std::vector<double> vec(8);
            statement st = (sql.prepare <<
                "select d from soci_test order by d", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t j = 0; j != vec.size(); ++j)
                {
                    ASSERT_EQUAL(d2, vec[j]);
                    d2 += 0.6;
                    ++i;
                }

                vec.resize(8);
            }
            CHECK(i == rowsToTest);
        }
    }

    SECTION("std::tm")
    {
        int const rowsToTest = 8;
        for (int i = 0; i != rowsToTest; ++i)
        {
            std::ostringstream ss;
            ss << 2000 + i << "-0" << 1 + i << '-' << 20 - i << ' '
                << 15 + i << ':' << 50 - i << ':' << 40 + i;

            sql << "insert into soci_test(id, tm) values(" << i
            << ", " << tc_.to_date_time(ss.str()) << ")";
        }

        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == rowsToTest);

        {
            std::tm t = std::tm();
            int i = 0;

            statement st = (sql.prepare <<
                "select tm from soci_test order by id", into(t));

            st.execute();
            while (st.fetch())
            {
                CHECK(t.tm_year == 2000 - 1900 + i);
                CHECK(t.tm_mon == i);
                CHECK(t.tm_mday == 20 - i);
                CHECK(t.tm_hour == 15 + i);
                CHECK(t.tm_min == 50 - i);
                CHECK(t.tm_sec == 40 + i);

                ++i;
            }
            CHECK(i == rowsToTest);
        }
        {
            int i = 0;

            std::vector<std::tm> vec(3);
            statement st = (sql.prepare <<
                "select tm from soci_test order by id", into(vec));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t j = 0; j != vec.size(); ++j)
                {
                    CHECK(vec[j].tm_year == 2000 - 1900 + i);
                    CHECK(vec[j].tm_mon == i);
                    CHECK(vec[j].tm_mday == 20 - i);
                    CHECK(vec[j].tm_hour == 15 + i);
                    CHECK(vec[j].tm_min == 50 - i);
                    CHECK(vec[j].tm_sec == 40 + i);

                    ++i;
                }

                vec.resize(3);
            }
            CHECK(i == rowsToTest);
        }
    }
}

// test for indicators (repeated fetch and bulk)
TEST_CASE_METHOD(common_tests, "Indicators", "[core][indicator]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(id, val) values(1, 10)";
        sql << "insert into soci_test(id, val) values(2, 11)";
        sql << "insert into soci_test(id, val) values(3, NULL)";
        sql << "insert into soci_test(id, val) values(4, NULL)";
        sql << "insert into soci_test(id, val) values(5, 12)";

        {
            int val;
            indicator ind;

            statement st = (sql.prepare <<
                "select val from soci_test order by id", into(val, ind));

            st.execute();
            bool gotData = st.fetch();
            CHECK(gotData);
            CHECK(ind == i_ok);
            CHECK(val == 10);
            gotData = st.fetch();
            CHECK(gotData);
            CHECK(ind == i_ok);
            CHECK(val == 11);
            gotData = st.fetch();
            CHECK(gotData);
            CHECK(ind == i_null);
            gotData = st.fetch();
            CHECK(gotData);
            CHECK(ind == i_null);
            gotData = st.fetch();
            CHECK(gotData);
            CHECK(ind == i_ok);
            CHECK(val == 12);
            gotData = st.fetch();
            CHECK(gotData == false);
        }
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            statement st = (sql.prepare <<
                "select val from soci_test order by id", into(vals, inds));

            st.execute();
            bool gotData = st.fetch();
            CHECK(gotData);
            CHECK(vals.size() == 3);
            CHECK(inds.size() == 3);
            CHECK(inds[0] == i_ok);
            CHECK(vals[0] == 10);
            CHECK(inds[1] == i_ok);
            CHECK(vals[1] == 11);
            CHECK(inds[2] == i_null);
            gotData = st.fetch();
            CHECK(gotData);
            CHECK(vals.size() == 2);
            CHECK(inds[0] == i_null);
            CHECK(inds[1] == i_ok);
            CHECK(vals[1] == 12);
            gotData = st.fetch();
            CHECK(gotData == false);
        }

        // additional test for "no data" condition
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            statement st = (sql.prepare <<
                "select val from soci_test where 0 = 1", into(vals, inds));

            bool gotData = st.execute(true);
            CHECK(gotData == false);

            // for convenience, vectors should be truncated
            CHECK(vals.empty());
            CHECK(inds.empty());

            // for even more convenience, fetch should not fail
            // but just report end of rowset
            // (and vectors should be truncated)

            vals.resize(1);
            inds.resize(1);

            gotData = st.fetch();
            CHECK(gotData == false);
            CHECK(vals.empty());
            CHECK(inds.empty());
        }

        // additional test for "no data" without prepared statement
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            sql << "select val from soci_test where 0 = 1",
                into(vals, inds);

            // vectors should be truncated
            CHECK(vals.empty());
            CHECK(inds.empty());
        }
    }

}

// test for different sizes of data vector and indicators vector
// (library should force ind. vector to have same size as data vector)
TEST_CASE_METHOD(common_tests, "Indicators vector", "[core][indicator][vector]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(id, str, val) values(1, 'ten', 10)";
        sql << "insert into soci_test(id, str, val) values(2, 'elf', 11)";
        sql << "insert into soci_test(id, str, val) values(3, NULL, NULL)";
        sql << "insert into soci_test(id, str, val) values(4, NULL, NULL)";
        sql << "insert into soci_test(id, str, val) values(5, 'xii', 12)";

        {
            std::vector<int> vals(4);
            std::vector<indicator> inds;

            statement st = (sql.prepare <<
                "select val from soci_test order by id", into(vals, inds));

            st.execute();
            st.fetch();
            CHECK(vals.size() == 4);
            CHECK(inds.size() == 4);
            vals.resize(3);
            st.fetch();
            CHECK(vals.size() == 1);
            CHECK(inds.size() == 1);

            std::vector<std::string> strs(5);
            sql << "select str from soci_test order by id", into(strs, inds);
            REQUIRE(inds.size() == 5);
            CHECK(inds[0] == i_ok);
            CHECK(inds[1] == i_ok);
            CHECK(inds[2] == i_null);
            CHECK(inds[3] == i_null);
            CHECK(inds[4] == i_ok);
        }
    }

}

// Note: this functionality is not available with older PostgreSQL
#ifndef SOCI_POSTGRESQL_NOPARAMS

// "use" tests, type conversions, etc.
TEST_CASE_METHOD(common_tests, "Use type conversion", "[core][use]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    SECTION("char")
    {
        char c('a');
        sql << "insert into soci_test(c) values(:c)", use(c);

        c = 'b';
        sql << "select c from soci_test", into(c);
        CHECK(c == 'a');

    }

    SECTION("std::string")
    {
        std::string s = "Hello SOCI!";
        sql << "insert into soci_test(str) values(:s)", use(s);

        std::string str;
        sql << "select str from soci_test", into(str);

        CHECK(str == "Hello SOCI!");
    }

    SECTION("short")
    {
        short s = 123;
        sql << "insert into soci_test(id) values(:id)", use(s);

        short s2 = 0;
        sql << "select id from soci_test", into(s2);

        CHECK(s2 == 123);
    }

    SECTION("int")
    {
        int i = -12345678;
        sql << "insert into soci_test(id) values(:i)", use(i);

        int i2 = 0;
        sql << "select id from soci_test", into(i2);

        CHECK(i2 == -12345678);
    }

    SECTION("unsigned long")
    {
        unsigned long ul = 4000000000ul;
        sql << "insert into soci_test(ul) values(:num)", use(ul);

        unsigned long ul2 = 0;
        sql << "select ul from soci_test", into(ul2);

        CHECK(ul2 == 4000000000ul);
    }

    SECTION("double")
    {
        double d = 3.14159265;
        sql << "insert into soci_test(d) values(:d)", use(d);

        double d2 = 0;
        sql << "select d from soci_test", into(d2);

        ASSERT_EQUAL(d2, d);
    }

    SECTION("std::tm")
    {
        std::tm t = std::tm();
        t.tm_year = 105;
        t.tm_mon = 10;
        t.tm_mday = 19;
        t.tm_hour = 21;
        t.tm_min = 39;
        t.tm_sec = 57;
        sql << "insert into soci_test(tm) values(:t)", use(t);

        std::tm t2 = std::tm();
        t2.tm_year = 0;
        t2.tm_mon = 0;
        t2.tm_mday = 0;
        t2.tm_hour = 0;
        t2.tm_min = 0;
        t2.tm_sec = 0;

        sql << "select tm from soci_test", into(t2);

        CHECK(t.tm_year == 105);
        CHECK(t.tm_mon  == 10);
        CHECK(t.tm_mday == 19);
        CHECK(t.tm_hour == 21);
        CHECK(t.tm_min  == 39);
        CHECK(t.tm_sec  == 57);
    }

    SECTION("repeated use")
    {
        int i;
        statement st = (sql.prepare
            << "insert into soci_test(id) values(:id)", use(i));

        i = 5;
        st.execute(true);
        i = 6;
        st.execute(true);
        i = 7;
        st.execute(true);

        std::vector<int> v(5);
        sql << "select id from soci_test order by id", into(v);

        CHECK(v.size() == 3);
        CHECK(v[0] == 5);
        CHECK(v[1] == 6);
        CHECK(v[2] == 7);
    }

    // tests for use of const objects

    SECTION("const char")
    {
        char const c('a');
        sql << "insert into soci_test(c) values(:c)", use(c);

        char c2 = 'b';
        sql << "select c from soci_test", into(c2);
        CHECK(c2 == 'a');

    }

    SECTION("const std::string")
    {
        std::string const s = "Hello const SOCI!";
        sql << "insert into soci_test(str) values(:s)", use(s);

        std::string str;
        sql << "select str from soci_test", into(str);

        CHECK(str == "Hello const SOCI!");
    }

    SECTION("const short")
    {
        short const s = 123;
        sql << "insert into soci_test(id) values(:id)", use(s);

        short s2 = 0;
        sql << "select id from soci_test", into(s2);

        CHECK(s2 == 123);
    }

    SECTION("const int")
    {
        int const i = -12345678;
        sql << "insert into soci_test(id) values(:i)", use(i);

        int i2 = 0;
        sql << "select id from soci_test", into(i2);

        CHECK(i2 == -12345678);
    }

    SECTION("const unsigned long")
    {
        unsigned long const ul = 4000000000ul;
        sql << "insert into soci_test(ul) values(:num)", use(ul);

        unsigned long ul2 = 0;
        sql << "select ul from soci_test", into(ul2);

        CHECK(ul2 == 4000000000ul);
    }

    SECTION("const double")
    {
        double const d = 3.14159265;
        sql << "insert into soci_test(d) values(:d)", use(d);

        double d2 = 0;
        sql << "select d from soci_test", into(d2);

        ASSERT_EQUAL(d2, d);
    }

    SECTION("const std::tm")
    {
        std::tm t = std::tm();
        t.tm_year = 105;
        t.tm_mon = 10;
        t.tm_mday = 19;
        t.tm_hour = 21;
        t.tm_min = 39;
        t.tm_sec = 57;
        std::tm const & ct = t;
        sql << "insert into soci_test(tm) values(:t)", use(ct);

        std::tm t2 = std::tm();
        t2.tm_year = 0;
        t2.tm_mon = 0;
        t2.tm_mday = 0;
        t2.tm_hour = 0;
        t2.tm_min = 0;
        t2.tm_sec = 0;

        sql << "select tm from soci_test", into(t2);

        CHECK(t.tm_year == 105);
        CHECK(t.tm_mon  == 10);
        CHECK(t.tm_mday == 19);
        CHECK(t.tm_hour == 21);
        CHECK(t.tm_min  == 39);
        CHECK(t.tm_sec  == 57);
    }
}

#endif // SOCI_POSTGRESQL_NOPARAMS

// test for multiple use (and into) elements
TEST_CASE_METHOD(common_tests, "Multiple use and into", "[core][use][into]")
{
    soci::session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    {
        int i1 = 5;
        int i2 = 6;
        int i3 = 7;

#ifndef SOCI_POSTGRESQL_NOPARAMS

        sql << "insert into soci_test(i1, i2, i3) values(:i1, :i2, :i3)",
            use(i1), use(i2), use(i3);

#else
        // Older PostgreSQL does not support use elements.

        sql << "insert into soci_test(i1, i2, i3) values(5, 6, 7)";

#endif // SOCI_POSTGRESQL_NOPARAMS

        i1 = 0;
        i2 = 0;
        i3 = 0;
        sql << "select i1, i2, i3 from soci_test",
            into(i1), into(i2), into(i3);

        CHECK(i1 == 5);
        CHECK(i2 == 6);
        CHECK(i3 == 7);

        // same for vectors
        sql << "delete from soci_test";

        i1 = 0;
        i2 = 0;
        i3 = 0;

#ifndef SOCI_POSTGRESQL_NOPARAMS

        statement st = (sql.prepare
            << "insert into soci_test(i1, i2, i3) values(:i1, :i2, :i3)",
            use(i1), use(i2), use(i3));

        i1 = 1;
        i2 = 2;
        i3 = 3;
        st.execute(true);
        i1 = 4;
        i2 = 5;
        i3 = 6;
        st.execute(true);
        i1 = 7;
        i2 = 8;
        i3 = 9;
        st.execute(true);

#else
        // Older PostgreSQL does not support use elements.

        sql << "insert into soci_test(i1, i2, i3) values(1, 2, 3)";
        sql << "insert into soci_test(i1, i2, i3) values(4, 5, 6)";
        sql << "insert into soci_test(i1, i2, i3) values(7, 8, 9)";

#endif // SOCI_POSTGRESQL_NOPARAMS

        std::vector<int> v1(5);
        std::vector<int> v2(5);
        std::vector<int> v3(5);

        sql << "select i1, i2, i3 from soci_test order by i1",
            into(v1), into(v2), into(v3);

        CHECK(v1.size() == 3);
        CHECK(v2.size() == 3);
        CHECK(v3.size() == 3);
        CHECK(v1[0] == 1);
        CHECK(v1[1] == 4);
        CHECK(v1[2] == 7);
        CHECK(v2[0] == 2);
        CHECK(v2[1] == 5);
        CHECK(v2[2] == 8);
        CHECK(v3[0] == 3);
        CHECK(v3[1] == 6);
        CHECK(v3[2] == 9);
    }
}

// Not supported with older PostgreSQL
#ifndef SOCI_POSTGRESQL_NOPARAMS

// use vector elements
TEST_CASE_METHOD(common_tests, "Use vector", "[core][use][vector]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    SECTION("char")
    {
        std::vector<char> v;
        v.push_back('a');
        v.push_back('b');
        v.push_back('c');
        v.push_back('d');

        sql << "insert into soci_test(c) values(:c)", use(v);

        std::vector<char> v2(4);

        sql << "select c from soci_test order by c", into(v2);
        CHECK(v2.size() == 4);
        CHECK(v2[0] == 'a');
        CHECK(v2[1] == 'b');
        CHECK(v2[2] == 'c');
        CHECK(v2[3] == 'd');
    }

    SECTION("std::string")
    {
        std::vector<std::string> v;
        v.push_back("ala");
        v.push_back("ma");
        v.push_back("kota");

        sql << "insert into soci_test(str) values(:s)", use(v);

        std::vector<std::string> v2(4);

        sql << "select str from soci_test order by str", into(v2);
        CHECK(v2.size() == 3);
        CHECK(v2[0] == "ala");
        CHECK(v2[1] == "kota");
        CHECK(v2[2] == "ma");
    }

    SECTION("short")
    {
        std::vector<short> v;
        v.push_back(-5);
        v.push_back(6);
        v.push_back(7);
        v.push_back(123);

        sql << "insert into soci_test(sh) values(:sh)", use(v);

        std::vector<short> v2(4);

        sql << "select sh from soci_test order by sh", into(v2);
        CHECK(v2.size() == 4);
        CHECK(v2[0] == -5);
        CHECK(v2[1] == 6);
        CHECK(v2[2] == 7);
        CHECK(v2[3] == 123);
    }

    SECTION("int")
    {
        std::vector<int> v;
        v.push_back(-2000000000);
        v.push_back(0);
        v.push_back(1);
        v.push_back(2000000000);

        sql << "insert into soci_test(id) values(:i)", use(v);

        std::vector<int> v2(4);

        sql << "select id from soci_test order by id", into(v2);
        CHECK(v2.size() == 4);
        CHECK(v2[0] == -2000000000);
        CHECK(v2[1] == 0);
        CHECK(v2[2] == 1);
        CHECK(v2[3] == 2000000000);
    }

    SECTION("unsigned int")
    {
        std::vector<unsigned int> v;
        v.push_back(0);
        v.push_back(1);
        v.push_back(123);
        v.push_back(1000);

        sql << "insert into soci_test(ul) values(:ul)", use(v);

        std::vector<unsigned int> v2(4);

        sql << "select ul from soci_test order by ul", into(v2);
        CHECK(v2.size() == 4);
        CHECK(v2[0] == 0);
        CHECK(v2[1] == 1);
        CHECK(v2[2] == 123);
        CHECK(v2[3] == 1000);
    }

    SECTION("double")
    {
        std::vector<double> v;
        v.push_back(0);
        v.push_back(-0.0001);
        v.push_back(0.0001);
        v.push_back(3.1415926);

        sql << "insert into soci_test(d) values(:d)", use(v);

        std::vector<double> v2(4);

        sql << "select d from soci_test order by d", into(v2);
        CHECK(v2.size() == 4);
        ASSERT_EQUAL(v2[0],-0.0001);
        ASSERT_EQUAL(v2[1], 0);
        ASSERT_EQUAL(v2[2], 0.0001);
        ASSERT_EQUAL(v2[3], 3.1415926);
    }

    SECTION("std::tm")
    {
        std::vector<std::tm> v;
        std::tm t = std::tm();
        t.tm_year = 105;
        t.tm_mon  = 10;
        t.tm_mday = 26;
        t.tm_hour = 22;
        t.tm_min  = 45;
        t.tm_sec  = 17;

        v.push_back(t);

        t.tm_sec = 37;
        v.push_back(t);

        t.tm_mday = 25;
        v.push_back(t);

        sql << "insert into soci_test(tm) values(:t)", use(v);

        std::vector<std::tm> v2(4);

        sql << "select tm from soci_test order by tm", into(v2);
        CHECK(v2.size() == 3);
        CHECK(v2[0].tm_year == 105);
        CHECK(v2[0].tm_mon  == 10);
        CHECK(v2[0].tm_mday == 25);
        CHECK(v2[0].tm_hour == 22);
        CHECK(v2[0].tm_min  == 45);
        CHECK(v2[0].tm_sec  == 37);
        CHECK(v2[1].tm_year == 105);
        CHECK(v2[1].tm_mon  == 10);
        CHECK(v2[1].tm_mday == 26);
        CHECK(v2[1].tm_hour == 22);
        CHECK(v2[1].tm_min  == 45);
        CHECK(v2[1].tm_sec  == 17);
        CHECK(v2[2].tm_year == 105);
        CHECK(v2[2].tm_mon  == 10);
        CHECK(v2[2].tm_mday == 26);
        CHECK(v2[2].tm_hour == 22);
        CHECK(v2[2].tm_min  == 45);
        CHECK(v2[2].tm_sec  == 37);
    }

    SECTION("const int")
    {
        std::vector<int> v;
        v.push_back(-2000000000);
        v.push_back(0);
        v.push_back(1);
        v.push_back(2000000000);

        std::vector<int> const & cv = v;

        sql << "insert into soci_test(id) values(:i)", use(cv);

        std::vector<int> v2(4);

        sql << "select id from soci_test order by id", into(v2);
        CHECK(v2.size() == 4);
        CHECK(v2[0] == -2000000000);
        CHECK(v2[1] == 0);
        CHECK(v2[2] == 1);
        CHECK(v2[3] == 2000000000);
    }
}

// test for named binding
TEST_CASE_METHOD(common_tests, "Named parameters", "[core][use][named-params]")
{
    soci::session sql(backEndFactory_, connectString_);
    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        int i1 = 7;
        int i2 = 8;

        // verify the exception is thrown if both by position
        // and by name use elements are specified
        try
        {
            sql << "insert into soci_test(i1, i2) values(:i1, :i2)",
                use(i1, "i1"), use(i2);

            FAIL("expected exception not thrown");
        }
        catch (soci_error const& e)
        {
            CHECK(e.get_error_message() ==
                "Binding for use elements must be either by position "
                "or by name.");
        }

        // normal test
        sql << "insert into soci_test(i1, i2) values(:i1, :i2)",
            use(i1, "i1"), use(i2, "i2");

        i1 = 0;
        i2 = 0;
        sql << "select i1, i2 from soci_test", into(i1), into(i2);
        CHECK(i1 == 7);
        CHECK(i2 == 8);

        i2 = 0;
        sql << "select i2 from soci_test where i1 = :i1", into(i2), use(i1);
        CHECK(i2 == 8);

        sql << "delete from soci_test";

        // test vectors

        std::vector<int> v1;
        v1.push_back(1);
        v1.push_back(2);
        v1.push_back(3);

        std::vector<int> v2;
        v2.push_back(4);
        v2.push_back(5);
        v2.push_back(6);

        sql << "insert into soci_test(i1, i2) values(:i1, :i2)",
            use(v1, "i1"), use(v2, "i2");

        sql << "select i2, i1 from soci_test order by i1 desc",
            into(v1), into(v2);
        CHECK(v1.size() == 3);
        CHECK(v2.size() == 3);
        CHECK(v1[0] == 6);
        CHECK(v1[1] == 5);
        CHECK(v1[2] == 4);
        CHECK(v2[0] == 3);
        CHECK(v2[1] == 2);
        CHECK(v2[2] == 1);
    }
}

#endif // SOCI_POSTGRESQL_NOPARAMS

// transaction test
TEST_CASE_METHOD(common_tests, "Transactions", "[core][transaction]")
{
    soci::session sql(backEndFactory_, connectString_);

    if (!tc_.has_transactions_support(sql))
    {
        WARN("Transactions not supported by the database, skipping the test.");
        return;
    }

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    int count;
    sql << "select count(*) from soci_test", into(count);
    CHECK(count == 0);

    {
        transaction tr(sql);

        sql << "insert into soci_test (id, name) values(1, 'John')";
        sql << "insert into soci_test (id, name) values(2, 'Anna')";
        sql << "insert into soci_test (id, name) values(3, 'Mike')";

        tr.commit();
    }
    {
        transaction tr(sql);

        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 3);

        sql << "insert into soci_test (id, name) values(4, 'Stan')";

        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 4);

        tr.rollback();

        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 3);
    }
    {
        transaction tr(sql);

        sql << "delete from soci_test";

        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 0);

        tr.rollback();

        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 3);
    }
    {
        // additional test for detection of double commit
        transaction tr(sql);
        tr.commit();
        try
        {
            tr.commit();
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(e.get_error_message() ==
                "The transaction object cannot be handled twice.");
        }
    }
}

#ifndef SOCI_POSTGRESQL_NOPARAMS

std::tm  generate_tm()
{
    std::tm t = std::tm();
    t.tm_year = 105;
    t.tm_mon = 10;
    t.tm_mday = 15;
    t.tm_hour = 22;
    t.tm_min = 14;
    t.tm_sec = 17;
    return t;
}

// test of use elements with indicators
TEST_CASE_METHOD(common_tests, "Use with indicators", "[core][use][indicator]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    indicator ind1 = i_ok;
    indicator ind2 = i_ok;
    indicator ind3 = i_ok;

    int id = 1;
    int val = 10;
    char const* insert = "insert into soci_test(id, val, tm) values(:id, :val, :tm)";
    sql << insert, use(id, ind1), use(val, ind2), use(generate_tm(), ind3);

    id = 2;
    val = 11;
    ind2 = i_null;
    std::tm tm = std::tm();
    ind3 = i_null;

    sql << "insert into soci_test(id, val, tm) values(:id, :val, :tm)",
        use(id, ind1), use(val, ind2), use(tm, ind3);

    sql << "select val from soci_test where id = 1", into(val, ind2);
    CHECK(ind2 == i_ok);
    CHECK(val == 10);
    sql << "select val, tm from soci_test where id = 2", into(val, ind2), into(tm, ind3);
    CHECK(ind2 == i_null);
    CHECK(ind3 == i_null);

    std::vector<int> ids;
    ids.push_back(3);
    ids.push_back(4);
    ids.push_back(5);
    std::vector<int> vals;
    vals.push_back(12);
    vals.push_back(13);
    vals.push_back(14);
    std::vector<indicator> inds;
    inds.push_back(i_ok);
    inds.push_back(i_null);
    inds.push_back(i_ok);

    sql << "insert into soci_test(id, val) values(:id, :val)",
        use(ids), use(vals, inds);

    ids.resize(5);
    vals.resize(5);
    sql << "select id, val from soci_test order by id desc",
        into(ids), into(vals, inds);

    CHECK(ids.size() == 5);
    CHECK(ids[0] == 5);
    CHECK(ids[1] == 4);
    CHECK(ids[2] == 3);
    CHECK(ids[3] == 2);
    CHECK(ids[4] == 1);
    CHECK(inds.size() == 5);
    CHECK(inds[0] == i_ok);
    CHECK(inds[1] == i_null);
    CHECK(inds[2] == i_ok);
    CHECK(inds[3] == i_null);
    CHECK(inds[4] == i_ok);
    CHECK(vals.size() == 5);
    CHECK(vals[0] == 14);
    CHECK(vals[2] == 12);
    CHECK(vals[4] == 10);
}

#endif // SOCI_POSTGRESQL_NOPARAMS

// Dynamic binding to Row objects
TEST_CASE_METHOD(common_tests, "Dynamic row binding", "[core][dynamic]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);

    auto_table_creator tableCreator(tc_.table_creator_2(sql));

    row r;
    sql << "select * from soci_test", into(r);
    CHECK(sql.got_data() == false);

    sql << "insert into soci_test"
        " values(3.14, 123, \'Johny\',"
        << tc_.to_date_time("2005-12-19 22:14:17")
        << ", 'a')";

    // select into a row
    {
        statement st = (sql.prepare <<
            "select * from soci_test", into(r));
        st.execute(true);
        CHECK(r.size() == 5);

        CHECK(r.get_properties(0).get_data_type() == dt_double);
        CHECK(r.get_properties(1).get_data_type() == dt_integer);
        CHECK(r.get_properties(2).get_data_type() == dt_string);
        CHECK(r.get_properties(3).get_data_type() == dt_date);

        // type char is visible as string
        // - to comply with the implementation for Oracle
        CHECK(r.get_properties(4).get_data_type() == dt_string);

        CHECK(r.get_properties("NUM_INT").get_data_type() == dt_integer);

        CHECK(r.get_properties(0).get_name() == "NUM_FLOAT");
        CHECK(r.get_properties(1).get_name() == "NUM_INT");
        CHECK(r.get_properties(2).get_name() == "NAME");
        CHECK(r.get_properties(3).get_name() == "SOMETIME");
        CHECK(r.get_properties(4).get_name() == "CHR");

        ASSERT_EQUAL_APPROX(r.get<double>(0), 3.14);
        CHECK(r.get<int>(1) == 123);
        CHECK(r.get<std::string>(2) == "Johny");
        CHECK(r.get<std::tm>(3).tm_year == 105);

        // again, type char is visible as string
        CHECK_EQUAL_PADDED(r.get<std::string>(4), "a");

        ASSERT_EQUAL_APPROX(r.get<double>("NUM_FLOAT"), 3.14);
        CHECK(r.get<int>("NUM_INT") == 123);
        CHECK(r.get<std::string>("NAME") == "Johny");
        CHECK_EQUAL_PADDED(r.get<std::string>("CHR"), "a");

        CHECK(r.get_indicator(0) == i_ok);

        // verify exception thrown on invalid get<>
        bool caught = false;
        try
        {
            r.get<std::string>(0);
        }
        catch (std::bad_cast const &)
        {
            caught = true;
        }
        CHECK(caught);

        // additional test for stream-like extraction
        {
            double d;
            int i;
            std::string s;
            std::tm t = std::tm();
            std::string c;

            r >> d >> i >> s >> t >> c;

            ASSERT_EQUAL_APPROX(d, 3.14);
            CHECK(i == 123);
            CHECK(s == "Johny");
            CHECK(t.tm_year == 105);
            CHECK(t.tm_mon == 11);
            CHECK(t.tm_mday == 19);
            CHECK(t.tm_hour == 22);
            CHECK(t.tm_min == 14);
            CHECK(t.tm_sec == 17);
            CHECK_EQUAL_PADDED(c, "a");
        }
    }

    // additional test to check if the row object can be
    // reused between queries
    {
        sql << "select * from soci_test", into(r);

        CHECK(r.size() == 5);

        CHECK(r.get_properties(0).get_data_type() == dt_double);
        CHECK(r.get_properties(1).get_data_type() == dt_integer);
        CHECK(r.get_properties(2).get_data_type() == dt_string);
        CHECK(r.get_properties(3).get_data_type() == dt_date);

        sql << "select name, num_int from soci_test", into(r);

        CHECK(r.size() == 2);

        CHECK(r.get_properties(0).get_data_type() == dt_string);
        CHECK(r.get_properties(1).get_data_type() == dt_integer);
    }
}

// more dynamic bindings
TEST_CASE_METHOD(common_tests, "Dynamic row binding 2", "[core][dynamic]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    sql << "insert into soci_test(id, val) values(1, 10)";
    sql << "insert into soci_test(id, val) values(2, 20)";
    sql << "insert into soci_test(id, val) values(3, 30)";

#ifndef SOCI_POSTGRESQL_NOPARAMS
    {
        int id = 2;
        row r;
        sql << "select val from soci_test where id = :id", use(id), into(r);

        CHECK(r.size() == 1);
        CHECK(r.get_properties(0).get_data_type() == dt_integer);
        CHECK(r.get<int>(0) == 20);
    }
    {
        int id;
        row r;
        statement st = (sql.prepare <<
            "select val from soci_test where id = :id", use(id), into(r));

        id = 2;
        st.execute(true);
        CHECK(r.size() == 1);
        CHECK(r.get_properties(0).get_data_type() == dt_integer);
        CHECK(r.get<int>(0) == 20);

        id = 3;
        st.execute(true);
        CHECK(r.size() == 1);
        CHECK(r.get_properties(0).get_data_type() == dt_integer);
        CHECK(r.get<int>(0) == 30);

        id = 1;
        st.execute(true);
        CHECK(r.size() == 1);
        CHECK(r.get_properties(0).get_data_type() == dt_integer);
        CHECK(r.get<int>(0) == 10);
    }
#else
    {
        row r;
        sql << "select val from soci_test where id = 2", into(r);

        CHECK(r.size() == 1);
        CHECK(r.get_properties(0).get_data_type() == dt_integer);
        CHECK(r.get<int>(0) == 20);
    }
#endif // SOCI_POSTGRESQL_NOPARAMS
}

// More Dynamic binding to row objects
TEST_CASE_METHOD(common_tests, "Dynamic row binding 3", "[core][dynamic]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);

    auto_table_creator tableCreator(tc_.table_creator_3(sql));

    row r1;
    sql << "select * from soci_test", into(r1);
    CHECK(sql.got_data() == false);

    sql << "insert into soci_test values('david', '(404)123-4567')";
    sql << "insert into soci_test values('john', '(404)123-4567')";
    sql << "insert into soci_test values('doe', '(404)123-4567')";

    row r2;
    statement st = (sql.prepare << "select * from soci_test", into(r2));
    st.execute();

    CHECK(r2.size() == 2);

    int count = 0;
    while (st.fetch())
    {
        ++count;
        CHECK(r2.get<std::string>("PHONE") == "(404)123-4567");
    }
    CHECK(count == 3);
}

// This is like the previous test but with a type_conversion instead of a row
TEST_CASE_METHOD(common_tests, "Dynamic binding with type conversions", "[core][dynamic][type_conversion]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);

    SECTION("simple conversions")
    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        SECTION("between single basic type and user type")
        {
            MyInt mi;
            mi.set(123);
            sql << "insert into soci_test(id) values(:id)", use(mi);

            int i;
            sql << "select id from soci_test", into(i);
            CHECK(i == 123);

            sql << "update soci_test set id = id + 1";

            sql << "select id from soci_test", into(mi);
            CHECK(mi.get() == 124);
        }

        SECTION("with use const")
        {
            MyInt mi;
            mi.set(123);

            MyInt const & cmi = mi;
            sql << "insert into soci_test(id) values(:id)", use(cmi);

            int i;
            sql << "select id from soci_test", into(i);
            CHECK(i == 123);
        }
    }

    SECTION("ORM conversions")
    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        SECTION("conversions based on values")
        {
            PhonebookEntry p1;
            sql << "select * from soci_test", into(p1);
            CHECK(p1.name ==  "");
            CHECK(p1.phone == "");

            p1.name = "david";

            // Note: uppercase column names are used here (and later on)
            // for consistency with how they can be read from database
            // (which means forced to uppercase on Oracle) and how they are
            // set/get in the type conversion routines for PhonebookEntry.
            // In short, IF the database is Oracle,
            // then all column names for binding should be uppercase.
            sql << "insert into soci_test values(:NAME, :PHONE)", use(p1);
            sql << "insert into soci_test values('john', '(404)123-4567')";
            sql << "insert into soci_test values('doe', '(404)123-4567')";

            PhonebookEntry p2;
            statement st = (sql.prepare << "select * from soci_test", into(p2));
            st.execute();

            int count = 0;
            while (st.fetch())
            {
                ++count;
                if (p2.name == "david")
                {
                    // see type_conversion<PhonebookEntry>
                    CHECK(p2.phone =="<NULL>");
                }
                else
                {
                    CHECK(p2.phone == "(404)123-4567");
                }
            }
            CHECK(count == 3);
        }

        SECTION("conversions based on values with use const")
        {
            PhonebookEntry p1;
            p1.name = "Joe Coder";
            p1.phone = "123-456";

            PhonebookEntry const & cp1 = p1;

            sql << "insert into soci_test values(:NAME, :PHONE)", use(cp1);

            PhonebookEntry p2;
            sql << "select * from soci_test", into(p2);
            CHECK(sql.got_data());

            CHECK(p2.name == "Joe Coder");
            CHECK(p2.phone == "123-456");
        }

        SECTION("conversions based on accessor functions (as opposed to direct variable bindings)")
        {
            PhonebookEntry3 p1;
            p1.setName("Joe Hacker");
            p1.setPhone("10010110");

            sql << "insert into soci_test values(:NAME, :PHONE)", use(p1);

            PhonebookEntry3 p2;
            sql << "select * from soci_test", into(p2);
            CHECK(sql.got_data());

            CHECK(p2.getName() == "Joe Hacker");
            CHECK(p2.getPhone() == "10010110");
        }

        SECTION("PhonebookEntry2 type conversion to test calls to values::get_indicator()")
        {
            PhonebookEntry2 p1;
            sql << "select * from soci_test", into(p1);
            CHECK(p1.name ==  "");
            CHECK(p1.phone == "");
            p1.name = "david";

            sql << "insert into soci_test values(:NAME, :PHONE)", use(p1);
            sql << "insert into soci_test values('john', '(404)123-4567')";
            sql << "insert into soci_test values('doe', '(404)123-4567')";

            PhonebookEntry2 p2;
            statement st = (sql.prepare << "select * from soci_test", into(p2));
            st.execute();

            int count = 0;
            while (st.fetch())
            {
                ++count;
                if (p2.name == "david")
                {
                    // see type_conversion<PhonebookEntry2>
                    CHECK(p2.phone =="<NULL>");
                }
                else
                {
                    CHECK(p2.phone == "(404)123-4567");
                }
            }
            CHECK(count == 3);
        }
    }
}

TEST_CASE_METHOD(common_tests, "Prepared insert with ORM", "[core][orm]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);
    auto_table_creator tableCreator(tc_.table_creator_3(sql));

    PhonebookEntry temp;
    PhonebookEntry e1 = { "name1", "phone1" };
    PhonebookEntry e2 = { "name2", "phone2" };

    //sql << "insert into soci_test values (:NAME, :PHONE)", use(temp);
    statement insertStatement = (sql.prepare << "insert into soci_test values (:NAME, :PHONE)", use(temp));

    temp = e1;
    insertStatement.execute(true);
    temp = e2;
    insertStatement.execute(true);

    int count = 0;

    sql << "select count(*) from soci_test where NAME in ('name1', 'name2')", into(count);

    CHECK(count == 2);
}

TEST_CASE_METHOD(common_tests, "Partial match with ORM", "[core][orm]")
{
    soci::session sql(backEndFactory_, connectString_);
    sql.uppercase_column_names(true);
    auto_table_creator tableCreator(tc_.table_creator_3(sql));

    PhonebookEntry in = { "name1", "phone1" };
    std::string name = "nameA";
    sql << "insert into soci_test values (:NAMED, :PHONE)", use(in), use(name, "NAMED");

    PhonebookEntry out;
    sql << "select * from soci_test where PHONE = 'phone1'", into(out);
    CHECK(out.name == "nameA");
    CHECK(out.phone == "phone1");
}

TEST_CASE_METHOD(common_tests, "Numeric round trip", "[core][float]")
{
    soci::session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    double d1 = 0.003958,
           d2;

    sql << "insert into soci_test(num76) values (:d1)", use(d1);
    sql << "select num76 from soci_test", into(d2);

    // The numeric value should make the round trip unchanged, we really want
    // to use exact comparisons here.
    ASSERT_EQUAL_EXACT(d1, d2);

    // test negative doubles too
    sql << "delete from soci_test";
    d1 = -d1;

    sql << "insert into soci_test(num76) values (:d1)", use(d1);
    sql << "select num76 from soci_test", into(d2);

    ASSERT_EQUAL_EXACT(d1, d2);
}

#ifndef SOCI_POSTGRESQL_NOPARAMS

// test for bulk fetch with single use
TEST_CASE_METHOD(common_tests, "Bulk fetch with single use", "[core][bulk]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    sql << "insert into soci_test(name, id) values('john', 1)";
    sql << "insert into soci_test(name, id) values('george', 2)";
    sql << "insert into soci_test(name, id) values('anthony', 1)";
    sql << "insert into soci_test(name, id) values('marc', 3)";
    sql << "insert into soci_test(name, id) values('julian', 1)";

    int code = 1;
    std::vector<std::string> names(10);
    sql << "select name from soci_test where id = :id order by name",
         into(names), use(code);

    CHECK(names.size() == 3);
    CHECK(names[0] == "anthony");
    CHECK(names[1] == "john");
    CHECK(names[2] == "julian");
}

#endif // SOCI_POSTGRESQL_NOPARAMS

// test for basic logging support
TEST_CASE_METHOD(common_tests, "Basic logging support", "[core][logging]")
{
    soci::session sql(backEndFactory_, connectString_);

    std::ostringstream log;
    sql.set_log_stream(&log);

    try
    {
        sql << "drop table soci_test1";
    }
    catch (...) {}

    CHECK(sql.get_last_query() == "drop table soci_test1");

    sql.set_log_stream(NULL);

    try
    {
        sql << "drop table soci_test2";
    }
    catch (...) {}

    CHECK(sql.get_last_query() == "drop table soci_test2");

    sql.set_log_stream(&log);

    try
    {
        sql << "drop table soci_test3";
    }
    catch (...) {}

    CHECK(sql.get_last_query() == "drop table soci_test3");
    CHECK(log.str() ==
        "drop table soci_test1\n"
        "drop table soci_test3\n");

}

// test for rowset creation and copying
TEST_CASE_METHOD(common_tests, "Rowset creation and copying", "[core][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        // Open empty rowset
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        CHECK(rs1.begin() == rs1.end());
    }

    {
        // Copy construction
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        rowset<row> rs2(rs1);
        rowset<row> rs3(rs1);
        rowset<row> rs4(rs3);

        CHECK(rs1.begin() == rs2.begin());
        CHECK(rs1.begin() == rs3.begin());
        CHECK(rs1.end() == rs2.end());
        CHECK(rs1.end() == rs3.end());
    }

    if (!tc_.has_multiple_select_bug())
    {
        // Assignment
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        rowset<row> rs2 = (sql.prepare << "select * from soci_test");
        rowset<row> rs3 = (sql.prepare << "select * from soci_test");
        rs1 = rs2;
        rs3 = rs2;

        CHECK(rs1.begin() == rs2.begin());
        CHECK(rs1.begin() == rs3.begin());
        CHECK(rs1.end() == rs2.end());
        CHECK(rs1.end() == rs3.end());
    }
}

// test for simple iterating using rowset iterator (without reading data)
TEST_CASE_METHOD(common_tests, "Rowset iteration", "[core][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(id, val) values(1, 10)";
        sql << "insert into soci_test(id, val) values(2, 11)";
        sql << "insert into soci_test(id, val) values(3, NULL)";
        sql << "insert into soci_test(id, val) values(4, NULL)";
        sql << "insert into soci_test(id, val) values(5, 12)";
        {
            rowset<row> rs = (sql.prepare << "select * from soci_test");

            CHECK(5 == std::distance(rs.begin(), rs.end()));
        }
    }

}

// test for reading rowset<row> using iterator
TEST_CASE_METHOD(common_tests, "Reading rows from rowset", "[core][row][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        {
            // Empty rowset
            rowset<row> rs = (sql.prepare << "select * from soci_test");
            CHECK(0 == std::distance(rs.begin(), rs.end()));
        }

        {
            // Non-empty rowset
            sql << "insert into soci_test values(3.14, 123, \'Johny\',"
                << tc_.to_date_time("2005-12-19 22:14:17")
                << ", 'a')";
            sql << "insert into soci_test values(6.28, 246, \'Robert\',"
                << tc_.to_date_time("2004-10-01 18:44:10")
                << ", 'b')";

            rowset<row> rs = (sql.prepare << "select * from soci_test");

            rowset<row>::const_iterator it = rs.begin();
            CHECK(it != rs.end());

            //
            // First row
            //
            row const & r1 = (*it);

            // Properties
            CHECK(r1.size() == 5);
            CHECK(r1.get_properties(0).get_data_type() == dt_double);
            CHECK(r1.get_properties(1).get_data_type() == dt_integer);
            CHECK(r1.get_properties(2).get_data_type() == dt_string);
            CHECK(r1.get_properties(3).get_data_type() == dt_date);
            CHECK(r1.get_properties(4).get_data_type() == dt_string);
            CHECK(r1.get_properties("NUM_INT").get_data_type() == dt_integer);

            // Data

            // Since we didn't specify order by in the above query,
            // the 2 rows may be returned in either order
            // (If we specify order by, we can't do it in a cross db
            // compatible way, because the Oracle table for this has been
            // created with lower case column names)

            std::string name = r1.get<std::string>(2);

            if (name == "Johny")
            {
                ASSERT_EQUAL_APPROX(r1.get<double>(0), 3.14);
                CHECK(r1.get<int>(1) == 123);
                CHECK(r1.get<std::string>(2) == "Johny");
                std::tm t1 = std::tm();
                t1 = r1.get<std::tm>(3);
                CHECK(t1.tm_year == 105);
                CHECK_EQUAL_PADDED(r1.get<std::string>(4), "a");
                ASSERT_EQUAL_APPROX(r1.get<double>("NUM_FLOAT"), 3.14);
                CHECK(r1.get<int>("NUM_INT") == 123);
                CHECK(r1.get<std::string>("NAME") == "Johny");
                CHECK_EQUAL_PADDED(r1.get<std::string>("CHR"), "a");
            }
            else if (name == "Robert")
            {
                ASSERT_EQUAL(r1.get<double>(0), 6.28);
                CHECK(r1.get<int>(1) == 246);
                CHECK(r1.get<std::string>(2) == "Robert");
                std::tm t1 = r1.get<std::tm>(3);
                CHECK(t1.tm_year == 104);
                CHECK(r1.get<std::string>(4) == "b");
                ASSERT_EQUAL(r1.get<double>("NUM_FLOAT"), 6.28);
                CHECK(r1.get<int>("NUM_INT") == 246);
                CHECK(r1.get<std::string>("NAME") == "Robert");
                CHECK_EQUAL_PADDED(r1.get<std::string>("CHR"), "b");
            }
            else
            {
                CAPTURE(name);
                FAIL("expected \"Johny\" or \"Robert\"");
            }

            //
            // Iterate to second row
            //
            ++it;
            CHECK(it != rs.end());

            //
            // Second row
            //
            row const & r2 = (*it);

            // Properties
            CHECK(r2.size() == 5);
            CHECK(r2.get_properties(0).get_data_type() == dt_double);
            CHECK(r2.get_properties(1).get_data_type() == dt_integer);
            CHECK(r2.get_properties(2).get_data_type() == dt_string);
            CHECK(r2.get_properties(3).get_data_type() == dt_date);
            CHECK(r2.get_properties(4).get_data_type() == dt_string);
            CHECK(r2.get_properties("NUM_INT").get_data_type() == dt_integer);

            std::string newName = r2.get<std::string>(2);
            CHECK(name != newName);

            if (newName == "Johny")
            {
                ASSERT_EQUAL_APPROX(r2.get<double>(0), 3.14);
                CHECK(r2.get<int>(1) == 123);
                CHECK(r2.get<std::string>(2) == "Johny");
                std::tm t2 = r2.get<std::tm>(3);
                CHECK(t2.tm_year == 105);
                CHECK(r2.get<std::string>(4) == "a");
                ASSERT_EQUAL_APPROX(r2.get<double>("NUM_FLOAT"), 3.14);
                CHECK(r2.get<int>("NUM_INT") == 123);
                CHECK(r2.get<std::string>("NAME") == "Johny");
                CHECK(r2.get<std::string>("CHR") == "a");
            }
            else if (newName == "Robert")
            {
                ASSERT_EQUAL_APPROX(r2.get<double>(0), 6.28);
                CHECK(r2.get<int>(1) == 246);
                CHECK(r2.get<std::string>(2) == "Robert");
                std::tm t2 = r2.get<std::tm>(3);
                CHECK(t2.tm_year == 104);
                CHECK_EQUAL_PADDED(r2.get<std::string>(4), "b");
                ASSERT_EQUAL_APPROX(r2.get<double>("NUM_FLOAT"), 6.28);
                CHECK(r2.get<int>("NUM_INT") == 246);
                CHECK(r2.get<std::string>("NAME") == "Robert");
                CHECK_EQUAL_PADDED(r2.get<std::string>("CHR"), "b");
            }
            else
            {
                CAPTURE(newName);
                FAIL("expected \"Johny\" or \"Robert\"");
            }
        }

        {
            // Non-empty rowset with NULL values
            sql << "insert into soci_test "
                << "(num_int, num_float , name, sometime, chr) "
                << "values (0, NULL, NULL, NULL, NULL)";

            rowset<row> rs = (sql.prepare
                     << "select num_int, num_float, name, sometime, chr "
                     << "from soci_test where num_int = 0");

            rowset<row>::const_iterator it = rs.begin();
            CHECK(it != rs.end());

            //
            // First row
            //
            row const& r1 = (*it);

            // Properties
            CHECK(r1.size() == 5);
            CHECK(r1.get_properties(0).get_data_type() == dt_integer);
            CHECK(r1.get_properties(1).get_data_type() == dt_double);
            CHECK(r1.get_properties(2).get_data_type() == dt_string);
            CHECK(r1.get_properties(3).get_data_type() == dt_date);
            CHECK(r1.get_properties(4).get_data_type() == dt_string);

            // Data
            CHECK(r1.get_indicator(0) == soci::i_ok);
            CHECK(r1.get<int>(0) == 0);
            CHECK(r1.get_indicator(1) == soci::i_null);
            CHECK(r1.get_indicator(2) == soci::i_null);
            CHECK(r1.get_indicator(3) == soci::i_null);
            CHECK(r1.get_indicator(4) == soci::i_null);
        }
    }
}

// test for reading rowset<int> using iterator
TEST_CASE_METHOD(common_tests, "Reading ints from rowset", "[core][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(id) values(1)";
        sql << "insert into soci_test(id) values(2)";
        sql << "insert into soci_test(id) values(3)";
        sql << "insert into soci_test(id) values(4)";
        sql << "insert into soci_test(id) values(5)";
        {
            rowset<int> rs = (sql.prepare << "select id from soci_test order by id asc");

            // 1st row
            rowset<int>::const_iterator pos = rs.begin();
            CHECK(1 == (*pos));

            // 3rd row
            std::advance(pos, 2);
            CHECK(3 == (*pos));

            // 5th row
            std::advance(pos, 2);
            CHECK(5 == (*pos));

            // The End
            ++pos;
            CHECK(pos == rs.end());
        }
    }

}

// test for handling 'use' and reading rowset<std::string> using iterator
TEST_CASE_METHOD(common_tests, "Reading strings from rowset", "[core][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(str) values('abc')";
        sql << "insert into soci_test(str) values('def')";
        sql << "insert into soci_test(str) values('ghi')";
        sql << "insert into soci_test(str) values('jkl')";
        {
            // Expected result in numbers
            std::string idle("def");
            rowset<std::string> rs1 = (sql.prepare
                    << "select str from soci_test where str = :idle",
                    use(idle));

            CHECK(1 == std::distance(rs1.begin(), rs1.end()));

            // Expected result in value
            idle = "jkl";
            rowset<std::string> rs2 = (sql.prepare
                    << "select str from soci_test where str = :idle",
                    use(idle));

            CHECK(idle == *(rs2.begin()));
        }
    }

}

// test for handling troublemaker
TEST_CASE_METHOD(common_tests, "Rowset expected exception", "[core][exception][rowset]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    sql << "insert into soci_test(str) values('abc')";

    std::string troublemaker;
    CHECK_THROWS_AS(
        rowset<std::string>((sql.prepare << "select str from soci_test", into(troublemaker))),
        soci_error
        );
}

// functor for next test
struct THelper
{
    THelper()
        : val_()
    {
    }
    void operator()(int i)
    {
        val_ = i;
    }
    int val_;
};

// test for handling NULL values with expected exception:
// "Null value fetched and no indicator defined."
TEST_CASE_METHOD(common_tests, "NULL expected exception", "[core][exception][null]")
{
    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    sql << "insert into soci_test(val) values(1)";
    sql << "insert into soci_test(val) values(2)";
    sql << "insert into soci_test(val) values(NULL)";
    sql << "insert into soci_test(val) values(3)";

    rowset<int> rs = (sql.prepare << "select val from soci_test order by val asc");

    CHECK_THROWS_AS( std::for_each(rs.begin(), rs.end(), THelper()), soci_error );
}

// This is like the first dynamic binding test but with rowset and iterators use
TEST_CASE_METHOD(common_tests, "Dynamic binding with rowset", "[core][dynamic][type_conversion]")
{
    soci::session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);

    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry p1;
        sql << "select * from soci_test", into(p1);
        CHECK(p1.name ==  "");
        CHECK(p1.phone == "");

        p1.name = "david";

        sql << "insert into soci_test values(:NAME, :PHONE)", use(p1);
        sql << "insert into soci_test values('john', '(404)123-4567')";
        sql << "insert into soci_test values('doe', '(404)123-4567')";

        rowset<PhonebookEntry> rs = (sql.prepare << "select * from soci_test");

        int count = 0;
        for (rowset<PhonebookEntry>::const_iterator it = rs.begin(); it != rs.end(); ++it)
        {
            ++count;
            PhonebookEntry const& p2 = (*it);
            if (p2.name == "david")
            {
                // see type_conversion<PhonebookEntry>
                CHECK(p2.phone =="<NULL>");
            }
            else
            {
                CHECK(p2.phone == "(404)123-4567");
            }
        }

        CHECK(3 == count);
    }
}

#ifdef SOCI_HAVE_BOOST

// test for handling NULL values with boost::optional
// (both into and use)
TEST_CASE_METHOD(common_tests, "NULL with optional", "[core][boost][null]")
{

    soci::session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(val) values(7)";

        {
            // verify non-null value is fetched correctly
            boost::optional<int> opt;
            sql << "select val from soci_test", into(opt);
            CHECK(opt.is_initialized());
            CHECK(opt.get() == 7);

            // indicators can be used with optional
            // (although that's just a consequence of implementation,
            // not an intended feature - but let's test it anyway)
            indicator ind;
            opt.reset();
            sql << "select val from soci_test", into(opt, ind);
            CHECK(opt.is_initialized());
            CHECK(opt.get() == 7);
            CHECK(ind == i_ok);

            // verify null value is fetched correctly
            sql << "select i1 from soci_test", into(opt);
            CHECK(opt.is_initialized() == false);

            // and with indicator
            opt = 5;
            sql << "select i1 from soci_test", into(opt, ind);
            CHECK(opt.is_initialized() == false);
            CHECK(ind == i_null);

            // verify non-null is inserted correctly
            opt = 3;
            sql << "update soci_test set val = :v", use(opt);
            int j = 0;
            sql << "select val from soci_test", into(j);
            CHECK(j == 3);

            // verify null is inserted correctly
            opt.reset();
            sql << "update soci_test set val = :v", use(opt);
            ind = i_ok;
            sql << "select val from soci_test", into(j, ind);
            CHECK(ind == i_null);
        }

        // vector tests (select)

        {
            sql << "delete from soci_test";

            // simple readout of non-null data

            sql << "insert into soci_test(id, val, str) values(1, 5, \'abc\')";
            sql << "insert into soci_test(id, val, str) values(2, 6, \'def\')";
            sql << "insert into soci_test(id, val, str) values(3, 7, \'ghi\')";
            sql << "insert into soci_test(id, val, str) values(4, 8, null)";
            sql << "insert into soci_test(id, val, str) values(5, 9, \'mno\')";

            std::vector<boost::optional<int> > v(10);
            sql << "select val from soci_test order by val", into(v);

            CHECK(v.size() == 5);
            CHECK(v[0].is_initialized());
            CHECK(v[0].get() == 5);
            CHECK(v[1].is_initialized());
            CHECK(v[1].get() == 6);
            CHECK(v[2].is_initialized());
            CHECK(v[2].get() == 7);
            CHECK(v[3].is_initialized());
            CHECK(v[3].get() == 8);
            CHECK(v[4].is_initialized());
            CHECK(v[4].get() == 9);

            // readout of nulls

            sql << "update soci_test set val = null where id = 2 or id = 4";

            std::vector<int> ids(5);
            sql << "select id, val from soci_test order by id", into(ids), into(v);

            CHECK(v.size() == 5);
            CHECK(ids.size() == 5);
            CHECK(v[0].is_initialized());
            CHECK(v[0].get() == 5);
            CHECK(v[1].is_initialized() == false);
            CHECK(v[2].is_initialized());
            CHECK(v[2].get() == 7);
            CHECK(v[3].is_initialized() == false);
            CHECK(v[4].is_initialized());
            CHECK(v[4].get() == 9);

            // readout with statement preparation

            int id = 1;

            ids.resize(3);
            v.resize(3);
            statement st = (sql.prepare <<
                "select id, val from soci_test order by id", into(ids), into(v));
            st.execute();
            while (st.fetch())
            {
                for (std::size_t i = 0; i != v.size(); ++i)
                {
                    CHECK(id == ids[i]);

                    if (id == 2 || id == 4)
                    {
                        CHECK(v[i].is_initialized() == false);
                    }
                    else
                    {
                        CHECK(v[i].is_initialized());
                        CHECK(v[i].get() == id + 4);
                    }

                    ++id;
                }

                ids.resize(3);
                v.resize(3);
            }
            CHECK(id == 6);
        }

        // and why not stress iterators and the dynamic binding, too!

        {
            rowset<row> rs = (sql.prepare << "select id, val, str from soci_test order by id");

            rowset<row>::const_iterator it = rs.begin();
            CHECK(it != rs.end());

            row const& r1 = (*it);

            CHECK(r1.size() == 3);

            // Note: for the reason of differences between number(x,y) type and
            // binary representation of integers, the following commented assertions
            // do not work for Oracle.
            // The problem is that for this single table the data type used in Oracle
            // table creator for the id column is number(10,0),
            // which allows to insert all int values.
            // On the other hand, the column description scheme used in the Oracle
            // backend figures out that the natural type for such a column
            // is eUnsignedInt - this makes the following assertions fail.
            // Other database backends (like PostgreSQL) use other types like int
            // and this not only allows to insert all int values (obviously),
            // but is also recognized as int (obviously).
            // There is a similar problem with stream-like extraction,
            // where internally get<T> is called and the type mismatch is detected
            // for the id column - that's why the code below skips this column
            // and tests the remaining column only.

            //CHECK(r1.get_properties(0).get_data_type() == dt_integer);
            CHECK(r1.get_properties(1).get_data_type() == dt_integer);
            CHECK(r1.get_properties(2).get_data_type() == dt_string);
            //CHECK(r1.get<int>(0) == 1);
            CHECK(r1.get<int>(1) == 5);
            CHECK(r1.get<std::string>(2) == "abc");
            CHECK(r1.get<boost::optional<int> >(1).is_initialized());
            CHECK(r1.get<boost::optional<int> >(1).get() == 5);
            CHECK(r1.get<boost::optional<std::string> >(2).is_initialized());
            CHECK(r1.get<boost::optional<std::string> >(2).get() == "abc");

            ++it;

            row const& r2 = (*it);

            CHECK(r2.size() == 3);

            // CHECK(r2.get_properties(0).get_data_type() == dt_integer);
            CHECK(r2.get_properties(1).get_data_type() == dt_integer);
            CHECK(r2.get_properties(2).get_data_type() == dt_string);
            //CHECK(r2.get<int>(0) == 2);
            try
            {
                // expect exception here, this is NULL value
                (void)r1.get<int>(1);
                FAIL("expected exception not thrown");
            }
            catch (soci_error const &) {}

            // but we can read it as optional
            CHECK(r2.get<boost::optional<int> >(1).is_initialized() == false);

            // stream-like data extraction

            ++it;
            row const &r3 = (*it);

            boost::optional<int> io;
            boost::optional<std::string> so;

            r3.skip(); // move to val and str columns
            r3 >> io >> so;

            CHECK(io.is_initialized());
            CHECK(io.get() == 7);
            CHECK(so.is_initialized());
            CHECK(so.get() == "ghi");

            ++it;
            row const &r4 = (*it);

            r3.skip(); // move to val and str columns
            r4 >> io >> so;

            CHECK(io.is_initialized() == false);
            CHECK(so.is_initialized() == false);
        }

        // bulk inserts of non-null data

        {
            sql << "delete from soci_test";

            std::vector<int> ids;
            std::vector<boost::optional<int> > v;

            ids.push_back(10); v.push_back(20);
            ids.push_back(11); v.push_back(21);
            ids.push_back(12); v.push_back(22);
            ids.push_back(13); v.push_back(23);

            sql << "insert into soci_test(id, val) values(:id, :val)",
                use(ids, "id"), use(v, "val");

            int sum;
            sql << "select sum(val) from soci_test", into(sum);
            CHECK(sum == 86);

            // bulk inserts of some-null data

            sql << "delete from soci_test";

            v[2].reset();
            v[3].reset();

            sql << "insert into soci_test(id, val) values(:id, :val)",
                use(ids, "id"), use(v, "val");

            sql << "select sum(val) from soci_test", into(sum);
            CHECK(sum == 41);
        }

        // composability with user conversions

        {
            sql << "delete from soci_test";

            boost::optional<MyInt> omi1;
            boost::optional<MyInt> omi2;

            omi1 = MyInt(125);
            omi2.reset();

            sql << "insert into soci_test(id, val) values(:id, :val)",
                use(omi1), use(omi2);

            sql << "select id, val from soci_test", into(omi2), into(omi1);

            CHECK(omi1.is_initialized() == false);
            CHECK(omi2.is_initialized());
            CHECK(omi2.get().get() == 125);
        }

        // use with const optional and user conversions

        {
            sql << "delete from soci_test";

            boost::optional<MyInt> omi1;
            boost::optional<MyInt> omi2;

            omi1 = MyInt(125);
            omi2.reset();

            boost::optional<MyInt> const & comi1 = omi1;
            boost::optional<MyInt> const & comi2 = omi2;

            sql << "insert into soci_test(id, val) values(:id, :val)",
                use(comi1), use(comi2);

            sql << "select id, val from soci_test", into(omi2), into(omi1);

            CHECK(omi1.is_initialized() == false);
            CHECK(omi2.is_initialized());
            CHECK(omi2.get().get() == 125);
        }

        // use with rowset and table containing null values

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            sql << "insert into soci_test(id, val) values(1, 10)";
            sql << "insert into soci_test(id, val) values(2, 11)";
            sql << "insert into soci_test(id, val) values(3, NULL)";
            sql << "insert into soci_test(id, val) values(4, 13)";

            rowset<boost::optional<int> > rs = (sql.prepare <<
                "select val from soci_test order by id asc");

            // 1st row
            rowset<boost::optional<int> >::const_iterator pos = rs.begin();
            CHECK((*pos).is_initialized());
            CHECK(10 == (*pos).get());

            // 2nd row
            ++pos;
            CHECK((*pos).is_initialized());
            CHECK(11 == (*pos).get());

            // 3rd row
            ++pos;
            CHECK((*pos).is_initialized() == false);

            // 4th row
            ++pos;
            CHECK((*pos).is_initialized());
            CHECK(13 == (*pos).get());
        }
    }
}

#endif // SOCI_HAVE_BOOST

// connection and reconnection tests
TEST_CASE_METHOD(common_tests, "Connection and reconnection", "[core][connect]")
{
    {
        // empty session
        soci::session sql;

        // idempotent:
        sql.close();

        try
        {
            sql.reconnect();
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(e.get_error_message() ==
               "Cannot reconnect without previous connection.");
        }

        // open from empty session
        sql.open(backEndFactory_, connectString_);
        sql.close();

        // reconnecting from closed session
        sql.reconnect();

        // opening already connected session
        try
        {
            sql.open(backEndFactory_, connectString_);
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(e.get_error_message() ==
               "Cannot open already connected session.");
        }

        sql.close();

        // open from closed
        sql.open(backEndFactory_, connectString_);

        // reconnect from already connected session
        sql.reconnect();
    }

    {
        soci::session sql;

        try
        {
            sql << "this statement cannot execute";
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(e.get_error_message() ==
                "Session is not connected.");
        }
    }

}

#ifdef SOCI_HAVE_BOOST

TEST_CASE_METHOD(common_tests, "Boost tuple", "[core][boost][tuple]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        boost::tuple<double, int, std::string> t1(3.5, 7, "Joe Hacker");
        ASSERT_EQUAL(t1.get<0>(), 3.5);
        CHECK(t1.get<1>() == 7);
        CHECK(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // basic query

        boost::tuple<double, int, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(t2.get<0>(), 3.5);
        CHECK(t2.get<1>() == 7);
        CHECK(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with boost::optional

        // use:
        boost::tuple<double, boost::optional<int>, std::string> t1(
            3.5, boost::optional<int>(7), "Joe Hacker");
        ASSERT_EQUAL(t1.get<0>(), 3.5);
        CHECK(t1.get<1>().is_initialized());
        CHECK(t1.get<1>().get() == 7);
        CHECK(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, boost::optional<int>, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(t2.get<0>(), 3.5);
        CHECK(t2.get<1>().is_initialized());
        CHECK(t2.get<1>().get() == 7);
        CHECK(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with user-provided conversions

        // use:
        boost::tuple<double, MyInt, std::string> t1(3.5, 7, "Joe Hacker");
        ASSERT_EQUAL(t1.get<0>(), 3.5);
        CHECK(t1.get<1>().get() == 7);
        CHECK(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, MyInt, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(t2.get<0>(), 3.5);
        CHECK(t2.get<1>().get() == 7);
        CHECK(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // let's have fun - composition of tuple, optional and user-defined type

        // use:
        boost::tuple<double, boost::optional<MyInt>, std::string> t1(
            3.5, boost::optional<MyInt>(7), "Joe Hacker");
        ASSERT_EQUAL(t1.get<0>(), 3.5);
        CHECK(t1.get<1>().is_initialized());
        CHECK(t1.get<1>().get().get() == 7);
        CHECK(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, boost::optional<MyInt>, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(t2.get<0>(), 3.5);
        CHECK(t2.get<1>().is_initialized());
        CHECK(t2.get<1>().get().get() == 7);
        CHECK(t2.get<2>() == "Joe Hacker");

        sql << "update soci_test set num_int = NULL";

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(t2.get<0>(), 3.5);
        CHECK(t2.get<1>().is_initialized() == false);
        CHECK(t2.get<2>() == "Joe Hacker");
    }

    {
        // rowset<tuple>

        sql << "insert into soci_test(num_float, num_int, name) values(4.0, 8, 'Tony Coder')";
        sql << "insert into soci_test(num_float, num_int, name) values(4.5, NULL, 'Cecile Sharp')";
        sql << "insert into soci_test(num_float, num_int, name) values(5.0, 10, 'Djhava Ravaa')";

        typedef boost::tuple<double, boost::optional<int>, std::string> T;

        rowset<T> rs = (sql.prepare
            << "select num_float, num_int, name from soci_test order by num_float asc");

        rowset<T>::const_iterator pos = rs.begin();

        ASSERT_EQUAL(pos->get<0>(), 3.5);
        CHECK(pos->get<1>().is_initialized() == false);
        CHECK(pos->get<2>() == "Joe Hacker");

        ++pos;
        ASSERT_EQUAL(pos->get<0>(), 4.0);
        CHECK(pos->get<1>().is_initialized());
        CHECK(pos->get<1>().get() == 8);
        CHECK(pos->get<2>() == "Tony Coder");

        ++pos;
        ASSERT_EQUAL(pos->get<0>(), 4.5);
        CHECK(pos->get<1>().is_initialized() == false);
        CHECK(pos->get<2>() == "Cecile Sharp");

        ++pos;
        ASSERT_EQUAL(pos->get<0>(),  5.0);
        CHECK(pos->get<1>().is_initialized());
        CHECK(pos->get<1>().get() == 10);
        CHECK(pos->get<2>() == "Djhava Ravaa");

        ++pos;
        CHECK(pos == rs.end());
    }
}

#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500

TEST_CASE_METHOD(common_tests, "Boost fusion", "[core][boost][fusion]")
{

    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        boost::fusion::vector<double, int, std::string> t1(3.5, 7, "Joe Hacker");
        ASSERT_EQUAL(boost::fusion::at_c<0>(t1), 3.5);
        CHECK(boost::fusion::at_c<1>(t1) == 7);
        CHECK(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // basic query

        boost::fusion::vector<double, int, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(boost::fusion::at_c<0>(t2), 3.5);
        CHECK(boost::fusion::at_c<1>(t2) == 7);
        CHECK(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with boost::optional

        // use:
        boost::fusion::vector<double, boost::optional<int>, std::string> t1(
            3.5, boost::optional<int>(7), "Joe Hacker");
        ASSERT_EQUAL(boost::fusion::at_c<0>(t1), 3.5);
        CHECK(boost::fusion::at_c<1>(t1).is_initialized());
        CHECK(boost::fusion::at_c<1>(t1).get() == 7);
        CHECK(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, boost::optional<int>, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(boost::fusion::at_c<0>(t2), 3.5);
        CHECK(boost::fusion::at_c<1>(t2).is_initialized());
        CHECK(boost::fusion::at_c<1>(t2) == 7);
        CHECK(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with user-provided conversions

        // use:
        boost::fusion::vector<double, MyInt, std::string> t1(3.5, 7, "Joe Hacker");
        ASSERT_EQUAL(boost::fusion::at_c<0>(t1), 3.5);
        CHECK(boost::fusion::at_c<1>(t1).get() == 7);
        CHECK(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, MyInt, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(boost::fusion::at_c<0>(t2), 3.5);
        CHECK(boost::fusion::at_c<1>(t2).get() == 7);
        CHECK(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // let's have fun - composition of tuple, optional and user-defined type

        // use:
        boost::fusion::vector<double, boost::optional<MyInt>, std::string> t1(
            3.5, boost::optional<MyInt>(7), "Joe Hacker");
        ASSERT_EQUAL(boost::fusion::at_c<0>(t1), 3.5);
        CHECK(boost::fusion::at_c<1>(t1).is_initialized());
        CHECK(boost::fusion::at_c<1>(t1).get().get() == 7);
        CHECK(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, boost::optional<MyInt>, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(boost::fusion::at_c<0>(t2), 3.5);
        CHECK(boost::fusion::at_c<1>(t2).is_initialized());
        CHECK(boost::fusion::at_c<1>(t2).get().get() == 7);
        CHECK(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "update soci_test set num_int = NULL";

        sql << "select num_float, num_int, name from soci_test", into(t2);

        ASSERT_EQUAL(boost::fusion::at_c<0>(t2), 3.5);
        CHECK(boost::fusion::at_c<1>(t2).is_initialized() == false);
        CHECK(boost::fusion::at_c<2>(t2) == "Joe Hacker");
    }

    {
        // rowset<fusion::vector>

        sql << "insert into soci_test(num_float, num_int, name) values(4.0, 8, 'Tony Coder')";
        sql << "insert into soci_test(num_float, num_int, name) values(4.5, NULL, 'Cecile Sharp')";
        sql << "insert into soci_test(num_float, num_int, name) values(5.0, 10, 'Djhava Ravaa')";

        typedef boost::fusion::vector<double, boost::optional<int>, std::string> T;

        rowset<T> rs = (sql.prepare
            << "select num_float, num_int, name from soci_test order by num_float asc");

        rowset<T>::const_iterator pos = rs.begin();

        ASSERT_EQUAL(boost::fusion::at_c<0>(*pos), 3.5);
        CHECK(boost::fusion::at_c<1>(*pos).is_initialized() == false);
        CHECK(boost::fusion::at_c<2>(*pos) == "Joe Hacker");

        ++pos;
        ASSERT_EQUAL(boost::fusion::at_c<0>(*pos), 4.0);
        CHECK(boost::fusion::at_c<1>(*pos).is_initialized());
        CHECK(boost::fusion::at_c<1>(*pos).get() == 8);
        CHECK(boost::fusion::at_c<2>(*pos) == "Tony Coder");

        ++pos;
        ASSERT_EQUAL(boost::fusion::at_c<0>(*pos), 4.5);
        CHECK(boost::fusion::at_c<1>(*pos).is_initialized() == false);
        CHECK(boost::fusion::at_c<2>(*pos) == "Cecile Sharp");

        ++pos;
        ASSERT_EQUAL(boost::fusion::at_c<0>(*pos), 5.0);
        CHECK(boost::fusion::at_c<1>(*pos).is_initialized());
        CHECK(boost::fusion::at_c<1>(*pos).get() == 10);
        CHECK(boost::fusion::at_c<2>(*pos) == "Djhava Ravaa");

        ++pos;
        CHECK(pos == rs.end());
    }
}

#endif // defined(BOOST_VERSION) && BOOST_VERSION >= 103500

// test for boost::gregorian::date
TEST_CASE_METHOD(common_tests, "Boost date", "[core][boost][datetime]")
{
    soci::session sql(backEndFactory_, connectString_);

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        std::tm nov15 = std::tm();
        nov15.tm_year = 105;
        nov15.tm_mon = 10;
        nov15.tm_mday = 15;
        nov15.tm_hour = 0;
        nov15.tm_min = 0;
        nov15.tm_sec = 0;

        sql << "insert into soci_test(tm) values(:tm)", use(nov15);

        boost::gregorian::date bgd;
        sql << "select tm from soci_test", into(bgd);

        CHECK(bgd.year() == 2005);
        CHECK(bgd.month() == 11);
        CHECK(bgd.day() == 15);

        sql << "update soci_test set tm = NULL";
        try
        {
            sql << "select tm from soci_test", into(bgd);
            FAIL("expected exception not thrown");
        }
        catch (soci_error const & e)
        {
            CHECK(e.get_error_message() ==
                "Null value not allowed for this type");
        }
    }

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        boost::gregorian::date bgd(2008, boost::gregorian::May, 5);

        sql << "insert into soci_test(tm) values(:tm)", use(bgd);

        std::tm t = std::tm();
        sql << "select tm from soci_test", into(t);

        CHECK(t.tm_year == 108);
        CHECK(t.tm_mon == 4);
        CHECK(t.tm_mday == 5);
    }

}

#endif // SOCI_HAVE_BOOST

// connection pool - simple sequential test, no multiple threads
TEST_CASE_METHOD(common_tests, "Connection pool", "[core][connection][pool]")
{
    // phase 1: preparation
    const size_t pool_size = 10;
    connection_pool pool(pool_size);

    for (std::size_t i = 0; i != pool_size; ++i)
    {
        session & sql = pool.at(i);
        sql.open(backEndFactory_, connectString_);
    }

    // phase 2: usage
    for (std::size_t i = 0; i != pool_size; ++i)
    {
        // poor man way to lease more than one connection
        soci::session sql_unused1(pool);
        soci::session sql(pool);
        soci::session sql_unused2(pool);
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            char c('a');
            sql << "insert into soci_test(c) values(:c)", use(c);
            sql << "select c from soci_test", into(c);
            CHECK(c == 'a');
        }
    }
}

// Issue 66 - test query transformation callback feature
static std::string no_op_transform(std::string query)
{
    return query;
}

static std::string lower_than_g(std::string query)
{
    return query + " WHERE c < 'g'";
}

struct where_condition : std::unary_function<std::string, std::string>
{
    where_condition(std::string const& where)
        : where_(where)
    {}

    result_type operator()(argument_type query) const
    {
        return query + " WHERE " + where_;
    }

    std::string where_;
};


void run_query_transformation_test(test_context_base const& tc, session& sql)
{
    // create and populate the test table
    auto_table_creator tableCreator(tc.table_creator_1(sql));

    for (char c = 'a'; c <= 'z'; ++c)
    {
        sql << "insert into soci_test(c) values(\'" << c << "\')";
    }

    char const* query = "select count(*) from soci_test";

    // free function, no-op
    {
        sql.set_query_transformation(no_op_transform);
        int count;
        sql << query, into(count);
        CHECK(count == 'z' - 'a' + 1);
    }

    // free function
    {
        sql.set_query_transformation(lower_than_g);
        int count;
        sql << query, into(count);
        CHECK(count == 'g' - 'a');
    }

    // function object with state
    {
        sql.set_query_transformation(where_condition("c > 'g' AND c < 'j'"));
        int count = 0;
        sql << query, into(count);
        CHECK(count == 'j' - 'h');
        count = 0;
        sql.set_query_transformation(where_condition("c > 's' AND c <= 'z'"));
        sql << query, into(count);
        CHECK(count == 'z' - 's');
    }

#if 0
    // lambda is just presented as an example to curious users
    {
        sql.set_query_transformation(
            [](std::string const& query) {
                return query + " WHERE c > 'g' AND c < 'j'";
        });

        int count = 0;
        sql << query, into(count);
        CHECK(count == 'j' - 'h');
    }
#endif

    // prepared statements

    // constant effect (pre-prepare set transformation)
    {
        // set transformation after statement is prepared
        sql.set_query_transformation(lower_than_g);
        // prepare statement
        int count;
        statement st = (sql.prepare << query, into(count));
        // observe transformation effect
        st.execute(true);
        CHECK(count == 'g' - 'a');
        // reset transformation
        sql.set_query_transformation(no_op_transform);
        // observe the same transformation, no-op set above has no effect
        count = 0;
        st.execute(true);
        CHECK(count == 'g' - 'a');
    }

    // no effect (post-prepare set transformation)
    {
        // reset
        sql.set_query_transformation(no_op_transform);

        // prepare statement
        int count;
        statement st = (sql.prepare << query, into(count));
        // set transformation after statement is prepared
        sql.set_query_transformation(lower_than_g);
        // observe no effect of WHERE clause injection
        st.execute(true);
        CHECK(count == 'z' - 'a' + 1);
    }
}

TEST_CASE_METHOD(common_tests, "Query transformation", "[core][query-transform]")
{
    soci::session sql(backEndFactory_, connectString_);
    run_query_transformation_test(tc_, sql);
}

TEST_CASE_METHOD(common_tests, "Query transformation with connection pool", "[core][query-transform][pool]")
{
    // phase 1: preparation
    const size_t pool_size = 10;
    connection_pool pool(pool_size);

    for (std::size_t i = 0; i != pool_size; ++i)
    {
        session & sql = pool.at(i);
        sql.open(backEndFactory_, connectString_);
    }

    soci::session sql(pool);
    run_query_transformation_test(tc_, sql);
}

// Originally, submitted to SQLite3 backend and later moved to common test.
// Test commit b394d039530f124802d06c3b1a969c3117683152
// Author: Mika Fischer <mika.fischer@zoopnet.de>
// Date:   Thu Nov 17 13:28:07 2011 +0100
// Implement get_affected_rows for SQLite3 backend
TEST_CASE_METHOD(common_tests, "Get affected rows", "[core][affected-rows]")
{
    soci::session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_4(sql));
    if (!tableCreator.get())
    {
        std::cout << "test get_affected_rows skipped (function not implemented)" << std::endl;
        return;
    }


    for (int i = 0; i != 10; i++)
    {
        sql << "insert into soci_test(val) values(:val)", use(i);
    }

    int step = 2;
    statement st1 = (sql.prepare <<
        "update soci_test set val = val + :step where val = 5", use(step, "step"));
    st1.execute(true);
    CHECK(st1.get_affected_rows() == 1);

    // attempts to run the query again, no rows should be affected
    st1.execute(true);
    CHECK(st1.get_affected_rows() == 0);

    statement st2 = (sql.prepare <<
        "update soci_test set val = val + 1");
    st2.execute(true);

    CHECK(st2.get_affected_rows() == 10);

    statement st3 = (sql.prepare <<
        "delete from soci_test where val <= 5");
    st3.execute(true);

    CHECK(st3.get_affected_rows() == 5);

    statement st4 = (sql.prepare <<
        "update soci_test set val = val + 1");
    st4.execute(true);

    CHECK(st4.get_affected_rows() == 5);

    std::vector<int> v(5, 0);
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        v[i] = (7 + static_cast<int>(i));
    }

    // test affected rows for bulk operations.
    statement st5 = (sql.prepare <<
        "delete from soci_test where val = :v", use(v));
    st5.execute(true);

    CHECK(st5.get_affected_rows() == 5);

    std::vector<std::string> w(2, "1");
    w[1] = "a"; // this invalid value may cause an exception.
    statement st6 = (sql.prepare <<
        "insert into soci_test(val) values(:val)", use(w));
    try { st6.execute(true); }
    catch(...) {}

    // confirm the partial insertion.
    int val = 0;
    sql << "select count(val) from soci_test", into(val);
    if(val != 0)
    {
        // Notice that some ODBC drivers don't return the number of updated
        // rows at all in the case of partially executed statement like this
        // one, while MySQL ODBC driver wrongly returns 2 affected rows even
        // though only one was actually inserted.
        //
        // So we can't check for "get_affected_rows() == val" here, it would
        // fail in too many cases -- just check that the backend doesn't lie to
        // us about no rows being affected at all (even if it just honestly
        // admits that it has no idea by returning -1).
        CHECK(st6.get_affected_rows() != 0);
    }
}

// test fix for: Backend is not set properly with connection pool (pull #5)
TEST_CASE_METHOD(common_tests, "Backend with connection pool", "[core][pool]")
{
    const size_t pool_size = 1;
    connection_pool pool(pool_size);

    for (std::size_t i = 0; i != pool_size; ++i)
    {
        session & sql = pool.at(i);
        sql.open(backEndFactory_, connectString_);
    }

    soci::session sql(pool);
    sql.reconnect();
    sql.begin(); // no crash expected
}

// issue 67 - Allocated statement backend memory leaks on exception
// If the test runs under memory debugger and it passes, then
// soci::details::statement_impl::backEnd_ must not leak
TEST_CASE_METHOD(common_tests, "Backend memory leak", "[core][leak]")
{
    soci::session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    try
    {
        rowset<row> rs1 = (sql.prepare << "select * from soci_testX");

        // TODO: On Linux, no exception thrown; neither from prepare, nor from execute?
        // soci_odbc_test_postgresql:
        //     /home/travis/build/SOCI/soci/src/core/test/common-tests.h:3505:
        //     void soci::tests::common_tests::test_issue67(): Assertion `!"exception expected"' failed.
        //FAIL("exception expected"); // relax temporarily
    }
    catch (soci_error const &e)
    {
        (void)e;
    }
}

// issue 154 - Calling undefine_and_bind and then define_and_bind causes a leak.
// If the test runs under memory debugger and it passes, then
// soci::details::standard_use_type_backend and vector_use_type_backend must not leak
TEST_CASE_METHOD(common_tests, "Bind memory leak", "[core][leak]")
{
    soci::session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    sql << "insert into soci_test(id) values (1)";
    {
        int id = 1;
        int val = 0;
        statement st(sql);
        st.exchange(use(id));
        st.alloc();
        st.prepare("select id from soci_test where id = :1");
        st.define_and_bind();
        st.undefine_and_bind();
        st.exchange(soci::into(val));
        st.define_and_bind();
        st.execute(true);
        CHECK(val == 1);
    }
    // vector variation
    {
        std::vector<int> ids(1, 2);
        std::vector<int> vals(1, 1);
        int val = 0;
        statement st(sql);
        st.exchange(use(ids));
        st.alloc();
        st.prepare("insert into soci_test(id, val) values (:1, :2)");
        st.define_and_bind();
        st.undefine_and_bind();
        st.exchange(use(vals));
        st.define_and_bind();
        st.execute(true);
        sql << "select val from soci_test where id = 2", into(val);
        CHECK(val == 1);
    }
}

TEST_CASE_METHOD(common_tests, "Insert error", "[core][insert][exception]")
{
    soci::session sql(backEndFactory_, connectString_);

    struct pk_table_creator : table_creator_base
    {
        explicit pk_table_creator(session& sql) : table_creator_base(sql)
        {
            // For some backends (at least Firebird), it is important to
            // execute the DDL statements in a separate transaction, so start
            // one here and commit it before using the new table below.
            sql.begin();
            sql << "create table soci_test("
                        "name varchar(100) not null primary key, "
                        "age integer not null"
                   ")";
            sql.commit();
        }
    } table_creator(sql);

    SECTION("literal SQL queries appear in the error message")
    {
        sql << "insert into soci_test(name, age) values ('John', 74)";
        sql << "insert into soci_test(name, age) values ('Paul', 72)";
        sql << "insert into soci_test(name, age) values ('George', 72)";

        try
        {
            // Oops, this should have been 'Ringo'
            sql << "insert into soci_test(name, age) values ('John', 74)";

            FAIL("exception expected on unique constraint violation not thrown");
        }
        catch (soci_error const &e)
        {
            std::string const msg = e.what();
            CAPTURE(msg);

            CHECK(msg.find("John") != std::string::npos);
        }
    }

    SECTION("SQL queries parameters appear in the error message")
    {
        char const* const names[] = { "John", "Paul", "George", "John", NULL };
        int const ages[] = { 74, 72, 72, 74, 0 };

        std::string name;
        int age;

        statement st = (sql.prepare <<
            "insert into soci_test(name, age) values (:name, :age)",
            use(name), use(age));
        try
        {
            int const *a = ages;
            for (char const* const* n = names; n; ++n, ++a)
            {
                name = *n;
                age = *a;
                st.execute(true);
            }
        }
        catch (soci_error const &e)
        {
            std::string const msg = e.what();
            CAPTURE(msg);

            CHECK(msg.find("John") != std::string::npos);
        }
    }
}

namespace
{

// This is just a helper to avoid duplicating the same code in two sections in
// the test below, it's logically part of it.
void check_for_exception_on_truncation(session& sql)
{
    // As the name column has length 20, inserting a longer string into it
    // shouldn't work, unless we're dealing with a database that doesn't
    // respect column types at all (hello SQLite).
    try
    {
        std::string const long_name("George Raymond Richard Martin");
        sql << "insert into soci_test(name) values(:name)", use(long_name);

        // If insert didn't throw, it should have at least preserved the data
        // (only SQLite does this currently).
        std::string name;
        sql << "select name from soci_test", into(name);
        CHECK(name == long_name);
    }
    catch (soci_error const &)
    {
        // Unfortunately the contents of the message differ too much between
        // the backends (most give an error about value being "too long",
        // Oracle says "too large" while SQL Server (via ODBC) just says that
        // it "would be truncated"), so we can't really check that we received
        // the right error here -- be optimistic and hope that we did.
    }
}

// And another helper for the test below.
void check_for_no_truncation(session& sql)
{
    const std::string str20 = "exactly of length 20";

    sql << "delete from soci_test";

    // Also check that there is no truncation when inserting a string of
    // the same length as the column size.
    CHECK_NOTHROW( (sql << "insert into soci_test(name) values(:s)", use(str20)) );

    std::string s;
    sql << "select name from soci_test", into(s);
    CHECK( s == str20 );
}

} // anonymous namespace

TEST_CASE_METHOD(common_tests, "Truncation error", "[core][insert][truncate][exception]")
{
    soci::session sql(backEndFactory_, connectString_);

    if (tc_.has_silent_truncate_bug(sql))
    {
        WARN("Database is broken and silently truncates input data.");
        return;
    }

    SECTION("Error given for char column")
    {
        struct fixed_name_table_creator : table_creator_base
        {
            fixed_name_table_creator(session& sql)
                : table_creator_base(sql)
            {
                sql << "create table soci_test(name char(20))";
            }
        } tableCreator(sql);

        tc_.on_after_ddl(sql);

        check_for_exception_on_truncation(sql);

        check_for_no_truncation(sql);
    }

    SECTION("Error given for varchar column")
    {
        // Reuse one of the standard tables which has a varchar(20) column.
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        check_for_exception_on_truncation(sql);

        check_for_no_truncation(sql);
    }
}

TEST_CASE_METHOD(common_tests, "Blank padding", "[core][insert][exception]")
{
    soci::session sql(backEndFactory_, connectString_);
    if (!tc_.enable_std_char_padding(sql))
    {
        WARN("This backend doesn't pad CHAR(N) correctly, skipping test.");
        return;
    }

    struct fixed_name_table_creator : table_creator_base
    {
        fixed_name_table_creator(session& sql)
            : table_creator_base(sql)
        {
            sql.begin();
            sql << "create table soci_test(sc char, name char(10), name2 varchar(10))";
            sql.commit();
        }
    } tableCreator(sql);

    std::string test1 = "abcde     ";
    std::string singleChar = "a";
    sql << "insert into soci_test(sc, name,name2) values(:sc,:name,:name2)",
            use(singleChar), use(test1), use(test1);

    std::string sc, tchar,tvarchar;
    sql << "select sc,name,name2 from soci_test",
            into(sc), into(tchar), into(tvarchar);

    // Firebird can pad "a" to "a   " when using UTF-8 encoding.
    CHECK_EQUAL_PADDED(sc, singleChar);
    CHECK_EQUAL_PADDED(tchar, test1);
    CHECK(tvarchar == test1);

    // Check 10-space string - same as inserting empty string since spaces will
    // be padded up to full size of the column.
    test1 = "          ";
    singleChar = " ";
    sql << "update soci_test set sc=:sc, name=:name, name2=:name2",
            use(singleChar), use(test1), use(test1);
    sql << "select sc, name,name2 from soci_test",
            into(sc), into(tchar), into(tvarchar);

    CHECK_EQUAL_PADDED(sc, singleChar);
    CHECK_EQUAL_PADDED(tchar, test1);
    CHECK(tvarchar == test1);
}

TEST_CASE_METHOD(common_tests, "Select without table", "[core][select][dummy_from]")
{
    soci::session sql(backEndFactory_, connectString_);

    int plus17;
    sql << ("select abs(-17)" + sql.get_dummy_from_clause()),
           into(plus17);

    CHECK(plus17 == 17);
}

TEST_CASE_METHOD(common_tests, "String length", "[core][string][length]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    std::string s("123");
    REQUIRE_NOTHROW((
        sql << "insert into soci_test(str) values(:s)", use(s)
    ));

    std::string sout;
    size_t slen;
    REQUIRE_NOTHROW((
        sql << "select str," + tc_.sql_length("str") + " from soci_test",
           into(sout), into(slen)
    ));
    CHECK(slen == 3);
    CHECK(sout.length() == 3);
    CHECK(sout == s);

    sql << "delete from soci_test";


    std::vector<std::string> v;
    v.push_back("Hello");
    v.push_back("");
    v.push_back("whole of varchar(20)");

    REQUIRE_NOTHROW((
        sql << "insert into soci_test(str) values(:s)", use(v)
    ));

    std::vector<std::string> vout(10);
    // Although none of the strings here is really null, Oracle handles the
    // empty string as being null, so to avoid an error about not providing
    // the indicator when retrieving a null value, we must provide it here.
    std::vector<indicator> vind(10);
    std::vector<unsigned int> vlen(10);

    REQUIRE_NOTHROW((
        sql << "select str," + tc_.sql_length("str") + " from soci_test"
               " order by " + tc_.sql_length("str"),
               into(vout, vind), into(vlen)
    ));

    REQUIRE(vout.size() == 3);
    REQUIRE(vlen.size() == 3);

    CHECK(vlen[0] == 0);
    CHECK(vout[0].length() == 0);

    CHECK(vlen[1] == 5);
    CHECK(vout[1].length() == 5);

    CHECK(vlen[2] == 20);
    CHECK(vout[2].length() == 20);
}

// Helper function used in two tests below.
static std::string make_long_xml_string()
{
    std::string s;
    s.reserve(6 + 200*26 + 7);

    s += "<file>";
    for (int i = 0; i != 200; ++i)
    {
        s += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
    s += "</file>";

    return s;
}

TEST_CASE_METHOD(common_tests, "CLOB", "[core][clob]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_clob(sql));
    if (!tableCreator.get())
    {
        WARN("CLOB type not supported by the database, skipping the test.");
        return;
    }

    long_string s1; // empty
    sql << "insert into soci_test(id, s) values (1, :s)", use(s1);

    long_string s2;
    s2.value = "hello";
    sql << "select s from soci_test where id = 1", into(s2);

    CHECK(s2.value.size() == 0);

    s1.value = make_long_xml_string();

    sql << "update soci_test set s = :s where id = 1", use(s1);

    sql << "select s from soci_test where id = 1", into(s2);

    CHECK(s2.value == s1.value);
}

TEST_CASE_METHOD(common_tests, "XML", "[core][xml]")
{
    soci::session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_xml(sql));
    if (!tableCreator.get())
    {
        WARN("XML type not supported by the database, skipping the test.");
        return;
    }

    int id = 1;
    xml_type xml;
    xml.value = make_long_xml_string();

    sql << "insert into soci_test (id, x) values (:1, "
        << tc_.to_xml(":2")
        << ")",
        use(id), use(xml);

    xml_type xml2;

    sql << "select "
        << tc_.from_xml("x")
        << " from soci_test where id = :1",
        into(xml2), use(id);

    // The returned value doesn't need to be identical to the original one as
    // string, only structurally equal as XML. In particular, extra whitespace
    // can be added and this does happen with Oracle, for example, which adds
    // an extra new line, so remove it if it's present.
    if (!xml2.value.empty() && *xml2.value.rbegin() == '\n')
    {
        xml2.value.resize(xml2.value.length() - 1);
    }

    CHECK(xml.value == xml2.value);

    sql << "update soci_test set x = null where id = :1", use(id);

    indicator ind;
    sql << "select "
        << tc_.from_xml("x")
        << " from soci_test where id = :1",
        into(xml2, ind), use(id);

    CHECK(ind == i_null);

    // Inserting malformed XML into an XML column must fail but some backends
    // (e.g. Firebird) don't have real XML support, so exclude them from this
    // test.
    if (tc_.has_real_xml_support())
    {
        xml.value = "<foo></not_foo>";
        CHECK_THROWS_AS(
            (sql << "insert into soci_test(id, x) values (2, "
                        + tc_.to_xml(":1") + ")",
                    use(xml)
            ), soci_error
        );
    }
}

} // namespace test_cases

} // namespace tests

} // namespace soci

#endif // SOCI_COMMON_TESTS_H_INCLUDED
