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
#include <ripple/shamap/SHAMapMissingNode.h>
#include <ostream>

namespace ripple {

std::ostream&
operator<< (std::ostream& out, const SHAMapMissingNode& mn)
{
    switch (mn.getMapType ())
    {
    case SHAMapType::TRANSACTION:
        out << "Missing/TXN(" << mn.getNodeHash () << ")";
        break;

    case SHAMapType::STATE:
        out << "Missing/STA(" << mn.getNodeHash () << ")";
        break;

    case SHAMapType::FREE:
    default:
        out << "Missing/" << mn.getNodeHash ();
        break;
    };

    return out;
}

} // ripple
