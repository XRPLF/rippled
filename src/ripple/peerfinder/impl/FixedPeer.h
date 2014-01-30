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

#ifndef RIPPLE_PEERFINDER_FIXEDPEER_H_INCLUDED
#define RIPPLE_PEERFINDER_FIXEDPEER_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Stores information about a fixed peer.
    A fixed peer is defined in the config file and can be specified using
    either an IP address or a hostname (which may resolve to zero or more
    addresses).
    A fixed peer which has multiple IP addresses is considered connected
    if there is a connection to any one of its addresses.
*/
class FixedPeer
{
public:
    /* The config name */
    std::string const m_name;

    /* The corresponding IP address(es) */
    IPAddresses m_addresses;

    FixedPeer (std::string const& name,
        IPAddresses const& addresses)
            : m_name (name)
            , m_addresses (addresses)
    {
        bassert (!m_addresses.empty ());

        // NIKB TODO add support for multiple IPs
        m_addresses.resize (1);
    }

    // NIKB TODO support peers which resolve to more than a single address
    IPAddress getAddress () const
    {
        if (m_addresses.size ())
            return m_addresses.at(0);

        return IPAddress ();
    }

    template <typename Comparator>
    bool hasAddress (IPAddress const& address, Comparator compare) const
    {
        for (IPAddresses::const_iterator iter = m_addresses.cbegin();
            iter != m_addresses.cend(); ++iter)
        {
            if (compare (*iter, address))
                return true;
        }
        
        return false;
    }
};

}
}

#endif
