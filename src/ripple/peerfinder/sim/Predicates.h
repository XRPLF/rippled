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

#ifndef RIPPLE_PEERFINDER_SIM_PREDICATES_H_INCLUDED
#define RIPPLE_PEERFINDER_SIM_PREDICATES_H_INCLUDED

namespace ripple {
namespace PeerFinder {
namespace Sim {

/** UnaryPredicate, returns `true` if the 'to' node on a Link matches. */
/** @{ */
template <typename Node>
class is_remote_node_pred
{
public:
    is_remote_node_pred (Node const& n)
        : node (n)
        { }
    template <typename Link>
    bool operator() (Link const& l) const
        { return &node == &l.remote_node(); }
private:
    Node const& node;
};

template <typename Node>
is_remote_node_pred <Node> is_remote_node (Node const& node)
{
    return is_remote_node_pred <Node> (node);
}

template <typename Node>
is_remote_node_pred <Node> is_remote_node (Node const* node)
{
    return is_remote_node_pred <Node> (*node);
}
/** @} */

//------------------------------------------------------------------------------

/** UnaryPredicate, `true` if the remote address matches. */
class is_remote_endpoint
{
public:
    explicit is_remote_endpoint (beast::IP::Endpoint const& address)
        : m_endpoint (address)
        { }
    template <typename Link>
    bool operator() (Link const& link) const
    {
        return link.remote_endpoint() == m_endpoint;
    }
private:
    beast::IP::Endpoint const m_endpoint;
};

}
}
}

#endif
