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

#ifndef RIPPLE_PROTOCOL_XRPAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_XRPAMOUNT_H_INCLUDED

#include <ripple/protocol/SystemParameters.h>
#include <beast/utility/noexcept.h>
#include <beast/utility/Zero.h>
#include <boost/operators.hpp>
#include <cstdint>

using beast::zero;

namespace ripple {

class XRPAmount
    : private boost::totally_ordered <XRPAmount>
    , private boost::additive <XRPAmount>
{
private:
    std::int64_t value_;

public:
    /** @{ */
    XRPAmount () = default;
    XRPAmount (XRPAmount const& other) = default;
    XRPAmount& operator= (XRPAmount const& other) = default;

    XRPAmount (beast::Zero)
        : value_ (0)
    {
    }

    XRPAmount&
    operator= (beast::Zero)
    {
        value_ = 0;
        return *this;
    }

    XRPAmount (std::int64_t value)
        : value_ (value)
    {
    }

    XRPAmount&
    operator= (std::int64_t v)
    {
        value_ = v;
        return *this;
    }

    XRPAmount&
    operator+= (XRPAmount const& other)
    {
        value_ += other.value_;
        return *this;
    }

    XRPAmount&
    operator-= (XRPAmount const& other)
    {
        value_ -= other.value_;
        return *this;
    }

    XRPAmount
    operator- () const
    {
        return { -value_ };
    }

    bool
    operator==(XRPAmount const& other) const
    {
        return value_ == other.value_;
    }

    bool
    operator<(XRPAmount const& other) const
    {
        return value_ < other.value_;
    }

    /** Returns true if the amount is not zero */
    explicit
    operator bool() const noexcept
    {
        return value_ != 0;
    }

    /** Return the sign of the amount */
    int
    signum() const noexcept
    {
        return (value_ < 0) ? -1 : (value_ ? 1 : 0);
    }
};

/** Returns true if the amount does not exceed the initial XRP in existence. */
inline
bool isLegalAmount (XRPAmount const& amount)
{
    return amount <= SYSTEM_CURRENCY_START;
}

}

#endif
