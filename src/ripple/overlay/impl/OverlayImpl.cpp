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

#include <ripple/basics/Log.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/server/JsonWriter.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/peerfinder/make_Manager.h>
#include <beast/ByteOrder.h>

namespace ripple {

/** A functor to visit all active peers and retrieve their JSON data */
struct get_peer_json
{
    typedef Json::Value return_type;

    Json::Value json;

    get_peer_json ()
    { }

    void operator() (Peer::ptr const& peer)
    {
        json.append (peer->json ());
    }

    Json::Value operator() ()
    {
        return json;
    }
};

//------------------------------------------------------------------------------

OverlayImpl::Child::Child (OverlayImpl& overlay)
    : overlay_(overlay)
{
}

OverlayImpl::Child::~Child()
{
    overlay_.remove(*this);
}

//------------------------------------------------------------------------------

OverlayImpl::Timer::Timer (OverlayImpl& overlay)
    : Child(overlay)
    , timer_(overlay_.io_service_)
{
}

void
OverlayImpl::Timer::close()
{
    error_code ec;
    timer_.cancel(ec);
}

void
OverlayImpl::Timer::run()
{
    error_code ec;
    timer_.expires_from_now (std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(
        std::bind(&Timer::on_timer, shared_from_this(),
            beast::asio::placeholders::error)));
}

void
OverlayImpl::Timer::on_timer (error_code ec)
{
    if (ec || overlay_.isStopping())
    {
        if (ec && ec != boost::asio::error::operation_aborted)
            if (overlay_.journal_.error) overlay_.journal_.error <<
                "on_timer: " << ec.message();
        return;
    }

    overlay_.m_peerFinder->once_per_second();
    overlay_.sendEndpoints();
    overlay_.autoConnect();

    timer_.expires_from_now (std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, shared_from_this(),
            beast::asio::placeholders::error)));
}

//------------------------------------------------------------------------------

OverlayImpl::OverlayImpl (
    Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    beast::File const& pathToDbFileOrDirectory,
    Resolver& resolver,
    boost::asio::io_service& io_service)
    : Overlay (parent)
    , io_service_ (io_service)
    , work_ (boost::in_place(std::ref(io_service_)))
    , strand_ (io_service_)
    , setup_(setup)
    , journal_ (deprecatedLogs().journal("Overlay"))
    , serverHandler_(serverHandler)
    , m_resourceManager (resourceManager)
    , m_peerFinder (PeerFinder::make_Manager (*this, io_service,
        pathToDbFileOrDirectory, get_seconds_clock(),
            deprecatedLogs().journal("PeerFinder")))
    , m_resolver (resolver)
    , m_nextShortId (0)
{
    beast::PropertyStream::Source::add (m_peerFinder.get());
}

OverlayImpl::~OverlayImpl ()
{
    close();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock <decltype(mutex_)> lock (mutex_);
    cond_.wait (lock, [this] { return list_.empty(); });
}

//------------------------------------------------------------------------------

void
OverlayImpl::onLegacyPeerHello (
    std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        boost::asio::const_buffer buffer,
            boost::asio::ip::tcp::endpoint remote_address)
{
    error_code ec;
    auto const local_endpoint (ssl_bundle->socket.local_endpoint(ec));
    if (ec)
        return;

    auto const slot = m_peerFinder->new_inbound_slot (
        beast::IPAddressConversion::from_asio(local_endpoint),
            beast::IPAddressConversion::from_asio(remote_address));

    if (slot != nullptr)
        return addpeer (std::make_shared<PeerImp>(std::move(ssl_bundle),
            boost::asio::const_buffers_1(buffer),
                beast::IPAddressConversion::from_asio(remote_address),
                    *this, m_resourceManager, *m_peerFinder, slot));
}

Handoff
OverlayImpl::onHandoff (std::unique_ptr <beast::asio::ssl_bundle>&& ssl_bundle,
    beast::http::message&& request,
        boost::asio::ip::tcp::endpoint remote_address)
{
    Handoff handoff;
    if (! isPeerUpgrade(request))
        return handoff;

    error_code ec;
    auto const local_endpoint (ssl_bundle->socket.local_endpoint(ec));
    if (ec)
    {
        // log?
        // Since we don't call std::move the socket will be closed.
        handoff.moved = false;
        return handoff;
    }

    // TODO Validate HTTP request

    auto const slot = m_peerFinder->new_inbound_slot (
        beast::IPAddressConversion::from_asio(local_endpoint),
            beast::IPAddressConversion::from_asio(remote_address));

    if (slot == nullptr)
    {
        // self connect
        handoff.moved = false;
        return handoff;
    }

    // For now, always redirect
    // Full, give them some addresses
    handoff.response = makeRedirectResponse(slot, request);
    handoff.keep_alive = request.keep_alive();
    return handoff;
}

//------------------------------------------------------------------------------

bool
OverlayImpl::isPeerUpgrade(beast::http::message const& request)
{
    if (! request.upgrade())
        return false;
    if (request.headers["Upgrade"] != "Ripple/1.2")
        return false;
    return true;
}

std::shared_ptr<HTTP::Writer>
OverlayImpl::makeRedirectResponse (PeerFinder::Slot::ptr const& slot,
    beast::http::message const& request)
{
    Json::Value json(Json::objectValue);
    {
        auto const result = m_peerFinder->redirect(slot);
        Json::Value& ips = (json["peer-ips"] = Json::arrayValue);
        for (auto const& _ : m_peerFinder->redirect(slot))
            ips.append(_.address.to_string());
    }

    beast::http::message m;
    m.request(false);
    m.status(503);
    m.reason("Service Unavailable");
    m.version(request.version());
    if (request.version() == std::make_pair(1, 0))
    {
        //?
    }
    auto const response = HTTP::make_JsonWriter (m, json);
    return response;
}

//------------------------------------------------------------------------------

void
OverlayImpl::connect (beast::IP::Endpoint const& remote_endpoint)
{
    assert(work_);

    PeerFinder::Slot::ptr const slot (
        m_peerFinder->new_outbound_slot (remote_endpoint));

    if (slot == nullptr)
        return;

    addpeer (std::make_shared <PeerImp> (remote_endpoint, io_service_, *this,
        m_resourceManager, *m_peerFinder, slot, setup_.context));
}

Peer::ShortId
OverlayImpl::next_id()
{
    return ++m_nextShortId;
}

//--------------------------------------------------------------------------

void
OverlayImpl::remove (PeerFinder::Slot::ptr const& slot)
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    PeersBySlot::iterator const iter (m_peers.find (slot));
    assert (iter != m_peers.end ());
    m_peers.erase (iter);
}

//--------------------------------------------------------------------------
//
// Stoppable
//
//--------------------------------------------------------------------------

void
OverlayImpl::onPrepare()
{
    PeerFinder::Config config;

    if (getConfig ().PEERS_MAX != 0)
        config.maxPeers = getConfig ().PEERS_MAX;

    config.outPeers = config.calcOutPeers();

    auto const port = serverHandler_.setup().overlay.port;

    config.wantIncoming =
        (! getConfig ().PEER_PRIVATE) && (port != 0);
    // if it's a private peer or we are running as standalone
    // automatic connections would defeat the purpose.
    config.autoConnect =
        !getConfig().RUN_STANDALONE &&
        !getConfig().PEER_PRIVATE;
    config.listeningPort = port;
    config.features = "";

    // Enforce business rules
    config.applyTuning();

    m_peerFinder->setConfig (config);

    auto bootstrapIps (getConfig ().IPS);

    // If no IPs are specified, use the Ripple Labs round robin
    // pool to get some servers to insert into the boot cache.
    if (bootstrapIps.empty ())
        bootstrapIps.push_back ("r.ripple.com 51235");

    if (!bootstrapIps.empty ())
    {
        m_resolver.resolve (bootstrapIps,
            [this](
                std::string const& name,
                std::vector <beast::IP::Endpoint> const& addresses)
            {
                std::vector <std::string> ips;

                for (auto const& addr : addresses)
                    ips.push_back (to_string (addr));

                std::string const base ("config: ");

                if (!ips.empty ())
                    m_peerFinder->addFallbackStrings (base + name, ips);
            });
    }

    // Add the ips_fixed from the rippled.cfg file
    if (! getConfig ().RUN_STANDALONE && !getConfig ().IPS_FIXED.empty ())
    {
        m_resolver.resolve (getConfig ().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector <beast::IP::Endpoint> const& addresses)
            {
                if (!addresses.empty ())
                    m_peerFinder->addFixedPeer (name, addresses);
            });
    }
}

void
OverlayImpl::onStart ()
{
    auto const timer = std::make_shared<Timer>(*this);
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    list_.emplace(timer.get(), timer);
    timer_ = timer;
    timer->run();
}

void
OverlayImpl::onStop ()
{
    strand_.dispatch(std::bind(&OverlayImpl::close, this));
}

void
OverlayImpl::onChildrenStopped ()
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    checkStopped ();
}

//--------------------------------------------------------------------------
//
// PropertyStream
//
//--------------------------------------------------------------------------

void
OverlayImpl::onWrite (beast::PropertyStream::Map& stream)
{
}

//--------------------------------------------------------------------------
/** A peer has connected successfully
    This is called after the peer handshake has been completed and during
    peer activation. At this point, the peer address and the public key
    are known.
*/
void
OverlayImpl::activate (Peer::ptr const& peer)
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);

    // Now track this peer
    {
        auto const result (m_shortIdMap.emplace (
            std::piecewise_construct,
                std::make_tuple (peer->getShortId()),
                    std::make_tuple (peer)));
        assert(result.second);
        (void) result.second;
    }

    {
        auto const result (m_publicKeyMap.emplace (
            std::piecewise_construct,
                std::make_tuple (peer->getNodePublic()),
                    std::make_tuple (peer)));
        assert(result.second);
        (void) result.second;
    }

    journal_.debug <<
        "activated " << peer->getRemoteAddress() <<
        " (" << peer->getShortId() <<
        ":" << RipplePublicKey(peer->getNodePublic()) << ")";

    // We just accepted this peer so we have non-zero active peers
    assert(size() != 0);
}

/** A peer is being disconnected
    This is called during the disconnection of a known, activated peer. It
    will not be called for outbound peer connections that don't succeed or
    for connections of peers that are dropped prior to being activated.
*/
void
OverlayImpl::onPeerDisconnect (Peer::ptr const& peer)
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    m_shortIdMap.erase (peer->getShortId ());
    m_publicKeyMap.erase (peer->getNodePublic ());
}

/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the Ripple protocol.
*/
std::size_t
OverlayImpl::size()
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    return m_publicKeyMap.size ();
}

// Returns information on verified peers.
Json::Value
OverlayImpl::json ()
{
    return foreach (get_peer_json());
}

Overlay::PeerSequence
OverlayImpl::getActivePeers ()
{
    Overlay::PeerSequence ret;

    std::lock_guard <decltype(mutex_)> lock (mutex_);

    ret.reserve (m_publicKeyMap.size ());

    for (auto const& e : m_publicKeyMap)
    {
        assert (e.second);
        ret.push_back (e.second);
    }

    return ret;
}

Peer::ptr
OverlayImpl::findPeerByShortID (Peer::ShortId const& id)
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    PeerByShortId::iterator const iter (
        m_shortIdMap.find (id));
    if (iter != m_shortIdMap.end ())
        return iter->second;
    return Peer::ptr();
}

//------------------------------------------------------------------------------

void
OverlayImpl::remove (Child& child)
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        checkStopped();
}

// Caller must hold the mutex
void
OverlayImpl::checkStopped ()
{
    if (isStopping() && areChildrenStopped () && list_.empty())
        stopped();
}

void
OverlayImpl::close()
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    if (work_)
    {
        work_ = boost::none;
        for (auto& _ : list_)
        {
            auto const child = _.second.lock();
            // Happens when the child is about to be destroyed
            if (child != nullptr)
                child->close();
        }
    }
}

void
OverlayImpl::addpeer (std::shared_ptr<PeerImp> const& peer)
{
    std::lock_guard <decltype(mutex_)> lock (mutex_);
    {
        std::pair <PeersBySlot::iterator, bool> const result (
            m_peers.emplace (peer->slot(), peer));
        assert (result.second);
        (void) result.second;
    }
    list_.emplace(peer.get(), peer);

    // This has to happen while holding the lock,
    // otherwise the socket might not be canceled during a stop.
    peer->start();
}

void
OverlayImpl::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect (addr);
}

void
OverlayImpl::sendEndpoints()
{
    auto const result = m_peerFinder->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        // VFALCO TODO Make sure this doesn't race with closing the peer
        PeerImp::ptr peer;
        {
            std::lock_guard <decltype(mutex_)> lock (mutex_);
            PeersBySlot::iterator const iter = m_peers.find (e.first);
            if (iter != m_peers.end())
                peer = iter->second.lock();
        }
        if (peer)
            peer->send_endpoints (e.second.begin(), e.second.end());
    }
}


//------------------------------------------------------------------------------

Overlay::Setup
setup_Overlay (BasicConfig const& config)
{
    Overlay::Setup setup;
    auto const& section = config.section("overlay");
    set (setup.http_handshake, "http_handshake", section);
    set (setup.auto_connect, "auto_connect", section);
    std::string promote;
    set (promote, "become_superpeer", section);
    if (promote == "never")
        setup.promote = Overlay::Promote::never;
    else if (promote == "always")
        setup.promote = Overlay::Promote::always;
    else
        setup.promote = Overlay::Promote::automatic;
    setup.context = make_SSLContext();
    return setup;
}

std::unique_ptr <Overlay>
make_Overlay (
    Overlay::Setup const& setup,
    beast::Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    beast::File const& pathToDbFileOrDirectory,
    Resolver& resolver,
    boost::asio::io_service& io_service)
{
    return std::make_unique <OverlayImpl> (setup, parent, serverHandler,
        resourceManager, pathToDbFileOrDirectory, resolver, io_service);
}

}
