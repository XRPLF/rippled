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

#include <ripple/protocol/STAccount.h>

namespace ripple {

STAccount::STAccount ()
    : STBase ()
    , value_ (beast::zero)
    , default_ (true)
{
}

STAccount::STAccount (SField const& n)
    : STBase (n)
    , value_ (beast::zero)
    , default_ (true)
{
}

STAccount::STAccount (SField const& n, Buffer&& v)
    : STAccount (n)
{
    if (v.empty())
        return;  // Zero is a valid size for a defaulted STAccount.

    // Is it safe to throw from this constructor?  Today (November 2015)
    // the only place that calls this constructor is
    //    STVar::STVar (SerialIter&, SField const&)
    // which throws.  If STVar can throw in its constructor, then so can
    // STAccount.
    if (v.size() != uint160::bytes)
        Throw<std::runtime_error> ("Invalid STAccount size");

    default_ = false;
    memcpy (value_.begin(), v.data (), uint160::bytes);
}

STAccount::STAccount (SerialIter& sit, SField const& name)
    : STAccount(name, sit.getVLBuffer())
{
}

STAccount::STAccount (SField const& n, AccountID const& v)
    : STBase (n)
    , default_ (false)
{
    value_.copyFrom (v);
}

std::string STAccount::getText () const
{
    if (isDefault())
        return "";
    return toBase58 (value());
}

} // ripple
