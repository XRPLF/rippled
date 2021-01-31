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

#ifndef RIPPLE_OVERLAY_OVERLAY_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAY_H_INCLUDED

#include <ripple/beast/utility/PropertyStream.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/server/Handoff.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <type_traits>

namespace boost {
namespace asio {
namespace ssl {
class context;
}
}  // namespace asio
}  // namespace boost

namespace ripple {

/** Manages the set of connected peers. */
class Overlay : public beast::PropertyStream::Source
{
protected:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             PropertyStream
    Overlay() : beast::PropertyStream::Source("peers")
    {
    }

public:
    enum class Promote { automatic, never, always };

    struct Setup
    {
        explicit Setup() = default;

        std::shared_ptr<boost::asio::ssl::context> context;
        beast::IP::Address public_ip;
        int ipLimit = 0;
        std::uint32_t crawlOptions = 0;
        std::optional<std::uint32_t> networkID;
        bool vlEnabled = true;
    };

    using PeerSequence = std::vector<std::shared_ptr<Peer>>;

    virtual ~Overlay() = default;

    virtual void
    start()
    {
    }

    virtual void
    stop()
    {
    }

    /** Conditionally accept an incoming HTTP request. */
    virtual Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address) = 0;

    /** Establish a peer connection to the specified endpoint.
        The call returns immediately, the connection attempt is
        performed asynchronously.
    */
    virtual void
    connect(beast::IP::Endpoint const& address) = 0;

    /** Returns the maximum number of peers we are configured to allow. */
    virtual int
    limit() = 0;

    /** Returns the number of active peers.
        Active peers are only those peers that have completed the
        handshake and are using the peer protocol.
    */
    virtual std::size_t
    size() const = 0;

    /** Return diagnostics on the status of all peers.
        @deprecated This is superceded by PropertyStream
    */
    virtual Json::Value
    json() = 0;

    /** Returns a sequence representing the current list of peers.
        The snapshot is made at the time of the call.
    */
    virtual PeerSequence
    getActivePeers() const = 0;

    /** Calls the checkTracking function on each peer
        @param index the value to pass to the peer's checkTracking function
    */
    virtual void
    checkTracking(std::uint32_t index) = 0;

    /** Returns the peer with the matching short id, or null. */
    virtual std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const = 0;

    /** Returns the peer with the matching public key, or null. */
    virtual std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) = 0;

    /** Broadcast a proposal. */
    virtual void
    broadcast(protocol::TMProposeSet& m) = 0;

    /** Broadcast a validation. */
    virtual void
    broadcast(protocol::TMValidation& m) = 0;

    /** Relay a proposal.
     * @param m the serialized proposal
     * @param uid the id used to identify this proposal
     * @param validator The pubkey of the validator that issued this proposal
     * @return the set of peers which have already sent us this proposal
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Relay a validation.
     * @param m the serialized validation
     * @param uid the id used to identify this validation
     * @param validator The pubkey of the validator that issued this validation
     * @return the set of peers which have already sent us this validation
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Visit every active peer.
     *
     * The visitor must be invocable as:
     *     Function(std::shared_ptr<Peer> const& peer);
     *
     * @param f the invocable to call with every peer
     */
    template <class Function>
    void
    foreach(Function f) const
    {
        for (auto const& p : getActivePeers())
            f(p);
    }

    /** Increment and retrieve counter for transaction job queue overflows. */
    virtual void
    incJqTransOverflow() = 0;
    virtual std::uint64_t
    getJqTransOverflow() const = 0;

    /** Increment and retrieve counters for total peer disconnects, and
     * disconnects we initiate for excessive resource consumption.
     */
    virtual void
    incPeerDisconnect() = 0;
    virtual std::uint64_t
    getPeerDisconnect() const = 0;
    virtual void
    incPeerDisconnectCharges() = 0;
    virtual std::uint64_t
    getPeerDisconnectCharges() const = 0;

    /** Returns information reported to the crawl shard RPC command.

        @param hops the maximum jumps the crawler will attempt.
        The number of hops achieved is not guaranteed.
    */
    virtual Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) = 0;

    /** Returns the ID of the network this server is configured for, if any.

        The ID is just a numerical identifier, with the IDs 0, 1 and 2 used to
        identify the mainnet, the testnet and the devnet respectively.

        @return The numerical identifier configured by the administrator of the
                server. An unseated optional, otherwise.
    */
    virtual boost::optional<std::uint32_t>
    networkID() const = 0;
};

}  // namespace ripple

#endif
