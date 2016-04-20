//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/detail/unit_test/amount.hpp>
#include <beast/detail/unit_test/global_suites.hpp>
#include <beast/detail/unit_test/suite.hpp>
#include <string>

// Include this .cpp in your project to gain access to the printing suite

namespace beast {
namespace detail {

inline
namespace unit_test {
namespace detail {

/** A suite that prints the list of globally defined suites. */
class print_test : public suite
{
private:
    template <class = void>
    void
    do_run();

public:
    template <class = void>
    static
    std::string
    prefix (suite_info const& s);

    template <class = void>
    void
    print (suite_list &c);

    void
    run()
    {
        do_run();
    }
};

template <class>
void
print_test::do_run()
{
    log << "------------------------------------------";
    print (global_suites());
    log << "------------------------------------------";
    pass();
}

template <class>
std::string
print_test::prefix (suite_info const& s)
{
    if (s.manual())
        return "|M| ";
    return "    ";
}

template <class>
void
print_test::print (suite_list &c)
{
    std::size_t manual (0);
    for (auto const& s : c)
    {
        log <<
            prefix (s) <<
            s.full_name();
        if (s.manual())
            ++manual;
    }
    log <<
        amount (c.size(), "suite") << " total, " <<
        amount (manual, "manual suite")
        ;
}

BEAST_DEFINE_TESTSUITE_MANUAL(print,unit_test,beast);

} // detail
} // unit_test
} // detail
} // beast
