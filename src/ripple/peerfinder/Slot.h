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

#ifndef RIPPLE_PEERFINDER_SLOT_H_INCLUDED
#define RIPPLE_PEERFINDER_SLOT_H_INCLUDED

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/protocol/PublicKey.h>
#include <boost/optional.hpp>
#include <memory>

namespace ripple {
namespace PeerFinder {

/** Properties and state associated with a peer to peer overlay connection. */
class Slot
{
public:
    using ptr = std::shared_ptr<Slot>;

    enum State { accept, connect, connected, active, closing };

    virtual ~Slot() = 0;

    /** Returns `true` if this is an inbound connection. */
    virtual bool
    inbound() const = 0;

    /** Returns `true` if this is a fixed connection.
        A connection is fixed if its remote endpoint is in the list of
        remote endpoints for fixed connections.
    */
    virtual bool
    fixed() const = 0;

    /** Returns `true` if this is a reserved connection.
        It might be a cluster peer, or a peer with a reservation.
        This is only known after then handshake completes.
     */
    virtual bool
    reserved() const = 0;

    /** Returns the state of the connection. */
    virtual State
    state() const = 0;

    /** The remote endpoint of socket. */
    virtual beast::IP::Endpoint const&
    remote_endpoint() const = 0;

    /** The local endpoint of the socket, when known. */
    virtual boost::optional<beast::IP::Endpoint> const&
    local_endpoint() const = 0;

    virtual boost::optional<std::uint16_t>
    listening_port() const = 0;

    /** The peer's public key, when known.
        The public key is established when the handshake is complete.
    */
    virtual boost::optional<PublicKey> const&
    public_key() const = 0;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
