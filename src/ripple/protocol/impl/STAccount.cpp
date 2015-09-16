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

#include <BeastConfig.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/types.h>

namespace ripple {

STAccount::STAccount (SerialIter& sit, SField const& name)
    : STAccount(name, sit.getVLBuffer())
{
}

std::string STAccount::getText () const
{
    AccountID u;
    RippleAddress a;
    if (! getValueH160 (u))
        return STBlob::getText ();
    return toBase58(u);
}

STAccount::STAccount (SField const& n, AccountID const& v)
        : STBlob (n, v.data (), v.size ())
{
}

bool STAccount::isValueH160 () const
{
    return peekValue ().size () == (160 / 8);
}

} // ripple
