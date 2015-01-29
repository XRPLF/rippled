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

#ifndef BEAST_UNIT_TEST_AMOUNT_H_INCLUDED
#define BEAST_UNIT_TEST_AMOUNT_H_INCLUDED

#include <cstddef>
#include <ostream>
#include <string>

namespace beast {
namespace unit_test {

/** Utility for producing nicely composed output of amounts with units. */
class amount
{
private:
    std::size_t n_;
    std::string const& what_;

public:
    amount (amount const&) = default;
    amount& operator= (amount const&) = delete;

    template <class = void>
    amount (std::size_t n, std::string const& what);

    friend
    std::ostream&
    operator<< (std::ostream& s, amount const& t);
};

template <class>
amount::amount (std::size_t n, std::string const& what)
    : n_ (n)
    , what_ (what)
{
}

inline
std::ostream&
operator<< (std::ostream& s, amount const& t)
{
    s << t.n_ << " " << t.what_ << ((t.n_ != 1) ? "s" : "");
    return s;
}

} // unit_test
} // beast

#endif
