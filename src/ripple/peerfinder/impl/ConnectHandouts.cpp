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

#include <ripple/peerfinder/impl/ConnectHandouts.h>

namespace ripple {
namespace PeerFinder {

ConnectHandouts::ConnectHandouts (
    std::size_t needed, Squelches& squelches)
    : m_needed (needed)
    , m_squelches (squelches)
{
    m_list.reserve (needed);
}

bool
ConnectHandouts::try_insert (beast::IP::Endpoint const& endpoint)
{
    if (full ())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of (m_list.begin(), m_list.end(),
        [&endpoint](beast::IP::Endpoint const& other)
        {
            // Ignore port for security reasons
            return other.address() ==
                endpoint.address();
        }))
    {
        return false;
    }

    // Add to squelch list so we don't try it too often.
    // If its already there, then make try_insert fail.
    auto const result (m_squelches.insert (
        endpoint.address()));
    if (! result.second)
        return false;
    
    m_list.push_back (endpoint);

    return true;
}

}
}
