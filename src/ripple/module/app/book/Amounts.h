//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_AMOUNTS_H_INCLUDED
#define RIPPLE_CORE_AMOUNTS_H_INCLUDED

#include <ripple/module/app/book/Amount.h>

#include <beast/utility/noexcept.h>

namespace ripple {
namespace core {

struct Amounts
{
    Amounts() = default;

    Amounts (Amount const& in_, Amount const& out_)
        : in (in_)
        , out (out_)
    {
    }

    /** Returns `true` if either quantity is not positive. */
    bool
    empty() const noexcept
    {
        return in <= zero || out <= zero;
    }

    Amount in;
    Amount out;
};

inline
bool
operator== (Amounts const& lhs, Amounts const& rhs) noexcept
{
    return lhs.in == rhs.in && lhs.out == rhs.out;
}

inline
bool
operator!= (Amounts const& lhs, Amounts const& rhs) noexcept
{
    return ! (lhs == rhs);
}

}
}

#endif
