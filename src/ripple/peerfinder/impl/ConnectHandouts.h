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

#ifndef RIPPLE_PEERFINDER_CONNECTHANDOUTS_H_INCLUDED
#define RIPPLE_PEERFINDER_CONNECTHANDOUTS_H_INCLUDED

#include <ripple/peerfinder/impl/Tuning.h>

#include <beast/container/aged_set.h>

namespace ripple {
namespace PeerFinder {

/** Receives handouts for making automatic connections. */
class ConnectHandouts
{
public:
    // Keeps track of addresses we have made outgoing connections
    // to, for the purposes of not connecting to them too frequently.
    typedef beast::aged_set <beast::IP::Address> Squelches;

    typedef std::vector <beast::IP::Endpoint> list_type;

private:
    std::size_t m_needed;
    Squelches& m_squelches;
    list_type m_list;

public:
    ConnectHandouts (std::size_t needed, Squelches& squelches);

    bool empty() const
    {
        return m_list.empty();
    }

    bool full() const
    {
        return m_list.size() >= m_needed;
    }

    bool try_insert (Endpoint const& endpoint)
    {
        return try_insert (endpoint.address);
    }

    list_type& list()
    {
        return m_list;
    }

    list_type const& list() const
    {
        return m_list;
    }

    bool try_insert (beast::IP::Endpoint const& endpoint);
};

}
}

#endif
