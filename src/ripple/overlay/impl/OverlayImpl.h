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

#ifndef RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED

#include <ripple/core/Job.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/P2PConfigImpl.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/TrafficCount.h>

namespace ripple {

class PeerImp;
class BasicConfig;

/** Represents application layer overlay. Application layer
 * overlay is also p2p overlay and has access to some members of P2POverlayImpl.
 * It implements application methods declared in Overlay and other application
 * methods such as metrics collection and message squelching management.
 */
class OverlayImpl : public P2POverlayImpl<PeerImp>,
                    public reduce_relay::SquelchHandler
{
private:
    Application& app_;
    TrafficCount m_traffic;
    int timer_count_;
    std::atomic<uint64_t> jqTransOverflow_{0};
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    // 'cs' = crawl shards
    std::mutex csMutex_;
    std::condition_variable csCV_;
    // Peer IDs expecting to receive a last link notification
    std::set<std::uint32_t> csIDs_;

    reduce_relay::Slots<UptimeClock> slots_;

    // A message with the list of manifests we send to peers
    std::shared_ptr<Message> manifestMessage_;
    // Used to track whether we need to update the cached list of manifests
    std::optional<std::uint32_t> manifestListSeq_;
    // Protects the message and the sequence list of manifests
    std::mutex manifestLock_;

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        Setup const& setup,
        std::uint16_t overlayPort,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

    void
    start() override;

    void
    stop() override;

    Json::Value
    json() override;

    void checkTracking(std::uint32_t) override;

    void
    broadcast(protocol::TMProposeSet& m) override;

    void
    broadcast(protocol::TMValidation& m) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    std::shared_ptr<Message>
    getManifestsMessage();

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    /** Called when TMManifests is received from a peer */
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp> const& from);

    void
    reportTraffic(TrafficCount::category cat, bool isInbound, int bytes);

    void
    incJqTransOverflow() override
    {
        ++jqTransOverflow_;
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return jqTransOverflow_;
    }

    void
    incPeerDisconnect() override
    {
        ++peerDisconnects_;
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return peerDisconnects_;
    }

    void
    incPeerDisconnectCharges() override
    {
        ++peerDisconnectsCharges_;
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return peerDisconnectsCharges_;
    }

    Json::Value
    crawlShards(bool includePublicKey, std::uint32_t relays) override;

    /** Called when the reply from the last peer in a peer chain is received.

        @param id peer id that received the shard info.
    */
    void
    endOfPeerChain(std::uint32_t id);

    /** Updates message count for validator/peer. Sends TMSquelch if the number
     * of messages for N peers reaches threshold T. A message is counted
     * if a peer receives the message for the first time and if
     * the message has been  relayed.
     * @param key Unique message's key
     * @param validator Validator's public key
     * @param peers Peers' id to update the slots for
     * @param type Received protocol message type
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        std::set<Peer::id_t>&& peers,
        protocol::MessageType type);

    /** Overload to reduce allocation in case of single peer
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t peer,
        protocol::MessageType type);

    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     */
    void
    deletePeer(Peer::id_t id);

private:
    void
    squelch(
        PublicKey const& validator,
        Peer::id_t const id,
        std::uint32_t squelchDuration) const override;

    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override;

    /** Handles crawl requests. Crawl returns information about the
        node and its peers so crawlers can map the network.

        @return true if the request was handled.
    */
    bool
    processCrawl(http_request_type const& req, Handoff& handoff);

    /** Handles validator list requests.
        Using a /vl/<hex-encoded public key> URL, will retrieve the
        latest valdiator list (or UNL) that this node has for that
        public key, if the node trusts that public key.

        @return true if the request was handled.
    */
    bool
    processValidatorList(http_request_type const& req, Handoff& handoff);

    /** Handles health requests. Health returns information about the
        health of the node.

        @return true if the request was handled.
    */
    bool
    processHealth(http_request_type const& req, Handoff& handoff);

    /** Returns information about peers on the overlay network.
        Reported through the /crawl API
        Controlled through the config section [crawl] overlay=[0|1]
    */
    Json::Value
    getOverlayInfo();

    /** Returns information about the local server.
        Reported through the /crawl API
        Controlled through the config section [crawl] server=[0|1]
    */
    Json::Value
    getServerInfo();

    /** Returns information about the local server's performance counters.
        Reported through the /crawl API
        Controlled through the config section [crawl] counts=[0|1]
    */
    Json::Value
    getServerCounts();

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    //
    // PropertyStream
    //
    void
    onWrite(beast::PropertyStream::Map& stream) override;

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator */
    void
    deleteIdlePeers();

private:
    struct TrafficGauges
    {
        TrafficGauges(
            char const* name,
            beast::insight::Collector::ptr const& collector)
            : bytesIn(collector->make_gauge(name, "Bytes_In"))
            , bytesOut(collector->make_gauge(name, "Bytes_Out"))
            , messagesIn(collector->make_gauge(name, "Messages_In"))
            , messagesOut(collector->make_gauge(name, "Messages_Out"))
        {
        }
        beast::insight::Gauge bytesIn;
        beast::insight::Gauge bytesOut;
        beast::insight::Gauge messagesIn;
        beast::insight::Gauge messagesOut;
    };

    struct Stats
    {
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector,
            std::vector<TrafficGauges>&& trafficGauges_)
            : peerDisconnects(
                  collector->make_gauge("Overlay", "Peer_Disconnects"))
            , trafficGauges(std::move(trafficGauges_))
            , hook(collector->make_hook(handler))
        {
        }

        beast::insight::Gauge peerDisconnects;
        std::vector<TrafficGauges> trafficGauges;
        beast::insight::Hook hook;
    };

    Stats m_stats;
    std::mutex m_statsMutex;

private:
    void
    collect_metrics()
    {
        auto counts = m_traffic.getCounts();
        std::lock_guard lock(m_statsMutex);
        assert(counts.size() == m_stats.trafficGauges.size());

        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            m_stats.trafficGauges[i].bytesIn = counts[i].bytesIn;
            m_stats.trafficGauges[i].bytesOut = counts[i].bytesOut;
            m_stats.trafficGauges[i].messagesIn = counts[i].messagesIn;
            m_stats.trafficGauges[i].messagesOut = counts[i].messagesOut;
        }
        m_stats.peerDisconnects = getPeerDisconnect();
    }

private:
    /** Implementation of delegated event handling from the p2p layer */
    bool
    onEvtProcessRequest(http_request_type const& req, Handoff& handoff)
        override;

    /** Instantiate inbound application layer peer implementation */
    std::shared_ptr<PeerImp>
    mkInboundPeer(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) override;

    /** Instantiate outbound application layer peer implementation */
    std::shared_ptr<PeerImp>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Peer::id_t id) override;

    /** Check if idle peers should be removed from squelching */
    void
    onEvtTimer() override;
};

}  // namespace ripple

#endif
