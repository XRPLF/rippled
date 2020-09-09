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

#include <ripple/app/main/Application.h>
#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/server/Handoff.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ripple {

class PeerImp;
class BasicConfig;

class OverlayImpl : public Overlay, public squelch::SquelchHandler
{
public:
    class Child
    {
    protected:
        OverlayImpl& overlay_;

        explicit Child(OverlayImpl& overlay);

        virtual ~Child();

    public:
        virtual void
        stop() = 0;
    };

private:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer<clock_type> timer_;

        explicit Timer(OverlayImpl& overlay);

        void
        stop() override;

        void
        run();

        void
        on_timer(error_code ec);
    };

    Application& app_;
    boost::asio::io_service& io_service_;
    boost::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    std::weak_ptr<Timer> timer_;
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;
    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    TrafficCount m_traffic;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImp>> m_peers;
    hash_map<Peer::id_t, std::weak_ptr<PeerImp>> ids_;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;
    int timer_count_;
    std::atomic<uint64_t> jqTransOverflow_{0};
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    // Last time we crawled peers for shard info. 'cs' = crawl shards
    std::atomic<std::chrono::seconds> csLast_{std::chrono::seconds{0}};
    std::mutex csMutex_;
    std::condition_variable csCV_;
    // Peer IDs expecting to receive a last link notification
    std::set<std::uint32_t> csIDs_;

    boost::optional<std::uint32_t> networkID_;

    squelch::Slots<UptimeClock> slots_;

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~OverlayImpl();

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

    PeerFinder::Manager&
    peerFinder()
    {
        return *m_peerFinder;
    }

    Resource::Manager&
    resourceManager()
    {
        return m_resourceManager;
    }

    ServerHandler&
    serverHandler()
    {
        return serverHandler_;
    }

    Setup const&
    setup() const
    {
        return setup_;
    }

    Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        endpoint_type remote_endpoint) override;

    void
    connect(beast::IP::Endpoint const& remote_endpoint) override;

    int
    limit() override;

    std::size_t
    size() const override;

    Json::Value
    json() override;

    PeerSequence
    getActivePeers() const override;

    void
    check() override;

    void checkTracking(std::uint32_t) override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

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

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    void
    add_active(std::shared_ptr<PeerImp> const& peer);

    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate(std::shared_ptr<PeerImp> const& peer);

    // Called when an active peer is destroyed.
    void
    onPeerDeactivate(Peer::id_t id);

    // UnaryFunc will be called as
    //  void(std::shared_ptr<PeerImp>&&)
    //
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp>> wp;
        {
            std::lock_guard lock(mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(ids_.size());

            for (auto& x : ids_)
                wp.push_back(x.second);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    // Called when TMManifests is received from a peer
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp> const& from);

    static bool
    isPeerUpgrade(http_request_type const& request);

    template <class Body>
    static bool
    isPeerUpgrade(boost::beast::http::response<Body> const& response)
    {
        if (!is_upgrade(response))
            return false;
        return response.result() ==
            boost::beast::http::status::switching_protocols;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<true, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (req.method() != boost::beast::http::verb::get)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<false, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    static std::string
    makePrefix(std::uint32_t id);

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

    boost::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

    Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) override;

    /** Called when the last link from a peer chain is received.

        @param id peer id that received the shard info.
    */
    void
    lastLink(std::uint32_t id);

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

    std::shared_ptr<Writer>
    makeRedirectResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address);

    std::shared_ptr<Writer>
    makeErrorResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address,
        std::string msg);

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

    /** Handles non-peer protocol requests.

        @return true if the request was handled.
    */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

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
    // Stoppable
    //

    void
    checkStopped();

    void
    onPrepare() override;

    void
    onStart() override;

    void
    onStop() override;

    void
    onChildrenStopped() override;

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

    void
    remove(Child& child);

    void
    stop();

    void
    autoConnect();

    void
    sendEndpoints();

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
};

}  // namespace ripple

#endif
