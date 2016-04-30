//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_PRINT_H_INCLUDED
#define BEAST_UNIT_TEST_PRINT_H_INCLUDED

#include <beast/unit_test/amount.hpp>
#include <beast/unit_test/results.hpp>
#include <beast/unit_test/abstract_ostream.hpp>
#include <beast/unit_test/basic_std_ostream.hpp>

#include <iostream>
#include <string>

namespace beast {
namespace unit_test {

/** Write test results to the specified output stream. */
/** @{ */
template <class = void>
void
print (results const& r, abstract_ostream& stream)
{
    for (auto const& s : r)
    {
        for (auto const& c : s)
        {
            stream <<
                s.name() <<
                (c.name().empty() ? "" : ("." + c.name()));

            std::size_t i (1);
            for (auto const& t : c.tests)
            {
                if (! t.pass)
                    stream <<
                        "#" << i <<
                        " failed: " << t.reason;
                ++i;
            }
        }
    }

    stream <<
        amount (r.size(), "suite") << ", " <<
        amount (r.cases(), "case") << ", " <<
        amount (r.total(), "test") << " total, " <<
        amount (r.failed(), "failure")
        ;
}

template <class = void>
void
print (results const& r, std::ostream& stream = std::cout)
{
    auto s (make_std_ostream (stream));
    print (r, s);
}

} // unit_test
} // beast

#endif
