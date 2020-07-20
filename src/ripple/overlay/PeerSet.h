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

#ifndef RIPPLE_APP_PEERS_PEERSET_H_INCLUDED
#define RIPPLE_APP_PEERS_PEERSET_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <mutex>
#include <set>

namespace ripple {

/** Supports data retrieval by managing a set of peers.

    When desired data (such as a ledger or a transaction set)
    is missing locally it can be obtained by querying connected
    peers. This class manages common aspects of the retrieval.
    Callers maintain the set by adding and removing peers depending
    on whether the peers have useful information.

    The data is represented by its hash.
*/
class PeerSet
{
public:
    virtual ~PeerSet() = default;

    /**
     * Try add more peers
     * @param limit  number of peers to add
     * @param hasItem  callback that helps to select peers
     * @param onPeerAdded  callback called when a peer is added
     */
    virtual void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) = 0;

    /** send a message */
    template <typename MessageType>
    void
    sendRequest(MessageType const& message, std::shared_ptr<Peer> const& peer)
    {
        this->sendRequest(message, protocolMessageType(message), peer);
    }

    virtual void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) = 0;

    /** get the set of ids of previously added peers */
    virtual const std::set<Peer::id_t>&
    getPeerIds() const = 0;
};

class PeerSetBuilder
{
public:
    virtual ~PeerSetBuilder() = default;

    virtual std::unique_ptr<PeerSet>
    build() = 0;
};

std::unique_ptr<PeerSetBuilder>
make_PeerSetBuilder(Application& app);

/**
 * Make a dummy PeerSet that does not do anything.
 * @note For the use case of InboundLedger in ApplicationImp::loadOldLedger(),
 *       where a real PeerSet is not needed.
 */
std::unique_ptr<PeerSet>
make_DummyPeerSet(Application& app);

}  // namespace ripple

#endif
