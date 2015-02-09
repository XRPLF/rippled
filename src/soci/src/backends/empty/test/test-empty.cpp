//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-empty.h"
#include <iostream>
#include <string>
#include <cassert>
#include <cstdlib>
#include <ctime>

using namespace soci;

std::string connectString;
backend_factory const &backEnd = *soci::factory_empty();


// NOTE:
// This file is supposed to serve two purposes:
// 1. To be a starting point for implementing new tests (for new backends).
// 2. To exercise (at least some of) the syntax and try the SOCI library
//    against different compilers, even in those environments where there
//    is no database. SOCI uses advanced template techniques which are known
//    to cause problems on different versions of popular compilers, and this
//    test is handy to verify that the code is accepted by as many compilers
//    as possible.
//
// Both of these purposes mean that the actual code here is meaningless
// from the database-development point of view. For new tests, you may wish
// to remove this code and keep only the general structure of this file.

struct Person
{
    int id;
    std::string firstName;
    std::string lastName;
};

namespace soci
{
    template<> struct type_conversion<Person>
    {
        typedef values base_type;
        static void from_base(values & /* r */, indicator /* ind */,
            Person & /* p */)
        {
        }
    };
}

void test1()
{
    {
        session sql(backEnd, connectString);

        sql << "Do what I want.";
        sql << "Do what I want " << 123 << " times.";

        std::string query = "some query";
        sql << query;

        int i = 7;
        sql << "insert", use(i);
        sql << "select", into(i);

#if defined (__LP64__) || ( __WORDSIZE == 64 )
        long int li = 9;
        sql << "insert", use(li);
        sql << "select", into(li);
#endif

        long long ll = 11;
        sql << "insert", use(ll);
        sql << "select", into(ll);

        indicator ind = i_ok;
        sql << "insert", use(i, ind);
        sql << "select", into(i, ind);

        std::vector<int> numbers(100);
        sql << "insert", use(numbers);
        sql << "select", into(numbers);

        std::vector<indicator> inds(100);
        sql << "insert", use(numbers, inds);
        sql << "select", into(numbers, inds);

        {
            statement st = (sql.prepare << "select", into(i));
            st.execute();
            st.fetch();
        }
        {
            statement st = (sql.prepare << "select", into(i, ind));
        }
        {
            statement st = (sql.prepare << "select", into(numbers));
        }
        {
            statement st = (sql.prepare << "select", into(numbers, inds));
        }
        {
            statement st = (sql.prepare << "insert", use(i));
        }
        {
            statement st = (sql.prepare << "insert", use(i, ind));
        }
        {
            statement st = (sql.prepare << "insert", use(numbers));
        }
        {
            statement st = (sql.prepare << "insert", use(numbers, inds));
        }
        {
            Person p;
            sql << "select person", into(p);
        }

    }

    std::cout << "test 1 passed" << std::endl;
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
            << " \'connect_string_for_empty_backend\'\n";
        std::exit(1);
    }

    try
    {
        test1();
        // test2();
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
