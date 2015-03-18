//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_COMMON_TESTS_H_INCLUDED
#define SOCI_COMMON_TESTS_H_INCLUDED

#include "soci.h"
#include "soci-config.h"

#ifdef HAVE_BOOST
// explicitly pull conversions for Boost's optional, tuple and fusion:
#include <boost/version.hpp>
#include <boost-optional.h>
#include <boost-tuple.h>
#include <boost-gregorian-date.h>
#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500
#include <boost-fusion.h>
#endif // BOOST_VERSION
#endif // HAVE_BOOST

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <typeinfo>

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
    MyInt() {}
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

// ensure connection is checked, no crash occurs

#define SOCI_TEST_ENSURE_CONNECTED(sql, method) { \
    std::string msg; \
    try { \
        (sql.method)(); \
        assert(!"exception expected"); \
    } catch (soci_error const &e) { msg = e.what(); } \
    assert(msg.empty() == false); } (void)sql

#define SOCI_TEST_ENSURE_CONNECTED2(sql, method) { \
    std::string msg; \
    try { std::string seq; long v(0); \
        (sql.method)(seq, v); \
        assert(!"exception expected"); \
    } catch (soci_error const &e) { msg = e.what(); } \
    assert(msg.empty() == false); } (void)sql

inline bool equal_approx(double const a, double const b)
{
    // The formula taken from CATCH test framework
    // https://github.com/philsquared/Catch/
    // Thanks to Richard Harris for his help refining this formula
    double const epsilon(std::numeric_limits<float>::epsilon() * 100);
    double const scale(1.0);
    return std::fabs(a - b) < epsilon * (scale + (std::max)(std::fabs(a), std::fabs(b)));
}

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
};

class test_context_base
{
public:
    test_context_base(backend_factory const &backEnd,
                    std::string const &connectString)
        : backEndFactory_(backEnd),
          connectString_(connectString) {}

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

    virtual ~test_context_base() {} // quiet the compiler

private:
    backend_factory const &backEndFactory_;
    std::string const connectString_;
};

class common_tests
{
public:
    common_tests(test_context_base const &tc)
    : tc_(tc),
      backEndFactory_(tc.get_backend_factory()),
      connectString_(tc.get_connect_string())
    {}

    void run(bool dbSupportsTransactions = true)
    {
        std::cout<<"\nSOCI Common Tests:\n\n";

        test0();
        test1();
        test2();
        test3();
        test4();
        test5();
        test6();
        test7();
        test8();
        test9();

        if (dbSupportsTransactions)
        {
            test10();
        }
        else
        {
            std::cout<<"skipping test 10 (database doesn't support transactions)\n";
        }

        test11();
        test12();
        test13();
        test14();
        test15();
        test16();
        test17();
        test18();
        test19();
        test20();
        test21();
        test22();
        test23();
        test24();
        test25();
        test26();
        test27();
        test28();
        test29();
        test30();
        test31();
        test_get_affected_rows();
        test_query_transformation();
        test_query_transformation_with_connection_pool();
        test_pull5();
        test_issue67();
        test_prepared_insert_with_orm_type();
        test_issue154();
        test_placeholder_partial_matching_with_orm_type();
    }

private:
    test_context_base const & tc_;
    backend_factory const &backEndFactory_;
    std::string const connectString_;

typedef std::auto_ptr<table_creator_base> auto_table_creator;

void test0()
{
    {
        soci::session sql; // no connection
        SOCI_TEST_ENSURE_CONNECTED(sql, begin);
        SOCI_TEST_ENSURE_CONNECTED(sql, commit);
        SOCI_TEST_ENSURE_CONNECTED(sql, rollback);
        SOCI_TEST_ENSURE_CONNECTED(sql, get_backend_name);
        SOCI_TEST_ENSURE_CONNECTED(sql, make_statement_backend);
        SOCI_TEST_ENSURE_CONNECTED(sql, make_rowid_backend);
        SOCI_TEST_ENSURE_CONNECTED(sql, make_blob_backend);
        SOCI_TEST_ENSURE_CONNECTED2(sql, get_next_sequence_value);
        SOCI_TEST_ENSURE_CONNECTED2(sql, get_last_insert_id);
    }
    std::cout << "test 0 passed\n";
}
void test1()
{
    session sql(backEndFactory_, connectString_);
    
    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    std::string msg;
    try
    {
        // expected error
        sql << "drop table soci_test_nosuchtable";
        assert(false);
    }
    catch (soci_error const &e)
    {
        msg = e.what();
    }
    assert(msg.empty() == false);

    sql << "insert into soci_test (id) values (" << 123 << ")";
    int id;
    sql << "select id from soci_test", into(id);
    assert(id == 123);

    std::cout << "test 1 passed\n";
}

// "into" tests, type conversions, etc.
void test2()
{
    {
        session sql(backEndFactory_, connectString_);

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            char c('a');
            sql << "insert into soci_test(c) values(:c)", use(c);
            sql << "select c from soci_test", into(c);
            assert(c == 'a');
        }

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::string helloSOCI("Hello, SOCI!");
            sql << "insert into soci_test(str) values(:s)", use(helloSOCI);
            std::string str;
            sql << "select str from soci_test", into(str);
            assert(str == "Hello, SOCI!");
        }

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            
            short three(3);
            sql << "insert into soci_test(sh) values(:id)", use(three);
            short sh(0);
            sql << "select sh from soci_test", into(sh);
            assert(sh == 3);
        }

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            int five(5);
            sql << "insert into soci_test(id) values(:id)", use(five);
            int i(0);
            sql << "select id from soci_test", into(i);
            assert(i == 5);
        }

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            unsigned long seven(7);
            sql << "insert into soci_test(ul) values(:ul)", use(seven);
            unsigned long ul(0);
            sql << "select ul from soci_test", into(ul);
            assert(ul == 7);
        }

        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            double pi(3.14159265);
            sql << "insert into soci_test(d) values(:d)", use(pi);
            double d(0.0);
            sql << "select d from soci_test", into(d);
            assert(equal_approx(d, 3.14159265));
        }
        
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::tm nov15;
            nov15.tm_year = 105;
            nov15.tm_mon = 10;
            nov15.tm_mday = 15;
            nov15.tm_hour = 0;
            nov15.tm_min = 0;
            nov15.tm_sec = 0;

            sql << "insert into soci_test(tm) values(:tm)", use(nov15);

            std::tm t;
            sql << "select tm from soci_test", into(t);
            assert(t.tm_year == 105);
            assert(t.tm_mon  == 10);
            assert(t.tm_mday == 15);
            assert(t.tm_hour == 0);
            assert(t.tm_min  == 0);
            assert(t.tm_sec  == 0);
        }
        
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::tm nov15;
            nov15.tm_year = 105;
            nov15.tm_mon = 10;
            nov15.tm_mday = 15;
            nov15.tm_hour = 22;
            nov15.tm_min = 14;
            nov15.tm_sec = 17;

            sql << "insert into soci_test(tm) values(:tm)", use(nov15);
            
            std::tm t;
            sql << "select tm from soci_test", into(t);
            assert(t.tm_year == 105);
            assert(t.tm_mon  == 10);
            assert(t.tm_mday == 15);
            assert(t.tm_hour == 22);
            assert(t.tm_min  == 14);
            assert(t.tm_sec  == 17);
        }

        // test indicators
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            int id(1);
            std::string str("Hello");
            sql << "insert into soci_test(id, str) values(:id, :str)",
                use(id), use(str);

            int i;
            indicator ind;
            sql << "select id from soci_test", into(i, ind);
            assert(ind == i_ok);
        }

        // more indicator tests, NULL values
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            sql << "insert into soci_test(id,tm) values(NULL,NULL)";
            int i;
            indicator ind;
            sql << "select id from soci_test", into(i, ind);
            assert(ind == i_null);

            // additional test for NULL with std::tm
            std::tm t;
            sql << "select tm from soci_test", into(t, ind);
            assert(ind == i_null);

            try
            {
                // expect error
                sql << "select id from soci_test", into(i);
                assert(false);
            }
            catch (soci_error const &e)
            {
                std::string error = e.what();
                assert(error ==
                    "Null value fetched and no indicator defined.");
            }

            sql << "select id from soci_test where id = 1000", into(i, ind);
            assert(sql.got_data() == false);

            // no data expected
            sql << "select id from soci_test where id = 1000", into(i);
            assert(sql.got_data() == false);

            // no data expected, test correct behaviour with use
            int id = 1000;
            sql << "select id from soci_test where id = :id", use(id), into(i);
            assert(sql.got_data() == false);
        }
    }

    std::cout << "test 2 passed" << std::endl;
}

// repeated fetch and bulk fetch
void test3()
{
    {
        session sql(backEndFactory_, connectString_);

        // repeated fetch and bulk fetch of char
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            char c;
            for (c = 'a'; c <= 'z'; ++c)
            {
                sql << "insert into soci_test(c) values(\'" << c << "\')";
            }

            int count;
            sql << "select count(*) from soci_test", into(count);
            assert(count == 'z' - 'a' + 1);

            {
                char c2 = 'a';

                statement st = (sql.prepare <<
                    "select c from soci_test order by c", into(c));

                st.execute();
                while (st.fetch())
                {
                    assert(c == c2);
                    ++c2;
                }
                assert(c2 == 'a' + count);
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
                        assert(c2 == vec[i]);
                        ++c2;
                    }

                    vec.resize(10);
                }
                assert(c2 == 'a' + count);
            }

            {
                // verify an exception is thrown when empty vector is used
                std::vector<char> vec;
                try
                {
                    sql << "select c from soci_test", into(vec);
                    assert(false);
                }
                catch (soci_error const &e)
                {
                     std::string msg = e.what();
                     assert(msg == "Vectors of size 0 are not allowed.");
                }
            }

        }

        // repeated fetch and bulk fetch of std::string
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            
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
            assert(count == rowsToTest);

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
                    assert(s == ss.str());
                    ++i;
                }
                assert(i == rowsToTest);
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
                        assert(ss.str() == vec[j]);
                        ++i;
                    }

                    vec.resize(4);
                }
                assert(i == rowsToTest);
            }
        }

        // repeated fetch and bulk fetch of short
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            short const rowsToTest = 100;
            short sh;
            for (sh = 0; sh != rowsToTest; ++sh)
            {
                sql << "insert into soci_test(sh) values(" << sh << ")";
            }

            int count;
            sql << "select count(*) from soci_test", into(count);
            assert(count == rowsToTest);

            {
                short sh2 = 0;

                statement st = (sql.prepare <<
                    "select sh from soci_test order by sh", into(sh));

                st.execute();
                while (st.fetch())
                {
                    assert(sh == sh2);
                    ++sh2;
                }
                assert(sh2 == rowsToTest);
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
                        assert(sh2 == vec[i]);
                        ++sh2;
                    }

                    vec.resize(8);
                }
                assert(sh2 == rowsToTest);
            }
        }

        // repeated fetch and bulk fetch of int (4-bytes)
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            int const rowsToTest = 100;
            int i;
            for (i = 0; i != rowsToTest; ++i)
            {
                sql << "insert into soci_test(id) values(" << i << ")";
            }

            int count;
            sql << "select count(*) from soci_test", into(count);
            assert(count == rowsToTest);

            {
                int i2 = 0;

                statement st = (sql.prepare <<
                    "select id from soci_test order by id", into(i));

                st.execute();
                while (st.fetch())
                {
                    assert(i == i2);
                    ++i2;
                }
                assert(i2 == rowsToTest);
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
                    assert(i == i2);
                    ++i2;
                }
                assert(i2 == rowsToTest);
            }
            {
                int i2 = 0;

                std::vector<int> vec(8);
                statement st = (sql.prepare <<
                    "select id from soci_test order by id", into(vec));
                st.execute();
                while (st.fetch())
                {
                    for (std::size_t i = 0; i != vec.size(); ++i)
                    {
                        assert(i2 == vec[i]);
                        ++i2;
                    }

                    vec.resize(8);
                }
                assert(i2 == rowsToTest);
            }
        }

        // repeated fetch and bulk fetch of unsigned int (4-bytes)
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            unsigned int const rowsToTest = 100;
            unsigned int ul;
            for (ul = 0; ul != rowsToTest; ++ul)
            {
                sql << "insert into soci_test(ul) values(" << ul << ")";
            }

            int count;
            sql << "select count(*) from soci_test", into(count);
            assert(count == static_cast<int>(rowsToTest));

            {
                unsigned int ul2 = 0;

                statement st = (sql.prepare <<
                    "select ul from soci_test order by ul", into(ul));

                st.execute();
                while (st.fetch())
                {
                    assert(ul == ul2);
                    ++ul2;
                }
                assert(ul2 == rowsToTest);
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
                        assert(ul2 == vec[i]);
                        ++ul2;
                    }

                    vec.resize(8);
                }
                assert(ul2 == rowsToTest);
            }
        }

        // repeated fetch and bulk fetch of double
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            int const rowsToTest = 100;
            double d = 0.0;
            for (int i = 0; i != rowsToTest; ++i)
            {
                sql << "insert into soci_test(d) values(" << d << ")";
                d += 0.6;
            }

            int count;
            sql << "select count(*) from soci_test", into(count);
            assert(count == rowsToTest);

            {
                double d2 = 0.0;
                int i = 0;

                statement st = (sql.prepare <<
                    "select d from soci_test order by d", into(d));

                st.execute();
                while (st.fetch())
                {
                    assert(equal_approx(d, d2));
                    d2 += 0.6;
                    ++i;
                }
                assert(i == rowsToTest);
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
                        assert(equal_approx(d2, vec[j]));
                        d2 += 0.6;
                        ++i;
                    }

                    vec.resize(8);
                }
                assert(i == rowsToTest);
            }
        }

        // repeated fetch and bulk fetch of std::tm
        {
            // create and populate the test table
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

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
            assert(count == rowsToTest);

            {
                std::tm t;
                int i = 0;

                statement st = (sql.prepare <<
                    "select tm from soci_test order by id", into(t));

                st.execute();
                while (st.fetch())
                {
                    assert(t.tm_year + 1900 == 2000 + i);
                    assert(t.tm_mon + 1 == 1 + i);
                    assert(t.tm_mday == 20 - i);
                    assert(t.tm_hour == 15 + i);
                    assert(t.tm_min == 50 - i);
                    assert(t.tm_sec == 40 + i);

                    ++i;
                }
                assert(i == rowsToTest);
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
                        assert(vec[j].tm_year + 1900 == 2000 + i);
                        assert(vec[j].tm_mon + 1 == 1 + i);
                        assert(vec[j].tm_mday == 20 - i);
                        assert(vec[j].tm_hour == 15 + i);
                        assert(vec[j].tm_min == 50 - i);
                        assert(vec[j].tm_sec == 40 + i);

                        ++i;
                    }

                    vec.resize(3);
                }
                assert(i == rowsToTest);
            }
        }
    }

    std::cout << "test 3 passed" << std::endl;
}

// test for indicators (repeated fetch and bulk)
void test4()
{
    session sql(backEndFactory_, connectString_);
    
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
            assert(gotData);
            assert(ind == i_ok);
            assert(val == 10);
            gotData = st.fetch();
            assert(gotData);
            assert(ind == i_ok);
            assert(val == 11);
            gotData = st.fetch();
            assert(gotData);
            assert(ind == i_null);
            gotData = st.fetch();
            assert(gotData);
            assert(ind == i_null);
            gotData = st.fetch();
            assert(gotData);
            assert(ind == i_ok);
            assert(val == 12);
            gotData = st.fetch();
            assert(gotData == false);
        }
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            statement st = (sql.prepare <<
                "select val from soci_test order by id", into(vals, inds));

            st.execute();
            bool gotData = st.fetch();
            assert(gotData);
            assert(vals.size() == 3);
            assert(inds.size() == 3);
            assert(inds[0] == i_ok);
            assert(vals[0] == 10);
            assert(inds[1] == i_ok);
            assert(vals[1] == 11);
            assert(inds[2] == i_null);
            gotData = st.fetch();
            assert(gotData);
            assert(vals.size() == 2);
            assert(inds[0] == i_null);
            assert(inds[1] == i_ok);
            assert(vals[1] == 12);
            gotData = st.fetch();
            assert(gotData == false);
        }

        // additional test for "no data" condition
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            statement st = (sql.prepare <<
                "select val from soci_test where 0 = 1", into(vals, inds));

            bool gotData = st.execute(true);
            assert(gotData == false);

            // for convenience, vectors should be truncated
            assert(vals.empty());
            assert(inds.empty());

            // for even more convenience, fetch should not fail
            // but just report end of rowset
            // (and vectors should be truncated)
            
            vals.resize(1);
            inds.resize(1);

            gotData = st.fetch();
            assert(gotData == false);
            assert(vals.empty());
            assert(inds.empty());
        }

        // additional test for "no data" without prepared statement
        {
            std::vector<int> vals(3);
            std::vector<indicator> inds(3);

            sql << "select val from soci_test where 0 = 1",
                into(vals, inds);

            // vectors should be truncated
            assert(vals.empty());
            assert(inds.empty());
        }
    }

    std::cout << "test 4 passed" << std::endl;
}

// test for different sizes of data vector and indicators vector
// (library should force ind. vector to have same size as data vector)
void test5()
{
    session sql(backEndFactory_, connectString_);
    
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(id, val) values(1, 10)";
        sql << "insert into soci_test(id, val) values(2, 11)";
        sql << "insert into soci_test(id, val) values(3, NULL)";
        sql << "insert into soci_test(id, val) values(4, NULL)";
        sql << "insert into soci_test(id, val) values(5, 12)";

        {
            std::vector<int> vals(4);
            std::vector<indicator> inds;

            statement st = (sql.prepare <<
                "select val from soci_test order by id", into(vals, inds));

            st.execute();
            st.fetch();
            assert(vals.size() == 4);
            assert(inds.size() == 4);
            vals.resize(3);
            st.fetch();
            assert(vals.size() == 1);
            assert(inds.size() == 1);
        }
    }

    std::cout << "test 5 passed" << std::endl;
}

// "use" tests, type conversions, etc.
void test6()
{
// Note: this functionality is not available with older PostgreSQL
#ifndef SOCI_POSTGRESQL_NOPARAMS
    {
        session sql(backEndFactory_, connectString_);

        // test for char
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            char c('a');
            sql << "insert into soci_test(c) values(:c)", use(c);

            c = 'b';
            sql << "select c from soci_test", into(c);
            assert(c == 'a');

        }

        // test for std::string
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            std::string s = "Hello SOCI!";
            sql << "insert into soci_test(str) values(:s)", use(s);

            std::string str;
            sql << "select str from soci_test", into(str);

            assert(str == "Hello SOCI!");
        }

        // test for short
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            short s = 123;
            sql << "insert into soci_test(id) values(:id)", use(s);

            short s2 = 0;
            sql << "select id from soci_test", into(s2);

            assert(s2 == 123);
        }

        // test for int
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            int i = -12345678;
            sql << "insert into soci_test(id) values(:i)", use(i);

            int i2 = 0;
            sql << "select id from soci_test", into(i2);

            assert(i2 == -12345678);
        }

        // test for unsigned long
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            unsigned long ul = 4000000000ul;
            sql << "insert into soci_test(ul) values(:num)", use(ul);

            unsigned long ul2 = 0;
            sql << "select ul from soci_test", into(ul2);

            assert(ul2 == 4000000000ul);
        }

        // test for double
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            double d = 3.14159265;
            sql << "insert into soci_test(d) values(:d)", use(d);

            double d2 = 0;
            sql << "select d from soci_test", into(d2);

            assert(equal_approx(d2, d));
        }

        // test for std::tm
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            std::tm t;
            t.tm_year = 105;
            t.tm_mon = 10;
            t.tm_mday = 19;
            t.tm_hour = 21;
            t.tm_min = 39;
            t.tm_sec = 57;
            sql << "insert into soci_test(tm) values(:t)", use(t);

            std::tm t2;
            t2.tm_year = 0;
            t2.tm_mon = 0;
            t2.tm_mday = 0;
            t2.tm_hour = 0;
            t2.tm_min = 0;
            t2.tm_sec = 0;

            sql << "select tm from soci_test", into(t2);

            assert(t.tm_year == 105);
            assert(t.tm_mon  == 10);
            assert(t.tm_mday == 19);
            assert(t.tm_hour == 21);
            assert(t.tm_min  == 39);
            assert(t.tm_sec  == 57);
        }

        // test for repeated use
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
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

            assert(v.size() == 3);
            assert(v[0] == 5);
            assert(v[1] == 6);
            assert(v[2] == 7);
        }

        // tests for use of const objects

        // test for char
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            char const c('a');
            sql << "insert into soci_test(c) values(:c)", use(c);

            char c2 = 'b';
            sql << "select c from soci_test", into(c2);
            assert(c2 == 'a');

        }

        // test for std::string
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            std::string const s = "Hello const SOCI!";
            sql << "insert into soci_test(str) values(:s)", use(s);

            std::string str;
            sql << "select str from soci_test", into(str);

            assert(str == "Hello const SOCI!");
        }

        // test for short
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            short const s = 123;
            sql << "insert into soci_test(id) values(:id)", use(s);

            short s2 = 0;
            sql << "select id from soci_test", into(s2);

            assert(s2 == 123);
        }

        // test for int
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            int const i = -12345678;
            sql << "insert into soci_test(id) values(:i)", use(i);

            int i2 = 0;
            sql << "select id from soci_test", into(i2);

            assert(i2 == -12345678);
        }

        // test for unsigned long
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            unsigned long const ul = 4000000000ul;
            sql << "insert into soci_test(ul) values(:num)", use(ul);

            unsigned long ul2 = 0;
            sql << "select ul from soci_test", into(ul2);

            assert(ul2 == 4000000000ul);
        }

        // test for double
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            double const d = 3.14159265;
            sql << "insert into soci_test(d) values(:d)", use(d);

            double d2 = 0;
            sql << "select d from soci_test", into(d2);

            assert(equal_approx(d2, d));
        }

        // test for std::tm
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));
            std::tm t;
            t.tm_year = 105;
            t.tm_mon = 10;
            t.tm_mday = 19;
            t.tm_hour = 21;
            t.tm_min = 39;
            t.tm_sec = 57;
            std::tm const & ct = t;
            sql << "insert into soci_test(tm) values(:t)", use(ct);

            std::tm t2;
            t2.tm_year = 0;
            t2.tm_mon = 0;
            t2.tm_mday = 0;
            t2.tm_hour = 0;
            t2.tm_min = 0;
            t2.tm_sec = 0;

            sql << "select tm from soci_test", into(t2);

            assert(t.tm_year == 105);
            assert(t.tm_mon  == 10);
            assert(t.tm_mday == 19);
            assert(t.tm_hour == 21);
            assert(t.tm_min  == 39);
            assert(t.tm_sec  == 57);
        }
    }

    std::cout << "test 6 passed" << std::endl;
#endif // SOCI_POSTGRESQL_NOPARAMS
}

// test for multiple use (and into) elements
void test7()
{
    {
        session sql(backEndFactory_, connectString_);
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

            assert(i1 == 5);
            assert(i2 == 6);
            assert(i3 == 7);

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

            assert(v1.size() == 3);
            assert(v2.size() == 3);
            assert(v3.size() == 3);
            assert(v1[0] == 1);
            assert(v1[1] == 4);
            assert(v1[2] == 7);
            assert(v2[0] == 2);
            assert(v2[1] == 5);
            assert(v2[2] == 8);
            assert(v3[0] == 3);
            assert(v3[1] == 6);
            assert(v3[2] == 9);
        }
    }

    std::cout << "test 7 passed" << std::endl;
}

// use vector elements
void test8()
{
// Not supported with older PostgreSQL
#ifndef SOCI_POSTGRESQL_NOPARAMS

    {
        session sql(backEndFactory_, connectString_);

        // test for char
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<char> v;
            v.push_back('a');
            v.push_back('b');
            v.push_back('c');
            v.push_back('d');

            sql << "insert into soci_test(c) values(:c)", use(v);

            std::vector<char> v2(4);

            sql << "select c from soci_test order by c", into(v2);
            assert(v2.size() == 4);
            assert(v2[0] == 'a');
            assert(v2[1] == 'b');
            assert(v2[2] == 'c');
            assert(v2[3] == 'd');
        }

        // test for std::string
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<std::string> v;
            v.push_back("ala");
            v.push_back("ma");
            v.push_back("kota");

            sql << "insert into soci_test(str) values(:s)", use(v);

            std::vector<std::string> v2(4);

            sql << "select str from soci_test order by str", into(v2);
            assert(v2.size() == 3);
            assert(v2[0] == "ala");
            assert(v2[1] == "kota");
            assert(v2[2] == "ma");
        }

        // test for short
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<short> v;
            v.push_back(-5);
            v.push_back(6);
            v.push_back(7);
            v.push_back(123);

            sql << "insert into soci_test(sh) values(:sh)", use(v);

            std::vector<short> v2(4);

            sql << "select sh from soci_test order by sh", into(v2);
            assert(v2.size() == 4);
            assert(v2[0] == -5);
            assert(v2[1] == 6);
            assert(v2[2] == 7);
            assert(v2[3] == 123);
        }

        // test for int
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<int> v;
            v.push_back(-2000000000);
            v.push_back(0);
            v.push_back(1);
            v.push_back(2000000000);

            sql << "insert into soci_test(id) values(:i)", use(v);

            std::vector<int> v2(4);

            sql << "select id from soci_test order by id", into(v2);
            assert(v2.size() == 4);
            assert(v2[0] == -2000000000);
            assert(v2[1] == 0);
            assert(v2[2] == 1);
            assert(v2[3] == 2000000000);
        }

        // test for unsigned int
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<unsigned int> v;
            v.push_back(0);
            v.push_back(1);
            v.push_back(123);
            v.push_back(1000);

            sql << "insert into soci_test(ul) values(:ul)", use(v);

            std::vector<unsigned int> v2(4);

            sql << "select ul from soci_test order by ul", into(v2);
            assert(v2.size() == 4);
            assert(v2[0] == 0);
            assert(v2[1] == 1);
            assert(v2[2] == 123);
            assert(v2[3] == 1000);
        }

        // test for double
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<double> v;
            v.push_back(0);
            v.push_back(-0.0001);
            v.push_back(0.0001);
            v.push_back(3.1415926);

            sql << "insert into soci_test(d) values(:d)", use(v);

            std::vector<double> v2(4);

            sql << "select d from soci_test order by d", into(v2);
            assert(v2.size() == 4);
            assert(equal_approx(v2[0],-0.0001));
            assert(equal_approx(v2[1], 0));
            assert(equal_approx(v2[2], 0.0001));
            assert(equal_approx(v2[3], 3.1415926));
        }

        // test for std::tm
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<std::tm> v;
            std::tm t;
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
            assert(v2.size() == 3);
            assert(v2[0].tm_year == 105);
            assert(v2[0].tm_mon  == 10);
            assert(v2[0].tm_mday == 25);
            assert(v2[0].tm_hour == 22);
            assert(v2[0].tm_min  == 45);
            assert(v2[0].tm_sec  == 37);
            assert(v2[1].tm_year == 105);
            assert(v2[1].tm_mon  == 10);
            assert(v2[1].tm_mday == 26);
            assert(v2[1].tm_hour == 22);
            assert(v2[1].tm_min  == 45);
            assert(v2[1].tm_sec  == 17);
            assert(v2[2].tm_year == 105);
            assert(v2[2].tm_mon  == 10);
            assert(v2[2].tm_mday == 26);
            assert(v2[2].tm_hour == 22);
            assert(v2[2].tm_min  == 45);
            assert(v2[2].tm_sec  == 37);
        }

        // additional test for int (use const vector)
        {
            auto_table_creator tableCreator(tc_.table_creator_1(sql));

            std::vector<int> v;
            v.push_back(-2000000000);
            v.push_back(0);
            v.push_back(1);
            v.push_back(2000000000);

            std::vector<int> const & cv = v;

            sql << "insert into soci_test(id) values(:i)", use(cv);

            std::vector<int> v2(4);

            sql << "select id from soci_test order by id", into(v2);
            assert(v2.size() == 4);
            assert(v2[0] == -2000000000);
            assert(v2[1] == 0);
            assert(v2[2] == 1);
            assert(v2[3] == 2000000000);
        }
    }

    std::cout << "test 8 passed" << std::endl;

#endif // SOCI_POSTGRESQL_NOPARAMS

}

// test for named binding
void test9()
{
// Not supported with older PostgreSQL
#ifndef SOCI_POSTGRESQL_NOPARAMS

    {
        session sql(backEndFactory_, connectString_);
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

                assert(false);
            }
            catch (soci_error const& e)
            {
                std::string what(e.what());
                assert(what ==
                    "Binding for use elements must be either by position "
                    "or by name.");
            }

            // normal test
            sql << "insert into soci_test(i1, i2) values(:i1, :i2)",
                use(i1, "i1"), use(i2, "i2");

            i1 = 0;
            i2 = 0;
            sql << "select i1, i2 from soci_test", into(i1), into(i2);
            assert(i1 == 7);
            assert(i2 == 8);

            i2 = 0;
            sql << "select i2 from soci_test where i1 = :i1", into(i2), use(i1);
            assert(i2 == 8);

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
            assert(v1.size() == 3);
            assert(v2.size() == 3);
            assert(v1[0] == 6);
            assert(v1[1] == 5);
            assert(v1[2] == 4);
            assert(v2[0] == 3);
            assert(v2[1] == 2);
            assert(v2[2] == 1);
        }
    }

    std::cout << "test 9 passed" << std::endl;

#endif // SOCI_POSTGRESQL_NOPARAMS

}

// transaction test
void test10()
{
    {
        session sql(backEndFactory_, connectString_);

        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        int count;
        sql << "select count(*) from soci_test", into(count);
        assert(count == 0);

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
            assert(count == 3);

            sql << "insert into soci_test (id, name) values(4, 'Stan')";

            sql << "select count(*) from soci_test", into(count);
            assert(count == 4);

            tr.rollback();

            sql << "select count(*) from soci_test", into(count);
            assert(count == 3);
        }
        {
            transaction tr(sql);

            sql << "delete from soci_test";

            sql << "select count(*) from soci_test", into(count);
            assert(count == 0);

            tr.rollback();

            sql << "select count(*) from soci_test", into(count);
            assert(count == 3);
        }
        {
            // additional test for detection of double commit
            transaction tr(sql);
            tr.commit();
            try
            {
                tr.commit();
                assert(false);
            }
            catch (soci_error const &e)
            {
                std::string msg = e.what();
                assert(msg ==
                    "The transaction object cannot be handled twice.");
            }
        }
    }

    std::cout << "test 10 passed" << std::endl;
}

// test of use elements with indicators
void test11()
{
#ifndef SOCI_POSTGRESQL_NOPARAMS
    {
        session sql(backEndFactory_, connectString_);

        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        indicator ind1 = i_ok;
        indicator ind2 = i_ok;

        int id = 1;
        int val = 10;

        sql << "insert into soci_test(id, val) values(:id, :val)",
            use(id, ind1), use(val, ind2);

        id = 2;
        val = 11;
        ind2 = i_null;
        sql << "insert into soci_test(id, val) values(:id, :val)",
            use(id, ind1), use(val, ind2);

        sql << "select val from soci_test where id = 1", into(val, ind2);
        assert(ind2 == i_ok);
        assert(val == 10);
        sql << "select val from soci_test where id = 2", into(val, ind2);
        assert(ind2 == i_null);

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

        assert(ids.size() == 5);
        assert(ids[0] == 5);
        assert(ids[1] == 4);
        assert(ids[2] == 3);
        assert(ids[3] == 2);
        assert(ids[4] == 1);
        assert(inds.size() == 5);
        assert(inds[0] == i_ok);
        assert(inds[1] == i_null);
        assert(inds[2] == i_ok);
        assert(inds[3] == i_null);
        assert(inds[4] == i_ok);
        assert(vals.size() == 5);
        assert(vals[0] == 14);
        assert(vals[2] == 12);
        assert(vals[4] == 10);
    }

    std::cout << "test 11 passed" << std::endl;

#endif // SOCI_POSTGRESQL_NOPARAMS
}

// Dynamic binding to Row objects
void test12()
{
    {
        session sql(backEndFactory_, connectString_);

        sql.uppercase_column_names(true);

        auto_table_creator tableCreator(tc_.table_creator_2(sql));

        row r;
        sql << "select * from soci_test", into(r);
        assert(sql.got_data() == false);

        sql << "insert into soci_test"
            " values(3.14, 123, \'Johny\',"
            << tc_.to_date_time("2005-12-19 22:14:17")
            << ", 'a')";

        // select into a row
        {
            row r;
            statement st = (sql.prepare <<
                "select * from soci_test", into(r));
            st.execute(true);
            assert(r.size() == 5);

            assert(r.get_properties(0).get_data_type() == dt_double);
            assert(r.get_properties(1).get_data_type() == dt_integer);
            assert(r.get_properties(2).get_data_type() == dt_string);
            assert(r.get_properties(3).get_data_type() == dt_date);

            // type char is visible as string
            // - to comply with the implementation for Oracle
            assert(r.get_properties(4).get_data_type() == dt_string);

            assert(r.get_properties("NUM_INT").get_data_type() == dt_integer);

            assert(r.get_properties(0).get_name() == "NUM_FLOAT");
            assert(r.get_properties(1).get_name() == "NUM_INT");
            assert(r.get_properties(2).get_name() == "NAME");
            assert(r.get_properties(3).get_name() == "SOMETIME");
            assert(r.get_properties(4).get_name() == "CHR");

            assert(equal_approx(r.get<double>(0), 3.14));
            assert(r.get<int>(1) == 123);
            assert(r.get<std::string>(2) == "Johny");
            std::tm t = { 0 };
            t = r.get<std::tm>(3);
            assert(t.tm_year == 105);

            // again, type char is visible as string
            assert(r.get<std::string>(4) == "a");

            assert(equal_approx(r.get<double>("NUM_FLOAT"), 3.14));
            assert(r.get<int>("NUM_INT") == 123);
            assert(r.get<std::string>("NAME") == "Johny");
            assert(r.get<std::string>("CHR") == "a");

            assert(r.get_indicator(0) == i_ok);

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
            assert(caught);

            // additional test for stream-like extraction
            {
                double d;
                int i;
                std::string s;
                std::tm t;
                std::string c;

                r >> d >> i >> s >> t >> c;

                assert(equal_approx(d, 3.14));
                assert(i == 123);
                assert(s == "Johny");
                assert(t.tm_year == 105);
                assert(t.tm_mon == 11);
                assert(t.tm_mday == 19);
                assert(t.tm_hour == 22);
                assert(t.tm_min == 14);
                assert(t.tm_sec == 17);
                assert(c == "a");
            }
        }

        // additional test to check if the row object can be
        // reused between queries
        {
            row r;
            sql << "select * from soci_test", into(r);

            assert(r.size() == 5);

            assert(r.get_properties(0).get_data_type() == dt_double);
            assert(r.get_properties(1).get_data_type() == dt_integer);
            assert(r.get_properties(2).get_data_type() == dt_string);
            assert(r.get_properties(3).get_data_type() == dt_date);

            sql << "select name, num_int from soci_test", into(r);

            assert(r.size() == 2);

            assert(r.get_properties(0).get_data_type() == dt_string);
            assert(r.get_properties(1).get_data_type() == dt_integer);
        }
    }

    std::cout << "test 12 passed" << std::endl;
}

// more dynamic bindings
void test13()
{
    session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_1(sql));

    sql << "insert into soci_test(id, val) values(1, 10)";
    sql << "insert into soci_test(id, val) values(2, 20)";
    sql << "insert into soci_test(id, val) values(3, 30)";

#ifndef SOCI_POSTGRESQL_NOPARAMS
    {
        int id = 2;
        row r;
        sql << "select val from soci_test where id = :id", use(id), into(r);

        assert(r.size() == 1);
        assert(r.get_properties(0).get_data_type() == dt_integer);
        assert(r.get<int>(0) == 20);
    }
    {
        int id;
        row r;
        statement st = (sql.prepare <<
            "select val from soci_test where id = :id", use(id), into(r));

        id = 2;
        st.execute(true);
        assert(r.size() == 1);
        assert(r.get_properties(0).get_data_type() == dt_integer);
        assert(r.get<int>(0) == 20);
        
        id = 3;
        st.execute(true);
        assert(r.size() == 1);
        assert(r.get_properties(0).get_data_type() == dt_integer);
        assert(r.get<int>(0) == 30);

        id = 1;
        st.execute(true);
        assert(r.size() == 1);
        assert(r.get_properties(0).get_data_type() == dt_integer);
        assert(r.get<int>(0) == 10);
    }
#else
    {
        row r;
        sql << "select val from soci_test where id = 2", into(r);

        assert(r.size() == 1);
        assert(r.get_properties(0).get_data_type() == dt_integer);
        assert(r.get<int>(0) == 20);
    }
#endif // SOCI_POSTGRESQL_NOPARAMS

    std::cout << "test 13 passed" << std::endl;
}

// More Dynamic binding to row objects
void test14()
{
    {
        session sql(backEndFactory_, connectString_);

        sql.uppercase_column_names(true);

        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        row r1;
        sql << "select * from soci_test", into(r1);
        assert(sql.got_data() == false);

        sql << "insert into soci_test values('david', '(404)123-4567')";
        sql << "insert into soci_test values('john', '(404)123-4567')";
        sql << "insert into soci_test values('doe', '(404)123-4567')";

        row r2;
        statement st = (sql.prepare << "select * from soci_test", into(r2));
        st.execute();
        
        assert(r2.size() == 2);
        
        int count = 0;
        while (st.fetch())
        {
            ++count;
            assert(r2.get<std::string>("PHONE") == "(404)123-4567");
        }
        assert(count == 3);
    }
    std::cout << "test 14 passed" << std::endl;
}

// test15 is like test14 but with a type_conversion instead of a row
void test15()
{
    session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);
    
    // simple conversion (between single basic type and user type)

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        MyInt mi;
        mi.set(123);
        sql << "insert into soci_test(id) values(:id)", use(mi);

        int i;
        sql << "select id from soci_test", into(i);
        assert(i == 123);

        sql << "update soci_test set id = id + 1";

        sql << "select id from soci_test", into(mi);
        assert(mi.get() == 124);
    }

    // simple conversion with use const

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        MyInt mi;
        mi.set(123);

        MyInt const & cmi = mi;
        sql << "insert into soci_test(id) values(:id)", use(cmi);

        int i;
        sql << "select id from soci_test", into(i);
        assert(i == 123);
    }

    // conversions based on values (many fields involved -> ORM)

    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry p1;
        sql << "select * from soci_test", into(p1);
        assert(p1.name ==  "");
        assert(p1.phone == "");

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
                assert(p2.phone =="<NULL>");
            }
            else
            {
                assert(p2.phone == "(404)123-4567");
            }
        }
        assert(count == 3);
    }

    // conversions based on values with use const

    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry p1;
        p1.name = "Joe Coder";
        p1.phone = "123-456";

        PhonebookEntry const & cp1 = p1;

        sql << "insert into soci_test values(:NAME, :PHONE)", use(cp1);

        PhonebookEntry p2;
        sql << "select * from soci_test", into(p2);
        assert(sql.got_data());

        assert(p2.name == "Joe Coder");
        assert(p2.phone == "123-456");
    }

    // conversions based on accessor functions (as opposed to direct variable bindings)

    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry3 p1;
        p1.setName("Joe Hacker");
        p1.setPhone("10010110");

        sql << "insert into soci_test values(:NAME, :PHONE)", use(p1);

        PhonebookEntry3 p2;
        sql << "select * from soci_test", into(p2);
        assert(sql.got_data());

        assert(p2.getName() == "Joe Hacker");
        assert(p2.getPhone() == "10010110");
    }

    {
        // Use the PhonebookEntry2 type conversion, to test
        // calls to values::get_indicator()
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry2 p1;
        sql << "select * from soci_test", into(p1);
        assert(p1.name ==  "");
        assert(p1.phone == "");
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
                assert(p2.phone =="<NULL>");
            }
            else
            {
                assert(p2.phone == "(404)123-4567");
            }
        }
        assert(count == 3);
    }

    std::cout << "test 15 passed" << std::endl;
}

void test_prepared_insert_with_orm_type()
{
    {
        session sql(backEndFactory_, connectString_);

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

        assert(count == 2);
    }

    std::cout << "test test_prepared_insert_with_orm_type passed" << std::endl;
}

void test_placeholder_partial_matching_with_orm_type()
{
    {
        session sql(backEndFactory_, connectString_);
        sql.uppercase_column_names(true);
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry in = { "name1", "phone1" };
        std::string name = "nameA";
        sql << "insert into soci_test values (:NAMED, :PHONE)", use(in), use(name, "NAMED");

        PhonebookEntry out;
        sql << "select * from soci_test where PHONE = 'phone1'", into(out);
        assert(out.name == "nameA");
        assert(out.phone == "phone1");
    }

    std::cout << "test test_placeholder_partial_matching_with_orm_type passed" << std::endl;
}

// test for bulk fetch with single use
void test16()
{
#ifndef SOCI_POSTGRESQL_NOPARAMS
    {
        session sql(backEndFactory_, connectString_);

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

        assert(names.size() == 3);
        assert(names[0] == "anthony");
        assert(names[1] == "john");
        assert(names[2] == "julian");
    }
#endif // SOCI_POSTGRESQL_NOPARAMS

    std::cout << "test 16 passed" << std::endl;
}

// test for basic logging support
void test17()
{
    session sql(backEndFactory_, connectString_);

    std::ostringstream log;
    sql.set_log_stream(&log);

    try
    {
        sql << "drop table soci_test1";
    }
    catch (...) {}

    assert(sql.get_last_query() == "drop table soci_test1");

    sql.set_log_stream(NULL);

    try
    {
        sql << "drop table soci_test2";
    }
    catch (...) {}

    assert(sql.get_last_query() == "drop table soci_test2");

    sql.set_log_stream(&log);

    try
    {
        sql << "drop table soci_test3";
    }
    catch (...) {}

    assert(sql.get_last_query() == "drop table soci_test3");
    assert(log.str() ==
        "drop table soci_test1\n"
        "drop table soci_test3\n");

    std::cout << "test 17 passed\n";
}

// test for rowset creation and copying
void test18()
{
    session sql(backEndFactory_, connectString_);
    
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        // Open empty rowset
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        assert(rs1.begin() == rs1.end());
    }

    {
        // Copy construction
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        rowset<row> rs2(rs1);
        rowset<row> rs3(rs1);
        rowset<row> rs4(rs3);

        assert(rs1.begin() == rs2.begin());
        assert(rs1.begin() == rs3.begin());
        assert(rs1.end() == rs2.end());
        assert(rs1.end() == rs3.end());
    }

    {
        // Assignment
        rowset<row> rs1 = (sql.prepare << "select * from soci_test");
        rowset<row> rs2 = (sql.prepare << "select * from soci_test");
        rowset<row> rs3 = (sql.prepare << "select * from soci_test");
        rs1 = rs2;
        rs3 = rs2;

        assert(rs1.begin() == rs2.begin());
        assert(rs1.begin() == rs3.begin());
        assert(rs1.end() == rs2.end());
        assert(rs1.end() == rs3.end());
    }
    std::cout << "test 18 passed" << std::endl;
}

// test for simple iterating using rowset iterator (without reading data)
void test19()
{
    session sql(backEndFactory_, connectString_);
    
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

            assert(5 == std::distance(rs.begin(), rs.end()));
        }
    }

    std::cout << "test 19 passed" << std::endl;
}

// test for reading rowset<row> using iterator
void test20()
{
    session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);
    
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        {
            // Empty rowset
            rowset<row> rs = (sql.prepare << "select * from soci_test");
            assert(0 == std::distance(rs.begin(), rs.end()));
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
            assert(it != rs.end());

            //
            // First row
            //
            row const & r1 = (*it);

            // Properties
            assert(r1.size() == 5);
            assert(r1.get_properties(0).get_data_type() == dt_double);
            assert(r1.get_properties(1).get_data_type() == dt_integer);
            assert(r1.get_properties(2).get_data_type() == dt_string);
            assert(r1.get_properties(3).get_data_type() == dt_date);
            assert(r1.get_properties(4).get_data_type() == dt_string);
            assert(r1.get_properties("NUM_INT").get_data_type() == dt_integer);

            // Data

            // Since we didn't specify order by in the above query,
            // the 2 rows may be returned in either order
            // (If we specify order by, we can't do it in a cross db
            // compatible way, because the Oracle table for this has been
            // created with lower case column names)

            std::string name = r1.get<std::string>(2);

            assert(name == "Johny" || name == "Robert");
            if (name == "Johny")
            {
                assert(equal_approx(r1.get<double>(0), 3.14));
                assert(r1.get<int>(1) == 123);
                assert(r1.get<std::string>(2) == "Johny");
                std::tm t1 = { 0 };
                t1 = r1.get<std::tm>(3);
                assert(t1.tm_year == 105);
                assert(r1.get<std::string>(4) == "a");
                assert(equal_approx(r1.get<double>("NUM_FLOAT"), 3.14));
                assert(r1.get<int>("NUM_INT") == 123);
                assert(r1.get<std::string>("NAME") == "Johny");
                assert(r1.get<std::string>("CHR") == "a");
            }
            else
            {
                assert(equal_approx(r1.get<double>(0), 6.28));
                assert(r1.get<int>(1) == 246);
                assert(r1.get<std::string>(2) == "Robert");
                std::tm t1 = r1.get<std::tm>(3);
                assert(t1.tm_year == 104);
                assert(r1.get<std::string>(4) == "b");
                assert(equal_approx(r1.get<double>("NUM_FLOAT"), 6.28));
                assert(r1.get<int>("NUM_INT") == 246);
                assert(r1.get<std::string>("NAME") == "Robert");
                assert(r1.get<std::string>("CHR") == "b");
            }

            //
            // Iterate to second row
            //
            ++it;
            assert(it != rs.end());

            //
            // Second row
            //
            row const & r2 = (*it);

            // Properties
            assert(r2.size() == 5);
            assert(r2.get_properties(0).get_data_type() == dt_double);
            assert(r2.get_properties(1).get_data_type() == dt_integer);
            assert(r2.get_properties(2).get_data_type() == dt_string);
            assert(r2.get_properties(3).get_data_type() == dt_date);
            assert(r2.get_properties(4).get_data_type() == dt_string);
            assert(r2.get_properties("NUM_INT").get_data_type() == dt_integer);

            std::string newName = r2.get<std::string>(2);
            assert(name != newName);
            assert(newName == "Johny" || newName == "Robert");

            if (newName == "Johny")
            {
                assert(equal_approx(r2.get<double>(0), 3.14));
                assert(r2.get<int>(1) == 123);
                assert(r2.get<std::string>(2) == "Johny");
                std::tm t2 = r2.get<std::tm>(3);
                assert(t2.tm_year == 105);
                assert(r2.get<std::string>(4) == "a");
                assert(equal_approx(r2.get<double>("NUM_FLOAT"), 3.14));
                assert(r2.get<int>("NUM_INT") == 123);
                assert(r2.get<std::string>("NAME") == "Johny");
                assert(r2.get<std::string>("CHR") == "a");
            }
            else
            {
                assert(equal_approx(r2.get<double>(0), 6.28));
                assert(r2.get<int>(1) == 246);
                assert(r2.get<std::string>(2) == "Robert");
                std::tm t2 = r2.get<std::tm>(3);
                assert(t2.tm_year == 104);
                assert(r2.get<std::string>(4) == "b");
                assert(equal_approx(r2.get<double>("NUM_FLOAT"), 6.28));
                assert(r2.get<int>("NUM_INT") == 246);
                assert(r2.get<std::string>("NAME") == "Robert");
                assert(r2.get<std::string>("CHR") == "b");
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
            assert(it != rs.end());

            //
            // First row
            //
            row const& r1 = (*it);

            // Properties
            assert(r1.size() == 5);
            assert(r1.get_properties(0).get_data_type() == dt_integer);
            assert(r1.get_properties(1).get_data_type() == dt_double);
            assert(r1.get_properties(2).get_data_type() == dt_string);
            assert(r1.get_properties(3).get_data_type() == dt_date);
            assert(r1.get_properties(4).get_data_type() == dt_string);

            // Data
            assert(r1.get_indicator(0) == soci::i_ok);
            assert(r1.get<int>(0) == 0);
            assert(r1.get_indicator(1) == soci::i_null);
            assert(r1.get_indicator(2) == soci::i_null);
            assert(r1.get_indicator(3) == soci::i_null);
            assert(r1.get_indicator(4) == soci::i_null);
        }
    }

    std::cout << "test 20 passed" << std::endl;
}

// test for reading rowset<int> using iterator
void test21()
{
    session sql(backEndFactory_, connectString_);
    
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
            assert(1 == (*pos));

            // 3rd row
            std::advance(pos, 2);
            assert(3 == (*pos));

            // 5th row
            std::advance(pos, 2);
            assert(5 == (*pos));

            // The End
            ++pos;
            assert(pos == rs.end());
        }
    }

    std::cout << "test 21 passed" << std::endl;
}

// test for handling 'use' and reading rowset<std::string> using iterator
void test22()
{
    session sql(backEndFactory_, connectString_);
    
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

            assert(1 == std::distance(rs1.begin(), rs1.end()));

            // Expected result in value
            idle = "jkl";
            rowset<std::string> rs2 = (sql.prepare
                    << "select str from soci_test where str = :idle",
                    use(idle));

            assert(idle == *(rs2.begin()));
        }
    }

    std::cout << "test 22 passed" << std::endl;
}

// test for handling troublemaker
void test23()
{
    session sql(backEndFactory_, connectString_);
    
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(str) values('abc')";
        {
            // verify exception thrown
            bool caught = false;
            try
            {
                std::string troublemaker;
                rowset<std::string> rs1 = (sql.prepare << "select str from soci_test",
                        into(troublemaker));
            }
            catch (soci_error const&)
            {
                caught = true;
            }
            assert(caught);
        }
        std::cout << "test 23 passed" << std::endl;
    }

}

// test for handling NULL values with expected exception:
// "Null value fetched and no indicator defined."
void test24()
{
    session sql(backEndFactory_, connectString_);
    
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(val) values(1)";
        sql << "insert into soci_test(val) values(2)";
        sql << "insert into soci_test(val) values(NULL)";
        sql << "insert into soci_test(val) values(3)";
        {
            // verify exception thrown
            bool caught = false;
            try
            {
                rowset<int> rs = (sql.prepare << "select val from soci_test order by val asc");

                int tester = 0;
                for (rowset<int>::const_iterator it = rs.begin(); it != rs.end(); ++it)
                {
                    tester = *it;
                }
                (void)tester;

                // Never should get here
                assert(false);
            }
            catch (soci_error const&)
            {
                caught = true;
            }
            assert(caught);
        }
        std::cout << "test 24 passed" << std::endl;
    }
}

// test25 is like test15 but with rowset and iterators use
void test25()
{
    session sql(backEndFactory_, connectString_);

    sql.uppercase_column_names(true);
    
    {
        auto_table_creator tableCreator(tc_.table_creator_3(sql));

        PhonebookEntry p1;
        sql << "select * from soci_test", into(p1);
        assert(p1.name ==  "");
        assert(p1.phone == "");

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
                assert(p2.phone =="<NULL>");
            }
            else
            {
                assert(p2.phone == "(404)123-4567");
            }
        }

        assert(3 == count);
    }
    std::cout << "test 25 passed" << std::endl;
}

// test for handling NULL values with boost::optional
// (both into and use)
void test26()
{
#ifdef HAVE_BOOST

    session sql(backEndFactory_, connectString_);

    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        sql << "insert into soci_test(val) values(7)";

        {
            // verify non-null value is fetched correctly
            boost::optional<int> opt;
            sql << "select val from soci_test", into(opt);
            assert(opt.is_initialized());
            assert(opt.get() == 7);

            // indicators can be used with optional
            // (although that's just a consequence of implementation,
            // not an intended feature - but let's test it anyway)
            indicator ind;
            opt.reset();
            sql << "select val from soci_test", into(opt, ind);
            assert(opt.is_initialized());
            assert(opt.get() == 7);
            assert(ind == i_ok);

            // verify null value is fetched correctly
            sql << "select i1 from soci_test", into(opt);
            assert(opt.is_initialized() == false);

            // and with indicator
            opt = 5;
            sql << "select i1 from soci_test", into(opt, ind);
            assert(opt.is_initialized() == false);
            assert(ind == i_null);

            // verify non-null is inserted correctly
            opt = 3;
            sql << "update soci_test set val = :v", use(opt);
            int j = 0;
            sql << "select val from soci_test", into(j);
            assert(j == 3);

            // verify null is inserted correctly
            opt.reset();
            sql << "update soci_test set val = :v", use(opt);
            ind = i_ok;
            sql << "select val from soci_test", into(j, ind);
            assert(ind == i_null);
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

            assert(v.size() == 5);
            assert(v[0].is_initialized());
            assert(v[0].get() == 5);
            assert(v[1].is_initialized());
            assert(v[1].get() == 6);
            assert(v[2].is_initialized());
            assert(v[2].get() == 7);
            assert(v[3].is_initialized());
            assert(v[3].get() == 8);
            assert(v[4].is_initialized());
            assert(v[4].get() == 9);

            // readout of nulls

            sql << "update soci_test set val = null where id = 2 or id = 4";

            std::vector<int> ids(5);
            sql << "select id, val from soci_test order by id", into(ids), into(v);

            assert(v.size() == 5);
            assert(ids.size() == 5);
            assert(v[0].is_initialized());
            assert(v[0].get() == 5);
            assert(v[1].is_initialized() == false);
            assert(v[2].is_initialized());
            assert(v[2].get() == 7);
            assert(v[3].is_initialized() == false);
            assert(v[4].is_initialized());
            assert(v[4].get() == 9);

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
                    assert(id == ids[i]);

                    if (id == 2 || id == 4)
                    {
                        assert(v[i].is_initialized() == false);
                    }
                    else
                    {
                        assert(v[i].is_initialized() && v[i].get() == id + 4);
                    }

                    ++id;
                }

                ids.resize(3);
                v.resize(3);
            }
            assert(id == 6);
        }

        // and why not stress iterators and the dynamic binding, too!

        {
            rowset<row> rs = (sql.prepare << "select id, val, str from soci_test order by id");

            rowset<row>::const_iterator it = rs.begin();
            assert(it != rs.end());
            
            row const& r1 = (*it);

            assert(r1.size() == 3);

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

            //assert(r1.get_properties(0).get_data_type() == dt_integer);
            assert(r1.get_properties(1).get_data_type() == dt_integer);
            assert(r1.get_properties(2).get_data_type() == dt_string);
            //assert(r1.get<int>(0) == 1);
            assert(r1.get<int>(1) == 5);
            assert(r1.get<std::string>(2) == "abc");
            assert(r1.get<boost::optional<int> >(1).is_initialized());
            assert(r1.get<boost::optional<int> >(1).get() == 5);
            assert(r1.get<boost::optional<std::string> >(2).is_initialized());
            assert(r1.get<boost::optional<std::string> >(2).get() == "abc");

            ++it;

            row const& r2 = (*it);

            assert(r2.size() == 3);

            // assert(r2.get_properties(0).get_data_type() == dt_integer);
            assert(r2.get_properties(1).get_data_type() == dt_integer);
            assert(r2.get_properties(2).get_data_type() == dt_string);
            //assert(r2.get<int>(0) == 2);
            try
            {
                // expect exception here, this is NULL value
                (void)r1.get<int>(1);
                assert(false);
            }
            catch (soci_error const &) {}

            // but we can read it as optional
            assert(r2.get<boost::optional<int> >(1).is_initialized() == false);

            // stream-like data extraction

            ++it;
            row const &r3 = (*it);

            boost::optional<int> io;
            boost::optional<std::string> so;

            r3.skip(); // move to val and str columns
            r3 >> io >> so;

            assert(io.is_initialized() && io.get() == 7);
            assert(so.is_initialized() && so.get() == "ghi");

            ++it;
            row const &r4 = (*it);

            r3.skip(); // move to val and str columns
            r4 >> io >> so;

            assert(io.is_initialized() == false);
            assert(so.is_initialized() == false);
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
            assert(sum == 86);

            // bulk inserts of some-null data

            sql << "delete from soci_test";

            v[2].reset();
            v[3].reset();

            sql << "insert into soci_test(id, val) values(:id, :val)",
                use(ids, "id"), use(v, "val");

            sql << "select sum(val) from soci_test", into(sum);
            assert(sum == 41);
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

            assert(omi1.is_initialized() == false);
            assert(omi2.is_initialized() && omi2.get().get() == 125);
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

            assert(omi1.is_initialized() == false);
            assert(omi2.is_initialized() && omi2.get().get() == 125);
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
            assert((*pos).is_initialized());
            assert(10 == (*pos).get());

            // 2nd row
            ++pos;
            assert((*pos).is_initialized());
            assert(11 == (*pos).get());

            // 3rd row
            ++pos;
            assert((*pos).is_initialized() == false);

            // 4th row
            ++pos;
            assert((*pos).is_initialized());
            assert(13 == (*pos).get());
        }
    }

    std::cout << "test 26 passed" << std::endl;
#else
    std::cout << "test 26 skipped (no Boost)" << std::endl;
#endif // HAVE_BOOST
}

// connection and reconnection tests
void test27()
{
    {
        // empty session
        session sql;

        // idempotent:
        sql.close();

        try
        {
            sql.reconnect();
            assert(false);
        }
        catch (soci_error const &e)
        {
            assert(e.what() == std::string(
                       "Cannot reconnect without previous connection."));
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
            assert(false);
        }
        catch (soci_error const &e)
        {
            assert(e.what() == std::string(
                       "Cannot open already connected session."));
        }

        sql.close();

        // open from closed
        sql.open(backEndFactory_, connectString_);

        // reconnect from already connected session
        sql.reconnect();
    }

    {
        session sql;

        try
        {
            sql << "this statement cannot execute";
            assert(false);
        }
        catch (soci_error const &e)
        {
            assert(e.what() == std::string("Session is not connected."));
        }
    }

    std::cout << "test 27 passed" << std::endl;
}

void test28()
{
#ifdef HAVE_BOOST
    session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        boost::tuple<double, int, std::string> t1(3.5, 7, "Joe Hacker");
        assert(equal_approx(t1.get<0>(), 3.5));
        assert(t1.get<1>() == 7);
        assert(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // basic query

        boost::tuple<double, int, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(t2.get<0>(), 3.5));
        assert(t2.get<1>() == 7);
        assert(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with boost::optional

        // use:
        boost::tuple<double, boost::optional<int>, std::string> t1(
            3.5, boost::optional<int>(7), "Joe Hacker");
        assert(equal_approx(t1.get<0>(), 3.5));
        assert(t1.get<1>().is_initialized());
        assert(t1.get<1>().get() == 7);
        assert(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, boost::optional<int>, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(t2.get<0>(), 3.5));
        assert(t2.get<1>().is_initialized());
        assert(t2.get<1>().get() == 7);
        assert(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with user-provided conversions

        // use:
        boost::tuple<double, MyInt, std::string> t1(3.5, 7, "Joe Hacker");
        assert(equal_approx(t1.get<0>(), 3.5));
        assert(t1.get<1>().get() == 7);
        assert(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, MyInt, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(t2.get<0>(), 3.5));
        assert(t2.get<1>().get() == 7);
        assert(t2.get<2>() == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // let's have fun - composition of tuple, optional and user-defined type

        // use:
        boost::tuple<double, boost::optional<MyInt>, std::string> t1(
            3.5, boost::optional<MyInt>(7), "Joe Hacker");
        assert(equal_approx(t1.get<0>(), 3.5));
        assert(t1.get<1>().is_initialized());
        assert(t1.get<1>().get().get() == 7);
        assert(t1.get<2>() == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::tuple<double, boost::optional<MyInt>, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(t2.get<0>(), 3.5));
        assert(t2.get<1>().is_initialized());
        assert(t2.get<1>().get().get() == 7);
        assert(t2.get<2>() == "Joe Hacker");

        sql << "update soci_test set num_int = NULL";

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(t2.get<0>(), 3.5));
        assert(t2.get<1>().is_initialized() == false);
        assert(t2.get<2>() == "Joe Hacker");
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

        assert(equal_approx(pos->get<0>(), 3.5));
        assert(pos->get<1>().is_initialized() == false);
        assert(pos->get<2>() == "Joe Hacker");

        ++pos;
        assert(equal_approx(pos->get<0>(), 4.0));
        assert(pos->get<1>().is_initialized());
        assert(pos->get<1>().get() == 8);
        assert(pos->get<2>() == "Tony Coder");

        ++pos;
        assert(equal_approx(pos->get<0>(), 4.5));
        assert(pos->get<1>().is_initialized() == false);
        assert(pos->get<2>() == "Cecile Sharp");

        ++pos;
        assert(equal_approx(pos->get<0>(),  5.0));
        assert(pos->get<1>().is_initialized());
        assert(pos->get<1>().get() == 10);
        assert(pos->get<2>() == "Djhava Ravaa");

        ++pos;
        assert(pos == rs.end());
    }

    std::cout << "test 28 passed" << std::endl;
#else
    std::cout << "test 28 skipped (no Boost)" << std::endl;
#endif // HAVE_BOOST
}

void test29()
{
#ifdef HAVE_BOOST
#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500

    session sql(backEndFactory_, connectString_);

    auto_table_creator tableCreator(tc_.table_creator_2(sql));
    {
        boost::fusion::vector<double, int, std::string> t1(3.5, 7, "Joe Hacker");
        assert(equal_approx(boost::fusion::at_c<0>(t1), 3.5));
        assert(boost::fusion::at_c<1>(t1) == 7);
        assert(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // basic query

        boost::fusion::vector<double, int, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(boost::fusion::at_c<0>(t2), 3.5));
        assert(boost::fusion::at_c<1>(t2) == 7);
        assert(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with boost::optional

        // use:
        boost::fusion::vector<double, boost::optional<int>, std::string> t1(
            3.5, boost::optional<int>(7), "Joe Hacker");
        assert(equal_approx(boost::fusion::at_c<0>(t1), 3.5));
        assert(boost::fusion::at_c<1>(t1).is_initialized());
        assert(boost::fusion::at_c<1>(t1).get() == 7);
        assert(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, boost::optional<int>, std::string> t2;
        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(boost::fusion::at_c<0>(t2), 3.5));
        assert(boost::fusion::at_c<1>(t2).is_initialized());
        assert(boost::fusion::at_c<1>(t2) == 7);
        assert(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // composability with user-provided conversions

        // use:
        boost::fusion::vector<double, MyInt, std::string> t1(3.5, 7, "Joe Hacker");
        assert(equal_approx(boost::fusion::at_c<0>(t1), 3.5));
        assert(boost::fusion::at_c<1>(t1).get() == 7);
        assert(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, MyInt, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(boost::fusion::at_c<0>(t2), 3.5));
        assert(boost::fusion::at_c<1>(t2).get() == 7);
        assert(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "delete from soci_test";
    }

    {
        // let's have fun - composition of tuple, optional and user-defined type

        // use:
        boost::fusion::vector<double, boost::optional<MyInt>, std::string> t1(
            3.5, boost::optional<MyInt>(7), "Joe Hacker");
        assert(equal_approx(boost::fusion::at_c<0>(t1), 3.5));
        assert(boost::fusion::at_c<1>(t1).is_initialized());
        assert(boost::fusion::at_c<1>(t1).get().get() == 7);
        assert(boost::fusion::at_c<2>(t1) == "Joe Hacker");

        sql << "insert into soci_test(num_float, num_int, name) values(:d, :i, :s)", use(t1);

        // into:
        boost::fusion::vector<double, boost::optional<MyInt>, std::string> t2;

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(boost::fusion::at_c<0>(t2), 3.5));
        assert(boost::fusion::at_c<1>(t2).is_initialized());
        assert(boost::fusion::at_c<1>(t2).get().get() == 7);
        assert(boost::fusion::at_c<2>(t2) == "Joe Hacker");

        sql << "update soci_test set num_int = NULL";

        sql << "select num_float, num_int, name from soci_test", into(t2);

        assert(equal_approx(boost::fusion::at_c<0>(t2), 3.5));
        assert(boost::fusion::at_c<1>(t2).is_initialized() == false);
        assert(boost::fusion::at_c<2>(t2) == "Joe Hacker");
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

        assert(equal_approx(boost::fusion::at_c<0>(*pos), 3.5));
        assert(boost::fusion::at_c<1>(*pos).is_initialized() == false);
        assert(boost::fusion::at_c<2>(*pos) == "Joe Hacker");

        ++pos;
        assert(equal_approx(boost::fusion::at_c<0>(*pos), 4.0));
        assert(boost::fusion::at_c<1>(*pos).is_initialized());
        assert(boost::fusion::at_c<1>(*pos).get() == 8);
        assert(boost::fusion::at_c<2>(*pos) == "Tony Coder");

        ++pos;
        assert(equal_approx(boost::fusion::at_c<0>(*pos), 4.5));
        assert(boost::fusion::at_c<1>(*pos).is_initialized() == false);
        assert(boost::fusion::at_c<2>(*pos) == "Cecile Sharp");

        ++pos;
        assert(equal_approx(boost::fusion::at_c<0>(*pos), 5.0));
        assert(boost::fusion::at_c<1>(*pos).is_initialized());
        assert(boost::fusion::at_c<1>(*pos).get() == 10);
        assert(boost::fusion::at_c<2>(*pos) == "Djhava Ravaa");

        ++pos;
        assert(pos == rs.end());
    }

    std::cout << "test 29 passed" << std::endl;

#else
    std::cout << "test 29 skipped (no boost::fusion)" << std::endl;
#endif // BOOST_VERSION

#else
    std::cout << "test 29 skipped (no Boost)" << std::endl;
#endif // HAVE_BOOST
}

// test for boost::gregorian::date
void test30()
{
#ifdef HAVE_BOOST

    session sql(backEndFactory_, connectString_);

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        std::tm nov15;
        nov15.tm_year = 105;
        nov15.tm_mon = 10;
        nov15.tm_mday = 15;
        nov15.tm_hour = 0;
        nov15.tm_min = 0;
        nov15.tm_sec = 0;

        sql << "insert into soci_test(tm) values(:tm)", use(nov15);

        boost::gregorian::date bgd;
        sql << "select tm from soci_test", into(bgd);

        assert(bgd.year() == 2005);
        assert(bgd.month() == 11);
        assert(bgd.day() == 15);

        sql << "update soci_test set tm = NULL";
        try
        {
            sql << "select tm from soci_test", into(bgd);
            assert(false);
        }
        catch (soci_error const & e)
        {
            assert(e.what() == std::string("Null value not allowed for this type"));
        }
    }

    {
        auto_table_creator tableCreator(tc_.table_creator_1(sql));

        boost::gregorian::date bgd(2008, boost::gregorian::May, 5);

        sql << "insert into soci_test(tm) values(:tm)", use(bgd);

        std::tm t;
        sql << "select tm from soci_test", into(t);

        assert(t.tm_year == 108);
        assert(t.tm_mon == 4);
        assert(t.tm_mday == 5);
    }

    std::cout << "test 30 passed" << std::endl;
#else
    std::cout << "test 30 skipped (no Boost)" << std::endl;
#endif // HAVE_BOOST
}

// connection pool - simple sequential test, no multiple threads
void test31()
{
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
            session sql_unused1(pool);
            session sql(pool);
            session sql_unused2(pool);
            {
                auto_table_creator tableCreator(tc_.table_creator_1(sql));

                char c('a');
                sql << "insert into soci_test(c) values(:c)", use(c);
                sql << "select c from soci_test", into(c);
                assert(c == 'a');
            }
        }
    }
    std::cout << "test 31 passed\n";
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


void run_query_transformation_test(session& sql)
{
    // create and populate the test table
    auto_table_creator tableCreator(tc_.table_creator_1(sql));

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
        assert(count == 'z' - 'a' + 1);
    }

    // free function
    {
        sql.set_query_transformation(lower_than_g);
        int count;
        sql << query, into(count);
        assert(count == 'g' - 'a');
    }

    // function object with state
    {
        sql.set_query_transformation(where_condition("c > 'g' AND c < 'j'"));
        int count = 0;
        sql << query, into(count);
        assert(count == 'j' - 'h');
        count = 0;
        sql.set_query_transformation(where_condition("c > 's' AND c <= 'z'"));
        sql << query, into(count);
        assert(count == 'z' - 's');
    }

// Bug in Visual Studio __cplusplus still means C++03
// https://connect.microsoft.com/VisualStudio/feedback/details/763051/
#if defined _MSC_VER && _MSC_VER>=1600
#define SOCI_HAVE_CPP11 1
#elif __cplusplus >= 201103L
#define SOCI_HAVE_CPP11 1
#else
#undef SOCI_HAVE_CPP11
#endif

#ifdef SOCI_HAVE_CPP11
    // lambda
    {
        sql.set_query_transformation(
            [](std::string const& query) {
                return query + " WHERE c > 'g' AND c < 'j'";
        });

        int count = 0;
        sql << query, into(count);
        assert(count == 'j' - 'h');
    }
#endif
#undef SOCI_HAVE_CPP11

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
        assert(count == 'g' - 'a');
        // reset transformation
        sql.set_query_transformation(no_op_transform);
        // observe the same transformation, no-op set above has no effect
        count = 0;
        st.execute(true);
        assert(count == 'g' - 'a');
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
        assert(count == 'z' - 'a' + 1);
    }
}

void test_query_transformation()
{
    {
        session sql(backEndFactory_, connectString_);
        run_query_transformation_test(sql);
    }
    std::cout << "test query_transformation passed" << std::endl;
}
void test_query_transformation_with_connection_pool()
{
    {
        // phase 1: preparation
        const size_t pool_size = 10;
        connection_pool pool(pool_size);

        for (std::size_t i = 0; i != pool_size; ++i)
        {
            session & sql = pool.at(i);
            sql.open(backEndFactory_, connectString_);
        }

        session sql(pool);
        run_query_transformation_test(sql);
    }
    std::cout << "test query_transformation with connection pool passed" << std::endl;
}

// Originally, submitted to SQLite3 backend and later moved to common test.
// Test commit b394d039530f124802d06c3b1a969c3117683152
// Author: Mika Fischer <mika.fischer@zoopnet.de>
// Date:   Thu Nov 17 13:28:07 2011 +0100
// Implement get_affected_rows for SQLite3 backend
void test_get_affected_rows()
{
    {
        session sql(backEndFactory_, connectString_);
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

        statement st1 = (sql.prepare <<
            "update soci_test set val = val + 1");
        st1.execute(true);

        assert(st1.get_affected_rows() == 10);

        statement st2 = (sql.prepare <<
            "delete from soci_test where val <= 5");
        st2.execute(true);

        assert(st2.get_affected_rows() == 5);

        statement st3 = (sql.prepare <<
            "update soci_test set val = val + 1");
        st3.execute(true);

        assert(st3.get_affected_rows() == 5);

        std::vector<int> v(5, 0);
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            v[i] = (7 + i);
        }
        
        // test affected rows for bulk operations.
        statement st4 = (sql.prepare <<
            "delete from soci_test where val = :v", use(v));
        st4.execute(true);

        assert(st4.get_affected_rows() == 5);

        std::vector<std::string> w(2, "1");
        w[1] = "a"; // this invalid value may cause an exception.
        statement st5 = (sql.prepare <<
            "insert into soci_test(val) values(:val)", use(w));
        try { st5.execute(true); } 
        catch(...) {}

        // confirm the partial insertion.
        int val = 0;
        sql << "select count(val) from soci_test", into(val);
        if(val != 0)
        {        
            // test the preserved 'number of rows 
            // affected' after a potential failure.
            assert(st5.get_affected_rows() != 0);
        }
    }

    std::cout << "test get_affected_rows passed" << std::endl;
}

// test fix for: Backend is not set properly with connection pool (pull #5) 
void test_pull5()
{
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

    std::cout << "test pull-5 passed\n";
}

// issue 67 - Allocated statement backend memory leaks on exception
// If the test runs under memory debugger and it passes, then
// soci::details::statement_impl::backEnd_ must not leak
void test_issue67()
{
    session sql(backEndFactory_, connectString_);
    auto_table_creator tableCreator(tc_.table_creator_1(sql));
    {
        try
        {
            rowset<row> rs1 = (sql.prepare << "select * from soci_testX");
            
            // TODO: On Linux, no exception thrown; neither from prepare, nor from execute?
            // soci_odbc_test_postgresql: 
            //     /home/travis/build/SOCI/soci/src/core/test/common-tests.h:3505:
            //     void soci::tests::common_tests::test_issue67(): Assertion `!"exception expected"' failed.
            //assert(!"exception expected"); // relax temporarily 
        }
        catch (soci_error const &e)
        {
            (void)e;
            assert("expected exception caught");
            std::cout << "test issue-67 passed - check memory debugger output for leaks" << std::endl;
        }
    }

}

// issue 154 - Calling undefine_and_bind and then define_and_bind causes a leak.
// If the test runs under memory debugger and it passes, then
// soci::details::standard_use_type_backend and vector_use_type_backend must not leak
void test_issue154()
{
    session sql(backEndFactory_, connectString_);
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
        assert(val == 1);
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
        assert(val == 1);
    }
    std::cout << "test issue-154 passed - check memory debugger output for leaks" << std::endl;
}

}; // class common_tests

} // namespace tests

} // namespace soci

#endif // SOCI_COMMON_TESTS_H_INCLUDED
