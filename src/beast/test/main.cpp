//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <beast/detail/unit_test/amount.hpp>
#include <beast/detail/unit_test/global_suites.hpp>
#include <beast/detail/unit_test/match.hpp>
#include <beast/detail/unit_test/reporter.hpp>
#include <beast/detail/unit_test/suite.hpp>
#include <beast/detail/stream/debug_ostream.hpp>
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
namespace detail {
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
} // detail
} // beast

// Simple main used to produce stand
// alone executables that run unit tests.
int main(int ac, char const* av[])
{
    using namespace std;
    using namespace beast::detail::unit_test;

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

    beast::detail::debug_ostream log;

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
