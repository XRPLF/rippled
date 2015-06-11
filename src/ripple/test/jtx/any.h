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

#ifndef RIPPLE_TEST_JTX_ANY_H_INCLUDED
#define RIPPLE_TEST_JTX_ANY_H_INCLUDED

#include <ripple/protocol/STAmount.h>

namespace ripple {
namespace test {

namespace jtx {

struct AnyAmount;

struct any_t
{
    any_t() { }

    inline
    AnyAmount
    operator()(STAmount const& sta) const;
};

// This wrapper helps pay destinations
// in their own issue using generic syntax
struct AnyAmount
{
    bool is_any;
    STAmount value;

    AnyAmount() = delete;
    AnyAmount (AnyAmount const&) = default;
    AnyAmount& operator= (AnyAmount const&) = default;

    AnyAmount (STAmount const& amount)
        : is_any(false)
        , value(amount)
    {
    }

    AnyAmount (STAmount const& amount,
            any_t const*)
        : is_any(true)
        , value(amount)
    {
    }

    // Reset the issue to a specific account
    void
    to (ripple::Account const& id)
    {
        if (! is_any)
            return;
        value.setIssuer(id);
    }
};

inline
AnyAmount
any_t::operator()(STAmount const& sta) const
{
    return AnyAmount(sta, this);
}

/** Returns an amount representing "any issuer"
    @note With respect to what the recipient will accept
*/
static any_t const any;

} // jtx

} // test
} // ripple

#endif
