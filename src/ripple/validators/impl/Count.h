//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_VALIDATORS_COUNT_H_INCLUDED
#define RIPPLE_VALIDATORS_COUNT_H_INCLUDED

namespace ripple {
namespace Validators {

/** Measures Validator performance statistics. */
struct Count
{
    Count()
        : received (0)
        , expected (0)
        , closed (0)
    {
    }

    Count (std::size_t received_,
           std::size_t expected_,
           std::size_t closed_)
        : received (received_)
        , expected (expected_)
        , closed (closed_)
    {
    }

    /** Reset the statistics. */
    void clear ()
    {
        *this = Count();
    }

    /** Returns the percentage of ledger participation. */
    int percent () const
    {
        int const total (closed + expected);
        if (total > 0)
            return (closed * 100) / total;
        return 0;
    }

    /** Returns the percentage of orphaned validations. */
    int percent_orphaned () const
    {
        int const total (received + closed);
        if (total > 0)
            return (received * 100) / total;
        return 0;
    }

    /** Output to PropertyStream. */
    void onWrite (beast::PropertyStream::Map& map)
    {
        map["received"] = received;
        map["expected"] = expected;
        map["closed"]   = closed;
        map["percent"]  = percent ();
        map["percent_orphan"] = percent_orphaned();
    }

    std::size_t received;   // Count of validations without a closed ledger
    std::size_t expected;   // Count of closed ledgers without a validation
    std::size_t closed;     // Number of validations with closed ledgers
};

inline Count operator+ (Count const& lhs, Count const& rhs)
{
    return Count (
        lhs.received + rhs.received,
        lhs.expected + rhs.expected,
        lhs.closed   + rhs.closed);
}

}
}

#endif
