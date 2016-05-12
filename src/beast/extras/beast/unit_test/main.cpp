//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/unit_test/amount.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/match.hpp>
#include <beast/unit_test/reporter.hpp>
#include <beast/unit_test/suite.hpp>
#include <beast/unit_test/debug_ostream.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <vector>

#ifdef _MSC_VER
# ifndef WIN32_LEAN_AND_MEAN // VC_EXTRALEAN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  undef WIN32_LEAN_AND_MEAN
# else
#  include <windows.h>
# endif
#endif

#include <cstdlib>

namespace beast {
namespace unit_test {

std::string
prefix(suite_info const& s)
{
    if (s.manual())
        return "|M| ";
    return "    ";
}

template<class Log>
void
print(Log& log, suite_list const& c)
{
    std::size_t manual = 0;
    for(auto const& s : c)
    {
        log <<
            prefix (s) <<
            s.full_name();
        if(s.manual())
            ++manual;
    }
    log <<
        amount(c.size(), "suite") << " total, " <<
        amount(manual, "manual suite")
        ;
}

template<class Log>
void
print(Log& log)
{
    log << "------------------------------------------";
    print(log, global_suites());
    log << "------------------------------------------";
}

} // unit_test
} // beast

// Simple main used to produce stand
// alone executables that run unit tests.
int main(int ac, char const* av[])
{
    using namespace std;
    using namespace beast::unit_test;

#ifdef _MSC_VER
    {
        int flags = _CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
        flags |= _CRTDBG_LEAK_CHECK_DF;
        _CrtSetDbgFlag (flags);
    }
#endif

    namespace po = boost::program_options;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h",  "Produce a help message")
        ("print,r", "Print the list of available test suites")
        ("suites,s", po::value<string>(), "suites to run")
        ;

    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);

    beast::debug_ostream log;

    if(vm.count("help"))
    {
        log << desc;
    }
    else if(vm.count("print"))
    {
        print(log);
    }
    else
    {
        std::string suites;
        if(vm.count("suites") > 0)
            suites = vm["suites"].as<string>();
        reporter r(log);
        bool failed;
        if(! suites.empty())
            failed = r.run_each_if(global_suites(),
                match_auto(suites));
        else
            failed = r.run_each(global_suites());
        if (failed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
}
