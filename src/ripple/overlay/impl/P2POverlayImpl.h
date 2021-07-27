//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED

#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/P2PConfigImpl.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/Handoff.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace ripple {

class BasicConfig;
template <typename PeerImp_t>
class ConnectAttempt;
template <typename PeerImp_t>
class P2PeerImp;

inline std::size_t
hash_value(std::shared_ptr<PeerFinder::Slot> const& slot)
{
    beast::uhash<> hasher;
    return hasher(slot);
}

/** Represents the overlay. Maintains connected remote peers.
 * Manages inbound/outbound connections and endpoints broadcast.
 * Maintains PeerFinder, which manages livecache/bootcache and
 * the endpoints generation for autoconnect, redirect, and broadcast.
 * @tparam PeerImp_t application layer peer implementation type
 */
template <typename PeerImp_t>
class P2POverlayImpl : public Overlay
{
protected:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

private:
    /** Access tags by id and slot into the multi index container */
    struct ById
    {
    };
    struct BySlot
    {
    };
    /** Item stored in the multi index container */
    struct PeerItem
    {
        PeerItem(
            Peer::id_t i,
            std::shared_ptr<PeerFinder::Slot> const& s,
            std::shared_ptr<PeerImp_t> const& p)
            : id(i), slot(s), peer(p)
        {
        }
        Peer::id_t id;
        std::shared_ptr<PeerFinder::Slot> slot;
        std::weak_ptr<PeerImp_t> peer;
    };
    /** Intermediate types to help with readability */
    template <class tag, typename Type, Type PeerItem::*PtrToMember>
    using hashed_unique = boost::multi_index::hashed_unique<
        boost::multi_index::tag<tag>,
        boost::multi_index::member<PeerItem, Type, PtrToMember>>;
    /** Intermediate types to help with readability */
    using indexing = boost::multi_index::indexed_by<
        hashed_unique<ById, Peer::id_t, &PeerItem::id>,
        hashed_unique<
            BySlot,
            std::shared_ptr<PeerFinder::Slot>,
            &PeerItem::slot>>;

public:
    /** Overlay maintains a list of children - asynchronous
     * processes (P2PeerImp, ConnectAttempt, and Timer) that must be
     * stopped and cleaned up when the overlay stops or the objects
     * are destroyed.
     */
    class Child
    {
    protected:
        P2POverlayImpl<PeerImp_t>& overlay_;

        explicit Child(P2POverlayImpl<PeerImp_t>& overlay);

        virtual ~Child();

    public:
        virtual void
        stop() = 0;
    };

private:
    /** Once a second overlay timer */
    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer<clock_type> timer_;
        bool stopping_{false};

        explicit Timer(P2POverlayImpl<PeerImp_t>& overlay);

        void
        stop() override;

        void
        async_wait();

        void
        on_timer(error_code ec);
    };

    std::weak_ptr<Timer> timer_;
    std::unique_ptr<P2PConfig> const p2pConfig_;
    std::optional<boost::asio::io_service::work> work_;
    std::condition_variable_any cond_;
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;
    boost::multi_index::multi_index_container<PeerItem, indexing> peers_;
    std::uint16_t const overlayPort_;
    Resource::Manager& m_resourceManager;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;
    std::optional<std::uint32_t> networkID_;

protected:
    boost::asio::io_service& io_service_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    Setup const setup_;
    beast::Journal const journal_;
    std::unique_ptr<PeerFinder::Manager> const m_peerFinder;

    //--------------------------------------------------------------------------

public:
    P2POverlayImpl(
        std::unique_ptr<P2PConfig>&& p2pConfig,
        Setup const& setup,
        std::uint16_t overlayPort,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    P2POverlayImpl(P2POverlayImpl const&) = delete;
    P2POverlayImpl&
    operator=(P2POverlayImpl const&) = delete;

    void
    start() override;

    void
    stop() override;

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

    PeerSequence
    getActivePeers() const override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    std::shared_ptr<PeerImp_t>
    findPeerBySlot(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Called when an active peer is destroyed */
    void
    onPeerDeactivate(Peer::id_t id);

    /** UnaryFunc will be called as
     *  void(std::shared_ptr<PeerImp>&&)
     */
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp_t>> wp;
        {
            std::lock_guard lock(mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(peers_.size());

            for (auto const& x : peers_)
                wp.push_back(x.peer);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    //--------------------------------------------------------------------------
    //
    // P2POverlayImpl
    //

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

    std::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

    /** Calls mkInboundPeer() to get the inbound peer application layer
     * instance and adds it to the peer's container. It is called
     * in P2POverlayImpl::onHandoff().
     */
    void
    addInboundPeer(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr);

    /** Calls mkOutboundPeer() to get the outbound peer application layer
     * instance and adds it to the peer's container. It is called
     * in ConnectAttempt::processResponse().
     */
    void
    addOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Peer::id_t id);

    P2PConfig const&
    p2pConfig() const
    {
        return *p2pConfig_;
    }

    void
    addChild(std::shared_ptr<Child> const& child)
    {
        std::lock_guard l(mutex_);
        list_.emplace(child.get(), child);
    }

private:
    void
    autoConnect();

    void
    remove(Child& child);

    void
    stopChildren();

    void
    sendEndpoints();

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

    /** Handles non-peer protocol requests.

        @return true if the request was handled.
    */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

    /** Delegates non-peer protocol requests to the application layer */
    virtual bool
    onEvtProcessRequest(http_request_type const& req, Handoff& handoff) = 0;

    /** Delegates instantiation of the application layer inbound peer
     * to the application layer overlay implementation.
     */
    virtual std::shared_ptr<PeerImp_t>
    mkInboundPeer(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

    /** Delegates instantiation of the application layer outbound peer
     * to the application layer overlay implementation.
     */
    virtual std::shared_ptr<PeerImp_t>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Peer::id_t id) = 0;

    /** Instantiates ConnectAttempt. Facilitates subclassing
     * of ConnectAttempt in unit-testing where address binding
     * might be required.
     */
    virtual std::shared_ptr<ConnectAttempt<PeerImp_t>>
    mkConnectAttempt(
        beast::IP::Endpoint const& remote_endpoint,
        Resource::Consumer const& usage,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        std::uint16_t id);

    void
    add_active(std::shared_ptr<PeerImp_t> const& peer);

    /** Hook for the application layer to handle on-timer event */
    virtual void
    onEvtTimer() = 0;
};

}  // namespace ripple

#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/P2PeerImp.h>

namespace ripple {

template <typename PeerImp_t>
P2POverlayImpl<PeerImp_t>::Child::Child(P2POverlayImpl<PeerImp_t>& overlay)
    : overlay_(overlay)
{
}

template <typename PeerImp_t>
P2POverlayImpl<PeerImp_t>::Child::~Child()
{
    overlay_.remove(*this);
}

template <typename PeerImp_t>
P2POverlayImpl<PeerImp_t>::Timer::Timer(P2POverlayImpl<PeerImp_t>& overlay)
    : Child(overlay), timer_(this->overlay_.io_service_)
{
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::Timer::stop()
{
    // This method is only ever called from the same strand that calls
    // Timer::on_timer, ensuring they never execute concurrently.
    stopping_ = true;
    timer_.cancel();
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::Timer::async_wait()
{
    timer_.expires_after(std::chrono::seconds(1));
    timer_.async_wait(this->overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, this->shared_from_this(), std::placeholders::_1)));
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::Timer::on_timer(error_code ec)
{
    if (ec || stopping_)
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(this->overlay_.journal_.error())
                << "on_timer: " << ec.message();
        }
        return;
    }

    this->overlay_.m_peerFinder->once_per_second();
    this->overlay_.sendEndpoints();
    this->overlay_.autoConnect();

    this->overlay_.onEvtTimer();

    async_wait();
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
P2POverlayImpl<PeerImp_t>::P2POverlayImpl(
    std::unique_ptr<P2PConfig>&& p2pConfig,
    Setup const& setup,
    std::uint16_t overlayPort,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : p2pConfig_(std::move(p2pConfig))
    , work_(std::in_place, std::ref(io_service))
    , overlayPort_(overlayPort)
    , m_resourceManager(resourceManager)
    , m_resolver(resolver)
    , next_id_(1)
    , io_service_(io_service)
    , strand_(io_service_)
    , setup_(setup)
    , journal_(p2pConfig_->logs().journal("Overlay"))
    , m_peerFinder(PeerFinder::make_Manager(
          io_service,
          stopwatch(),
          p2pConfig_->logs().journal("PeerFinder"),
          config,
          collector))
{
}

template <typename PeerImp_t>
Handoff
P2POverlayImpl<PeerImp_t>::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{
    auto const id = next_id_++;
    beast::WrappedSink sink(p2pConfig_->logs()["Peer"], makePrefix(id));
    beast::Journal journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remote_endpoint;

    error_code ec;
    auto const local_endpoint(
        stream_ptr->next_layer().socket().local_endpoint(ec));
    if (ec)
    {
        JLOG(journal.debug()) << remote_endpoint << " failed: " << ec.message();
        return handoff;
    }

    auto consumer = m_resourceManager.newInboundEndpoint(
        beast::IPAddressConversion::from_asio(remote_endpoint));
    if (consumer.disconnect())
        return handoff;

    auto const slot = m_peerFinder->new_inbound_slot(
        beast::IPAddressConversion::from_asio(local_endpoint),
        beast::IPAddressConversion::from_asio(remote_endpoint));

    if (slot == nullptr)
    {
        // self-connect, close
        handoff.moved = false;
        return handoff;
    }

    // Validate HTTP request

    {
        auto const types = beast::rfc2616::split_commas(request["Connect-As"]);
        if (std::find_if(types.begin(), types.end(), [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response =
                makeRedirectResponse(slot, request, remote_endpoint.address());
            handoff.keep_alive = beast::rfc2616::is_keep_alive(request);
            return handoff;
        }
    }

    auto const negotiatedVersion = negotiateProtocolVersion(request["Upgrade"]);
    if (!negotiatedVersion)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Unable to agree on a protocol version");
        handoff.keep_alive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*stream_ptr, journal);
    if (!sharedValue)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Incorrect security cookie");
        handoff.keep_alive = false;
        return handoff;
    }

    try
    {
        auto publicKey = verifyHandshake(
            request,
            *sharedValue,
            setup_.networkID,
            setup_.public_ip,
            remote_endpoint.address(),
            *p2pConfig_);

        {
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
            bool const reserved =
                static_cast<bool>(p2pConfig_->clusterMember(publicKey)) ||
                p2pConfig_->reservedPeer(publicKey);
            auto const result =
                m_peerFinder->activate(slot, publicKey, reserved);
            if (result != PeerFinder::Result::success)
            {
                m_peerFinder->on_closed(slot);
                JLOG(journal.debug())
                    << "Peer " << remote_endpoint << " redirected, slots full";
                handoff.moved = false;
                handoff.response = makeRedirectResponse(
                    slot, request, remote_endpoint.address());
                handoff.keep_alive = false;
                return handoff;
            }
        }

        addInboundPeer(
            id,
            slot,
            std::move(request),
            publicKey,
            *negotiatedVersion,
            consumer,
            std::move(stream_ptr));

        handoff.moved = true;
        return handoff;
    }
    catch (std::exception const& e)
    {
        JLOG(journal.debug()) << "Peer " << remote_endpoint
                              << " fails handshake (" << e.what() << ")";

        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot, request, remote_endpoint.address(), e.what());
        handoff.keep_alive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
bool
P2POverlayImpl<PeerImp_t>::isPeerUpgrade(http_request_type const& request)
{
    if (!is_upgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

template <typename PeerImp_t>
std::string
P2POverlayImpl<PeerImp_t>::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

template <typename PeerImp_t>
std::shared_ptr<Writer>
P2POverlayImpl<PeerImp_t>::makeRedirectResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address)
{
    boost::beast::http::response<json_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::service_unavailable);
    msg.insert("Server", BuildInfo::getFullVersionString());
    {
        std::ostringstream ostr;
        ostr << remote_address;
        msg.insert("Remote-Address", ostr.str());
    }
    msg.insert("Content-Type", "application/json");
    msg.insert(boost::beast::http::field::connection, "close");
    msg.body() = Json::objectValue;
    {
        Json::Value& ips = (msg.body()["peer-ips"] = Json::arrayValue);
        for (auto const& _ : m_peerFinder->redirect(slot))
            ips.append(_.address.to_string());
    }
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

template <typename PeerImp_t>
std::shared_ptr<Writer>
P2POverlayImpl<PeerImp_t>::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address,
    std::string text)
{
    boost::beast::http::response<boost::beast::http::empty_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::bad_request);
    msg.reason("Bad Request (" + text + ")");
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Remote-Address", remote_address.to_string());
    msg.insert(boost::beast::http::field::connection, "close");
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::connect(beast::IP::Endpoint const& remote_endpoint)
{
    assert(work_);

    auto usage = resourceManager().newOutboundEndpoint(remote_endpoint);
    if (usage.disconnect())
    {
        JLOG(journal_.info()) << "Over resource limit: " << remote_endpoint;
        return;
    }

    auto const slot = peerFinder().new_outbound_slot(remote_endpoint);
    if (slot == nullptr)
    {
        JLOG(journal_.debug()) << "Connect: No slot for " << remote_endpoint;
        return;
    }

    auto const p = mkConnectAttempt(remote_endpoint, usage, slot, next_id_++);

    std::lock_guard lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::start()
{
    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        p2pConfig_->config(),
        overlayPort_,
        !p2pConfig_->isValidator(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);
    m_peerFinder->start();

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps = p2pConfig_->config().IPS.empty()
        ? p2pConfig_->config().IPS_FIXED
        : p2pConfig_->config().IPS;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.push_back("r.ripple.com 51235");

        // Pool of servers operated by Alloy Networks - https://www.alloy.ee
        bootstrapIps.push_back("zaphod.alloy.ee 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.push_back("sahyadri.isrdc.in 51235");
    }

    m_resolver.resolve(
        bootstrapIps,
        [this](
            std::string const& name,
            std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                    ips.push_back(to_string(addr.at_port(DEFAULT_PEER_PORT)));
                else
                    ips.push_back(to_string(addr));
            }

            std::string const base("config: ");
            if (!ips.empty())
                m_peerFinder->addFallbackStrings(base + name, ips);
        });

    // Add the ips_fixed from the rippled.cfg file
    if (!p2pConfig_->config().standalone() &&
        !p2pConfig_->config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            p2pConfig_->config().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                        ips.emplace_back(addr.address(), DEFAULT_PEER_PORT);
                    else
                        ips.emplace_back(addr);
                }

                if (!ips.empty())
                    m_peerFinder->addFixedPeer(name, ips);
            });
    }

    auto const timer = std::make_shared<Timer>(*this);
    addChild(timer);
    timer_ = timer;
    timer->async_wait();
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::stop()
{
    strand_.dispatch(std::bind(&P2POverlayImpl::stopChildren, this));
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cond_.wait(lock, [this] { return list_.empty(); });
    }
    m_peerFinder->stop();
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
int
P2POverlayImpl<PeerImp_t>::limit()
{
    return m_peerFinder->config().maxPeers;
}

template <typename PeerImp_t>
bool
P2POverlayImpl<PeerImp_t>::processRequest(
    const http_request_type& req,
    Handoff& handoff)
{
    return onEvtProcessRequest(req, handoff);
}

//------------------------------------------------------------------------------

/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the Ripple protocol.
*/
template <typename PeerImp_t>
std::size_t
P2POverlayImpl<PeerImp_t>::size() const
{
    std::lock_guard lock(mutex_);
    return peers_.size();
}

template <typename PeerImp_t>
Overlay::PeerSequence
P2POverlayImpl<PeerImp_t>::getActivePeers() const
{
    Overlay::PeerSequence ret;
    ret.reserve(size());

    for_each([&ret](std::shared_ptr<PeerImp_t>&& sp) {
        ret.emplace_back(std::move(sp));
    });

    return ret;
}

template <typename PeerImp_t>
std::shared_ptr<Peer>
P2POverlayImpl<PeerImp_t>::findPeerByShortID(Peer::id_t const& id) const
{
    std::lock_guard lock(mutex_);
    auto const& id_index = peers_.template get<0>();
    auto const iter = id_index.find(id);
    if (iter != id_index.end())
        return iter->peer.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
template <typename PeerImp_t>
std::shared_ptr<Peer>
P2POverlayImpl<PeerImp_t>::findPeerByPublicKey(PublicKey const& pubKey)
{
    std::lock_guard lock(mutex_);
    auto const& id_index = peers_.template get<0>();
    for (auto const& e : id_index)
    {
        if (auto peer = e.peer.lock())
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

template <typename PeerImp_t>
std::shared_ptr<PeerImp_t>
P2POverlayImpl<PeerImp_t>::findPeerBySlot(
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::shared_ptr<PeerImp_t> peer = {};
    {
        std::lock_guard lock(mutex_);
        auto const& slot_index = peers_.template get<1>();
        auto const iter = slot_index.find(slot);
        if (iter != slot_index.end())
            peer = iter->peer.lock();
    }
    return peer;
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::onPeerDeactivate(Peer::id_t id)
{
    std::lock_guard lock(mutex_);
    auto& id_index = peers_.template get<0>();
    auto it = id_index.find(id);
    if (it != id_index.end())
        id_index.erase(it);
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::remove(Child& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        cond_.notify_all();
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::stopChildren()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (OverlayImpl::remove() may be called on this same thread).  So
    // iterating directly over list_ to call child->stop() could lead to
    // undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees OverlayImpl::remove()
    // won't be called until vector<> children leaves scope.
    std::vector<std::shared_ptr<Child>> children;
    {
        std::lock_guard lock(mutex_);
        if (!work_)
            return;
        work_ = std::nullopt;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect(addr);
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::addInboundPeer(
    Peer::id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    auto peer = mkInboundPeer(
        id,
        slot,
        std::move(request),
        publicKey,
        protocol,
        consumer,
        std::move(stream_ptr));
    add_active(peer);
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::addOutboundPeer(
    std::unique_ptr<stream_type>&& stream_ptr,
    boost::beast::multi_buffer const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Peer::id_t id)
{
    auto peer = mkOutboundPeer(
        std::move(stream_ptr),
        buffers,
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id);
    add_active(peer);
}

template <typename PeerImp_t>
std::shared_ptr<ConnectAttempt<PeerImp_t>>
P2POverlayImpl<PeerImp_t>::mkConnectAttempt(
    beast::IP::Endpoint const& remote_endpoint,
    Resource::Consumer const& usage,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    std::uint16_t id)
{
    return std::make_shared<ConnectAttempt<PeerImp_t>>(
        *p2pConfig_,
        io_service_,
        beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
        usage,
        setup_.context,
        id,
        slot,
        p2pConfig_->logs().journal("Peer"),
        *this);
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::add_active(std::shared_ptr<PeerImp_t> const& peer)
{
    std::lock_guard lock(mutex_);

    list_.emplace(peer.get(), peer);
    auto result = peers_.emplace(peer->id(), peer->slot(), peer);
    JLOG(journal_.trace()) << peer->slot()->inbound() << " " << peer->id()
                           << " " << peer->getRemoteAddress() << std::endl;
    assert(result.second);
    (void)result.second;

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

template <typename PeerImp_t>
void
P2POverlayImpl<PeerImp_t>::sendEndpoints()
{
    auto const result = m_peerFinder->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        if (auto peer = findPeerBySlot(e.first))
            peer->sendEndpoints(e.second.begin(), e.second.end());
    }
}

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
