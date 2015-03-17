//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-odbc.h"
#include "common-tests.h"
#include <iostream>
#include <string>
#include <cassert>
#include <ctime>
#include <cmath>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_odbc();

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(ID INTEGER, VAL SMALLINT, C CHAR, STR VARCHAR(20), SH SMALLINT, UL NUMERIC(20), D DOUBLE, "
            "TM TIMESTAMP(9), I1 INTEGER, I2 INTEGER, I3 INTEGER, NAME VARCHAR(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(NUM_FLOAT DOUBLE, NUM_INT INTEGER, NAME VARCHAR(20), SOMETIME TIMESTAMP, CHR CHAR)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(NAME VARCHAR(100) NOT NULL, PHONE VARCHAR(15))";
    }
};

struct table_creator_for_get_affected_rows : table_creator_base
{
    table_creator_for_get_affected_rows(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(VAL INTEGER)";
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

    table_creator_base * table_creator_1(session& s) const
    {
        return new table_creator_one(s);
    }

    table_creator_base * table_creator_2(session& s) const
    {
        return new table_creator_two(s);
    }

    table_creator_base * table_creator_3(session& s) const
    {
        return new table_creator_three(s);
    }

    table_creator_base * table_creator_4(session& s) const
    {
        return new table_creator_for_get_affected_rows(s);
    }

    std::string to_date_time(std::string const &datdt_string) const
    {
        return "\'" + datdt_string + "\'";
    }
};

struct table_creator_bigint : table_creator_base
{
    table_creator_bigint(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST (VAL BIGINT)";
    }
};

void test_odbc_db2_long_long()
{
    const int num_recs = 100;
    session sql(backEnd, connectString);
    table_creator_bigint table(sql);

    {
        long long n;
        statement st = (sql.prepare <<
            "INSERT INTO SOCI_TEST (VAL) VALUES (:val)", use(n));
        for (int i = 0; i < num_recs; i++)
        {
            n = 1000000000LL + i;
            st.execute();
        }
    }
    {
        long long n2;
        statement st = (sql.prepare <<
            "SELECT VAL FROM SOCI_TEST ORDER BY VAL", into(n2));
        st.execute();
        for (int i = 0; i < num_recs; i++)
        {
            st.fetch();
            assert(n2 == 1000000000LL + i);
        }
    }

    std::cout << "test odbc_db2_long_long passed" << std::endl;
}

void test_odbc_db2_unsigned_long_long()
{
    const int num_recs = 100;
    session sql(backEnd, connectString);
    table_creator_bigint table(sql);

    {
        unsigned long long n;
        statement st = (sql.prepare <<
            "INSERT INTO SOCI_TEST (VAL) VALUES (:val)", use(n));
        for (int i = 0; i < num_recs; i++)
        {
            n = 1000000000LL + i;
            st.execute();
        }
    }
    {
        unsigned long long n2;
        statement st = (sql.prepare <<
            "SELECT VAL FROM SOCI_TEST ORDER BY VAL", into(n2));
        st.execute();
        for (int i = 0; i < num_recs; i++)
        {
            st.fetch();
            assert(n2 == 1000000000LL + i);
        }
    }

    std::cout << "test odbc_db2_unsigned_long_long passed" << std::endl;
}

void test_odbc_db2_long_long_vector()
{
    const std::size_t num_recs = 100;
    session sql(backEnd, connectString);
    table_creator_bigint table(sql);

    {
        std::vector<long long> v(num_recs);
        for (std::size_t i = 0; i < num_recs; i++)
        {
            v[i] = 1000000000LL + i;
        }

        sql << "INSERT INTO SOCI_TEST (VAL) VALUES (:bi)", use(v);
    }
    {
        std::size_t recs = 0;

        std::vector<long long> v(num_recs / 2 + 1);
        statement st = (sql.prepare <<
            "SELECT VAL FROM SOCI_TEST ORDER BY VAL", into(v));
        st.execute();
        while (true)
        {
            if (!st.fetch())
            {
                break;
            }

            const std::size_t vsize = v.size();
            for (std::size_t i = 0; i < vsize; i++)
            {
                assert(v[i] == 1000000000LL +
                    static_cast<long long>(recs));
                recs++;
            }
        }
        assert(recs == num_recs);
    }

    std::cout << "test odbc_db2_long_long_vector passed" << std::endl;
}

void test_odbc_db2_unsigned_long_long_vector()
{
    const std::size_t num_recs = 100;
    session sql(backEnd, connectString);
    table_creator_bigint table(sql);

    {
        std::vector<unsigned long long> v(num_recs);
        for (std::size_t i = 0; i < num_recs; i++)
        {
            v[i] = 1000000000LL + i;
        }

        sql << "INSERT INTO SOCI_TEST (VAL) VALUES (:bi)", use(v);
    }
    {
        std::size_t recs = 0;

        std::vector<unsigned long long> v(num_recs / 2 + 1);
        statement st = (sql.prepare <<
            "SELECT VAL FROM SOCI_TEST ORDER BY VAL", into(v));
        st.execute();
        while (true)
        {
            if (!st.fetch())
            {
                break;
            }

            const std::size_t vsize = v.size();
            for (std::size_t i = 0; i < vsize; i++)
            {
                assert(v[i] == 1000000000LL +
                    static_cast<unsigned long long>(recs));
                recs++;
            }
        }
        assert(recs == num_recs);
    }

    std::cout << "test odbc_db2_unsigned_long_long_vector passed" << std::endl;
}

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
        std::cerr << std::endl <<
            "usage: test-odbc-db2 \"DSN=<db>;Uid=<user>;Pwd=<password>\"" <<
            std::endl << std::endl;
        return EXIT_FAILURE;
    }
    try
    {
        std::cout << "\nSOCI ODBC with DB2 Tests:\n\n";

        test_context tc(backEnd, connectString);
        common_tests tests(tc);
        tests.run();

        std::cout << "\nSOCI DB2 Specific Tests:\n\n";
        test_odbc_db2_long_long();
        test_odbc_db2_unsigned_long_long();
        test_odbc_db2_long_long_vector();
        test_odbc_db2_unsigned_long_long_vector();

        std::cout << "\nOK, all tests passed.\n\n";
        return EXIT_SUCCESS;
    }
    catch (soci::odbc_soci_error const & e)
    {
        std::cout << "ODBC Error Code: " << e.odbc_error_code() << std::endl
                  << "Native Error Code: " << e.native_error_code() << std::endl
                  << "SOCI Message: " << e.what() << std::endl
                  << "ODBC Message: " << e.odbc_error_message() << std::endl;
    }
    catch (std::exception const & e)
    {
        std::cout << "STD::EXECEPTION " << e.what() << '\n';
    }
    return EXIT_FAILURE;
}
