//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_AMOUNTS_H_INCLUDED
#define RIPPLE_TEST_JTX_AMOUNTS_H_INCLUDED

#include <ripple/test/jtx/tags.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/STAmount.h>
#include <cstdint>
#include <string>

namespace ripple {
namespace test {
namespace jtx {

// Represents "no amount" of a currency
// This is distinct from zero or a balance.
// For example, no USD means the trust line
// doesn't even exist. Using this in an
// inappropriate context will generate a
// compile error.
//
struct None
{
    Issue issue;
};

namespace detail {

struct XRP_t
{
    XRP_t() = default;

    /** Implicit conversion to Issue.

        This allows passing XRP where
        an Issue is expected.
    */
    operator Issue() const
    {
        return xrpIssue();
    }

    /** Returns an amount of XRP as STAmount

        @param v The number of XRP (not drops)
    */
    STAmount operator()(double v) const;

    /** Returns None-of-XRP */
    None operator()(none_t) const
    {
        return { xrpIssue() };
    }
};

} // detail

/** Converts to XRP Issue or STAmount.

    Examples:
        XRP         Converts to the XRP Issue
        XRP(10)     Returns STAmount of 10 XRP
*/
extern detail::XRP_t XRP;

/** Returns an XRP STAmount.

    Example:
        drops(10)   Returns STAmount of 10 drops
*/
inline
STAmount
drops (std::uint64_t v)
{
    return STAmount(v, false);
}

namespace detail {

struct epsilon_multiple
{
    std::size_t n;
};

} // detail

// The smallest possible IOU STAmount
struct epsilon_t
{
    epsilon_t()
    {
    }

    detail::epsilon_multiple
    operator()(std::size_t n) const
    {
        return { n };
    }
};

static epsilon_t const epsilon;

/** Converts to IOU Issue or STAmount.

    Examples:
        IOU         Converts to the underlying Issue
        IOU(10)     Returns STAmount of 10 of
                        the underlying Issue.
*/
class IOU
{
private:
    Issue issue_;

public:
    IOU(Issue const& issue)
        : issue_(issue)
    {
    }

    /** Implicit conversion to Issue.

        This allows passing an IOU
        value where an Issue is expected.
    */
    operator Issue() const
    {
        return issue_;
    }

    STAmount operator()(double v) const;
    STAmount operator()(epsilon_t) const;
    STAmount operator()(detail::epsilon_multiple) const;

    // VFALCO TODO
    // STAmount operator()(char const* s) const;

    /** Returns None-of-Issue */
    None operator()(none_t) const
    {
        return { issue_ };
    }
};

} // jtx
} // test
} // ripple

#endif
