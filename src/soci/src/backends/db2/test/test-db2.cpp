//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-db2.h"
#include "common-tests.h"
#include <iostream>
#include <string>
#include <cassert>
#include <cstdlib>
#include <ctime>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_db2();


//
// Support for soci Common Tests
//

struct table_creator_one : public table_creator_base
{
    table_creator_one(session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(ID INTEGER, VAL SMALLINT, C CHAR, STR VARCHAR(20), SH SMALLINT, UL NUMERIC(20), D DOUBLE, "
            "TM TIMESTAMP, I1 INTEGER, I2 INTEGER, I3 INTEGER, NAME VARCHAR(20))";
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

class test_context :public test_context_base
{
public:
    test_context(backend_factory const & pi_back_end, std::string const & pi_connect_string)
        : test_context_base(pi_back_end, pi_connect_string) {}

    table_creator_base* table_creator_1(session & pr_s) const
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_one(pr_s);
    }

    table_creator_base* table_creator_2(session & pr_s) const
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_two(pr_s);
    }

    table_creator_base* table_creator_3(session & pr_s) const
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_three(pr_s);
    }

    table_creator_base* table_creator_4(session& s) const
    {
        return new table_creator_for_get_affected_rows(s);
    }

    std::string to_date_time(std::string const & pi_datdt_string) const
    {
        return "to_date('" + pi_datdt_string + "', 'YYYY-MM-DD HH24:MI:SS')";
    }
};


//
// Additional tests to exercise the DB2 backend
//

void test1()
{
    {
        session sql(backEnd, connectString);

        sql << "SELECT CURRENT TIMESTAMP FROM SYSIBM.SYSDUMMY1";
        sql << "SELECT " << 123 << " FROM SYSIBM.SYSDUMMY1";

        std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8))";
        sql << query;

        {
            const int i = 7;
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,"id");
            int j = 0;
            sql << "select id from db2inst1.SOCI_TEST where id=7", into(j);
            assert(j == i);
        }

        {
            const long int li = 9;
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(li,"id");
            long int lj = 0;;
            sql << "select id from db2inst1.SOCI_TEST where id=9", into(lj);
            assert(lj == li);
        }

        {
            const long long ll = 11;
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(ll,"id");
            long long lj = 0;
            sql << "select id from db2inst1.SOCI_TEST where id=11", into(lj);
            assert(lj == ll);
        }

        {
            const int i = 13;
            indicator i_ind = i_ok;
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,i_ind,"id");
            int j = 0;
            indicator j_ind = i_null;
            sql << "select id from db2inst1.SOCI_TEST where id=13", into(j,j_ind);
            assert(j == i);
            assert(j_ind == i_ok);
        }

        {
            std::vector<int> numbers(100);
            for (int i = 0 ; i < 100 ; i++)
            {
                numbers[i] = i + 1000;
            }
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(numbers,"id");
            sql << "select id from db2inst1.SOCI_TEST where id >= 1000 and id < 2000 order by id", into(numbers);
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 1000);
            }
        }

        {
            std::vector<int> numbers(100);
            std::vector<indicator> inds(100);
            for (int i = 0 ; i < 100 ; i++)
            {
                numbers[i] = i + 2000;
                inds[i] = i_ok;
            }
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(numbers,inds,"id");
            for (int i = 0 ; i < 100 ; i++)
            {
                numbers[i] = 0;
                inds[i] = i_null;
            }
            sql << "select id from db2inst1.SOCI_TEST where id >= 2000 and id < 3000 order by id", into(numbers,inds);
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 2000);
                assert(inds[i] == i_ok);
            }
        }

        {
            int i = 0;
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id < 1000", into(i));
            st.execute();
            st.fetch();
            assert (i == 7);
            st.fetch();
            assert (i == 9);
            st.fetch();
            assert (i == 11);
            st.fetch();
            assert (i == 13);
        }

        {
            int i = 0;
            indicator i_ind = i_null;
            std::string d;
            indicator d_ind = i_ok;
            statement st = (sql.prepare << "select id, data from db2inst1.SOCI_TEST where id = 13", into(i, i_ind), into(d, d_ind));
            st.execute();
            st.fetch();
            assert (i == 13);
            assert (i_ind == i_ok);
            assert (d_ind == i_null);
        }

        {
            std::vector<int> numbers(100);
            for (int i = 0 ; i < 100 ; i++)
            {
                numbers[i] = 0;
            }
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id >= 1000 order by id", into(numbers));
            st.execute();
            st.fetch();
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 1000);
            }
            st.fetch();
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 2000);
            }
        }

        {
            std::vector<int> numbers(100);
            std::vector<indicator> inds(100);
            for (int i = 0 ; i < 100 ; i++)
            {
                numbers[i] = 0;
                inds[i] = i_null;
            }
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id >= 1000 order by id", into(numbers, inds));
            st.execute();
            st.fetch();
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 1000);
                assert(inds[i] == i_ok);
            }
            st.fetch();
            for (int i = 0 ; i < 100 ; i++)
            {
                assert(numbers[i] == i + 2000);
                assert(inds[i] == i_ok);
            }
        }

        {
            // XXX: what is the purpose of this test??  what is the expected value?
            int i = 0;
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(i));
        }
        
        {
            // XXX: what is the purpose of this test??  what is the expected value?
            int i = 0;
            indicator ind = i_ok;
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(i, ind));
        }
        
        {
            // XXX: what is the purpose of this test??  what is the expected value?
            std::vector<int> numbers(100);
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(numbers));
        }

        {
            // XXX: what is the purpose of this test??  what is the expected value?
            std::vector<int> numbers(100);
            std::vector<indicator> inds(100);
            statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(numbers, inds));
        }

        sql<<"DROP TABLE DB2INST1.SOCI_TEST";

        sql.commit();
    }

    std::cout << "test 1 passed" << std::endl;
}

void test2() {
    {
        session sql(backEnd, connectString);

        std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8),DT TIMESTAMP)";
        sql << query;

        {
            int i = 7;
            std::string n("test");
            sql << "insert into db2inst1.SOCI_TEST (id,data) values (:id,:name)", use(i,"id"),use(n,"name");
            int j;
            std::string m;
            sql << "select id,data from db2inst1.SOCI_TEST where id=7", into(j),into(m);
            assert (j == i);
            assert (m == n);
        }
        
        {
            int i = 8;
            sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,"id");
            int j;
            std::string m;
            indicator ind = i_ok;
            sql << "select id,data from db2inst1.SOCI_TEST where id=8", into(j),into(m,ind);
            assert(j == i);
            assert(ind==i_null);
        }

        {
            std::tm dt;
            sql << "select current timestamp from sysibm.sysdummy1",into(dt);
            sql << "insert into db2inst1.SOCI_TEST (dt) values (:dt)",use(dt,"dt");
            std::tm dt2;
            sql << "select dt from db2inst1.SOCI_TEST where dt is not null", into(dt2);
            assert(dt2.tm_year == dt.tm_year && dt2.tm_mon == dt.tm_mon && dt2.tm_mday == dt.tm_mday &&
                dt2.tm_hour == dt.tm_hour && dt2.tm_min == dt.tm_min && dt2.tm_sec == dt.tm_sec);
        }
        
        sql<<"DROP TABLE DB2INST1.SOCI_TEST";
        sql.commit();
    }

    std::cout << "test 2 passed" << std::endl;
}

void test3() {
    {
        session sql(backEnd, connectString);
        int i;

        std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8),DT TIMESTAMP)";
        sql << query;

        std::vector<long long> ids(100);
        std::vector<std::string> data(100);
        std::vector<std::tm> dts(100);
        for (int i = 0; i < 100; i++)
        {
            ids[i] = 1000000000LL + i;
            data[i] = "test";
            dts[i].tm_year = 112;
            dts[i].tm_mon = 7;
            dts[i].tm_mday = 17;
            dts[i].tm_hour = 0;
            dts[i].tm_min = 0;
            dts[i].tm_sec = i % 60;
        }
        
        sql << "insert into db2inst1.SOCI_TEST (id, data, dt) values (:id, :data, :dt)",
            use(ids, "id"), use(data,"data"), use(dts, "dt");

        i = 0;
        rowset<row> rs = (sql.prepare<<"SELECT ID, DATA, DT FROM DB2INST1.SOCI_TEST");
        for (rowset<row>::const_iterator it = rs.begin(); it != rs.end(); it++)
        {
            const row & r = *it;
            const long long id = r.get<long long>(0);
            const std::string data = r.get<std::string>(1);
            const std::tm dt = r.get<std::tm>(2);
            
            assert(id == 1000000000LL + i);
            assert(data == "test");
            assert(dt.tm_year == 112);
            assert(dt.tm_mon == 7);
            assert(dt.tm_mday == 17);
            assert(dt.tm_hour == 0);
            assert(dt.tm_min == 0);
            assert(dt.tm_sec == i % 60);

            i += 1;
        }
        
        sql<<"DROP TABLE DB2INST1.SOCI_TEST";
        sql.commit();
    }

    std::cout << "test 3 passed" << std::endl;
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
        std::cout << "usage: " << argv[0]
            << " connectstring\n"
            << "example: " << argv[0]
            << " \'DSN=SAMPLE;Uid=db2inst1;Pwd=db2inst1;autocommit=off\'\n";
        std::exit(1);
    }

    test_context tc(backEnd, connectString);
    common_tests tests(tc);
    tests.run();
    
    try
    {
        std::cout<<"\nSOCI DB2 Tests:\n\n";
        
        session sql(backEnd, connectString);

        try
        {
            // attempt to delete the test table from previous runs
            sql << "DROP TABLE DB2INST1.SOCI_TEST";
        }
        catch (soci_error const & e)
        {
            // if the table didn't exist, then proceed
        }

        test1();
        test2();
        test3();
        // ...

        std::cout << "\nOK, all tests passed.\n\n";

        return EXIT_SUCCESS;
    }
    catch (std::exception const & e)
    {
        std::cout << e.what() << '\n';
    }

    return EXIT_FAILURE;
}
