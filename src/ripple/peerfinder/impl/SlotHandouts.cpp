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

#include <ripple/peerfinder/impl/SlotHandouts.h>

namespace ripple {
namespace PeerFinder {

SlotHandouts::SlotHandouts (SlotImp::ptr const& slot)
    : m_slot (slot)
{
    m_list.reserve (Tuning::numberOfEndpoints);
}

bool
SlotHandouts::try_insert (Endpoint const& ep)
{
    if (full ())
        return false;

    if (ep.hops > Tuning::maxHops)
        return false;

    if (m_slot->recent.filter (ep.address, ep.hops))
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
        return false;

    m_list.emplace_back (ep.address, ep.hops);

    // Insert into this slot's recent table. Although the endpoint
    // didn't come from the slot, adding it to the slot's table
    // prevents us from sending it again until it has expired from
    // the other end's cache.
    //
    m_slot->recent.insert (ep.address, ep.hops);

    return true;
}

}
}
