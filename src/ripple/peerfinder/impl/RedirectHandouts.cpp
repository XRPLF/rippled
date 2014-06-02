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

#include <ripple/peerfinder/impl/RedirectHandouts.h>

namespace ripple {
namespace PeerFinder {

RedirectHandouts::RedirectHandouts (SlotImp::ptr const& slot)
    : m_slot (slot)
{
    m_list.reserve (Tuning::redirectEndpointCount);
}

bool
RedirectHandouts::try_insert (Endpoint const& ep)
{
    if (full ())
        return false;

    // VFALCO NOTE This check can be removed when we provide the
    //             addresses in a peer HTTP handshake instead of
    //             the tmENDPOINTS message.
    //
    if (ep.hops > Tuning::maxHops)
        return false;

    // Don't send them our address
    if (ep.hops == 0)
        return false;

    // Don't send them their own address
    if (m_slot->remote_endpoint().address() ==
        ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of (m_list.begin(), m_list.end(),
        [&ep](Endpoint const& other)
        {
            // Ignore port for security reasons
            return other.address.address() == ep.address.address();
        }))
    {
        return false;
    }

    m_list.emplace_back (ep.address, ep.hops);

    return true;
}

}
}
