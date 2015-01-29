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

#ifndef BEAST_UNIT_TEST_PRINT_H_INCLUDED
#define BEAST_UNIT_TEST_PRINT_H_INCLUDED

#include <beast/unit_test/amount.h>
#include <beast/unit_test/results.h>

#include <beast/streams/abstract_ostream.h>
#include <beast/streams/basic_std_ostream.h>

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
