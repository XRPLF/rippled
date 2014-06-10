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

#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/PeerDoor.h>
#include <ripple/overlay/impl/PeerImp.h>

#include <beast/ByteOrder.h>

#if DOXYGEN
#include <ripple/overlay/README.md>
#endif

namespace ripple {

SETUP_LOG (Peer)

class PeersLog;
template <> char const* LogPartition::getPartitionName <PeersLog> () { return "Overlay"; }

class PeerFinderLog;
template <> char const* LogPartition::getPartitionName <PeerFinderLog> () { return "PeerFinder"; }

class NameResolverLog;
template <> char const* LogPartition::getPartitionName <NameResolverLog> () { return "NameResolver"; }

/** Calls a function during static initialization. */
struct static_call
{
    // Function must be callable as
    //      void f (void) const
    //
    template <class Function>
    static_call (Function const& f)
    {
        f ();
    }
};

static static_call init_PeerFinderLog (&LogPartition::get <PeerFinderLog>);
static static_call init_NameResolverLog (&LogPartition::get <NameResolverLog>);

//------------------------------------------------------------------------------

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

OverlayImpl::OverlayImpl (Stoppable& parent,
    Resource::Manager& resourceManager,
    SiteFiles::Manager& siteFiles,
    beast::File const& pathToDbFileOrDirectory,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    boost::asio::ssl::context& ssl_context)
    : Overlay (parent)
    , m_child_count (1)
    , m_journal (LogPartition::getJournal <PeersLog> ())
    , m_resourceManager (resourceManager)
    , m_peerFinder (add (PeerFinder::Manager::New (
        *this,
        siteFiles,
        pathToDbFileOrDirectory,
        *this,
        get_seconds_clock (),
        LogPartition::getJournal <PeerFinderLog> ())))
    , m_io_service (io_service)
    , m_ssl_context (ssl_context)
    , m_resolver (resolver)
{
}

OverlayImpl::~OverlayImpl ()
{
    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock <decltype(m_mutex)> lock (m_mutex);
    m_cond.wait (lock, [this] {
        return this->m_child_count == 0; });
}

void
OverlayImpl::accept (bool proxyHandshake, socket_type&& socket)
{
    // An error getting an endpoint means the connection closed.
    // Just do nothing and the socket will be closed by the caller.
    boost::system::error_code ec;
    auto const local_endpoint_native (socket.local_endpoint (ec));
    if (ec)
        return;
    auto const remote_endpoint_native (socket.remote_endpoint (ec));
    if (ec)
        return;

    auto const local_endpoint (
        beast::IPAddressConversion::from_asio (local_endpoint_native));
    auto const remote_endpoint (
        beast::IPAddressConversion::from_asio (remote_endpoint_native));

    PeerFinder::Slot::ptr const slot (m_peerFinder->new_inbound_slot (
        local_endpoint, remote_endpoint));

    if (slot == nullptr)
        return;

    MultiSocket::Flag flags (
        MultiSocket::Flag::server_role | MultiSocket::Flag::ssl_required);

    if (proxyHandshake)
        flags = flags.with (MultiSocket::Flag::proxy);

    PeerImp::ptr const peer (std::make_shared <PeerImp> (
        std::move (socket), remote_endpoint, *this, m_resourceManager,
            *m_peerFinder, slot, m_ssl_context, flags));

    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        {
            std::pair <PeersBySlot::iterator, bool> const result (
                m_peers.emplace (slot, peer));
            assert (result.second);
        }
        ++m_child_count;

        // This has to happen while holding the lock,
        // otherwise the socket might not be canceled during a stop.
        peer->start ();
    }
}

void
OverlayImpl::connect (beast::IP::Endpoint const& remote_endpoint)
{
    if (isStopping())
    {
        m_journal.debug <<
            "Skipping " << remote_endpoint <<
            " connect on stop";
        return;
    }

    PeerFinder::Slot::ptr const slot (
        m_peerFinder->new_outbound_slot (remote_endpoint));

    if (slot == nullptr)
        return;

    MultiSocket::Flag const flags (
        MultiSocket::Flag::client_role | MultiSocket::Flag::ssl);

    PeerImp::ptr const peer (std::make_shared <PeerImp> (
        remote_endpoint, m_io_service, *this, m_resourceManager,
            *m_peerFinder, slot, m_ssl_context, flags));

    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        {
            std::pair <PeersBySlot::iterator, bool> const result (
                m_peers.emplace (slot, peer));
            assert (result.second);
        }
        ++m_child_count;

        // This has to happen while holding the lock,
        // otherwise the socket might not be canceled during a stop.
        peer->start ();
    }
}

Peer::ShortId
OverlayImpl::next_id()
{
    return ++m_nextShortId;
}

//--------------------------------------------------------------------------

// Check for the stopped condition
// Caller must hold the mutex
void
OverlayImpl::check_stopped ()
{
    // To be stopped, child Stoppable objects must be stopped
    // and the count of dependent objects must be zero
    if (areChildrenStopped () && m_child_count == 0)
    {
        m_cond.notify_all ();
        m_journal.info <<
            "Stopped.";
        stopped ();
    }
}

// Decrement the count of dependent objects
// Caller must hold the mutex
void
OverlayImpl::release ()
{
    if (--m_child_count == 0)
        check_stopped ();
}

void
OverlayImpl::remove (PeerFinder::Slot::ptr const& slot)
{
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);

    PeersBySlot::iterator const iter (m_peers.find (slot));
    assert (iter != m_peers.end ());
    m_peers.erase (iter);

    release();
}

//--------------------------------------------------------------------------
//
// PeerFinder::Callback
//
//--------------------------------------------------------------------------

void
OverlayImpl::connect (std::vector <beast::IP::Endpoint> const& list)
{
    for (std::vector <beast::IP::Endpoint>::const_iterator iter (list.begin());
        iter != list.end(); ++iter)
        connect (*iter);
}

void
OverlayImpl::activate (PeerFinder::Slot::ptr const& slot)
{
    m_journal.trace <<
        "Activate " << slot->remote_endpoint();

    std::lock_guard <decltype(m_mutex)> lock (m_mutex);

    PeersBySlot::iterator const iter (m_peers.find (slot));
    assert (iter != m_peers.end ());
    PeerImp::ptr const peer (iter->second.lock());
    assert (peer != nullptr);
    peer->activate ();
}

void
OverlayImpl::send (PeerFinder::Slot::ptr const& slot,
    std::vector <PeerFinder::Endpoint> const& endpoints)
{
    typedef std::vector <PeerFinder::Endpoint> List;
    protocol::TMEndpoints tm;
    for (List::const_iterator iter (endpoints.begin());
        iter != endpoints.end(); ++iter)
    {
        PeerFinder::Endpoint const& ep (*iter);
        protocol::TMEndpoint& tme (*tm.add_endpoints());
        if (ep.address.is_v4())
            tme.mutable_ipv4()->set_ipv4(
                beast::toNetworkByteOrder (ep.address.to_v4().value));
        else
            tme.mutable_ipv4()->set_ipv4(0);
        tme.mutable_ipv4()->set_ipv4port (ep.address.port());

        tme.set_hops (ep.hops);
    }

    tm.set_version (1);

    Message::pointer msg (
        std::make_shared <Message> (
            tm, protocol::mtENDPOINTS));

    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        PeersBySlot::iterator const iter (m_peers.find (slot));
        assert (iter != m_peers.end ());
        PeerImp::ptr const peer (iter->second.lock());
        assert (peer != nullptr);
        peer->send (msg);
    }
}

void
OverlayImpl::disconnect (PeerFinder::Slot::ptr const& slot, bool graceful)
{
    if (m_journal.trace) m_journal.trace <<
        "Disconnect " << slot->remote_endpoint () <<
        (graceful ? "gracefully" : "");

    std::lock_guard <decltype(m_mutex)> lock (m_mutex);

    PeersBySlot::iterator const iter (m_peers.find (slot));
    assert (iter != m_peers.end ());
    PeerImp::ptr const peer (iter->second.lock());
    assert (peer != nullptr);
    peer->close (graceful);
    //peer->detach ("disc", false);
}

//--------------------------------------------------------------------------
//
// Stoppable
//
//--------------------------------------------------------------------------

void
OverlayImpl::onPrepare ()
{
    PeerFinder::Config config;

    if (getConfig ().PEERS_MAX != 0)
        config.maxPeers = getConfig ().PEERS_MAX;

    config.outPeers = config.calcOutPeers();

    config.wantIncoming =
        (! getConfig ().PEER_PRIVATE) &&
        (getConfig().peerListeningPort != 0);

    // if it's a private peer or we are running as standalone
    // automatic connections would defeat the purpose.
    config.autoConnect =
        !getConfig().RUN_STANDALONE &&
        !getConfig().PEER_PRIVATE;

    config.listeningPort = getConfig().peerListeningPort;

    config.features = "";

    // Enforce business rules
    config.applyTuning ();

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

    // Configure the peer doors, which allow the server to accept incoming
    // peer connections:
    if (! getConfig ().RUN_STANDALONE)
    {
        m_doorDirect = make_PeerDoor (
            PeerDoor::sslRequired,
            *this,
            getConfig ().PEER_IP,
            getConfig ().peerListeningPort,
            m_io_service);

        if (getConfig ().peerPROXYListeningPort != 0)
        {
            m_doorProxy = make_PeerDoor (
                PeerDoor::sslAndPROXYRequired,
                *this,
                getConfig ().PEER_IP,
                getConfig ().peerPROXYListeningPort,
                m_io_service);
        }
    }
}

void
OverlayImpl::onStart ()
{
}

/** Close all peer connections.
    If `graceful` is true then active 
    Requirements:
        Caller must hold the mutex.
*/
void
OverlayImpl::close_all (bool graceful)
{
    for (auto const& entry : m_peers)
    {
        PeerImp::ptr const peer (entry.second.lock());

        // VFALCO The only case where the weak_ptr is expired should be if
        //        ~PeerImp is pre-empted before it calls m_peers.remove()
        //
        if (peer != nullptr)
            peer->close (graceful);
    }
}

void
OverlayImpl::onStop ()
{
    if (m_doorDirect)
        m_doorDirect->stop();
    if (m_doorProxy)
        m_doorProxy->stop();

    std::lock_guard <decltype(m_mutex)> lock (m_mutex);
    // Take off the extra count we added in the constructor
    release();

    close_all (false);       
}

void
OverlayImpl::onChildrenStopped ()
{
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);
    check_stopped ();
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
OverlayImpl::onPeerActivated (Peer::ptr const& peer)
{
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);

    // Now track this peer
    {
        auto const result (m_shortIdMap.emplace (
            std::piecewise_construct,
                std::make_tuple (peer->getShortId()),
                    std::make_tuple (peer)));
        assert(result.second);
    }

    {
        auto const result (m_publicKeyMap.emplace (
            boost::unordered::piecewise_construct,
                boost::make_tuple (peer->getNodePublic()),
                    boost::make_tuple (peer)));
        assert(result.second);
    }

    m_journal.debug << 
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
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);
    m_shortIdMap.erase (peer->getShortId ());
    m_publicKeyMap.erase (peer->getNodePublic ());
}

/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the Ripple protocol.
*/
std::size_t
OverlayImpl::size ()
{
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);
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

    std::lock_guard <decltype(m_mutex)> lock (m_mutex);

    ret.reserve (m_publicKeyMap.size ());

    BOOST_FOREACH (PeerByPublicKey::value_type const& pair, m_publicKeyMap)
    {
        assert (!!pair.second);
        ret.push_back (pair.second);
    }

    return ret;
}

Peer::ptr
OverlayImpl::findPeerByShortID (Peer::ShortId const& id)
{
    std::lock_guard <decltype(m_mutex)> lock (m_mutex);
    PeerByShortId::iterator const iter (
        m_shortIdMap.find (id));
    if (iter != m_shortIdMap.end ())
        return iter->second;
    return Peer::ptr();
}

//------------------------------------------------------------------------------

std::unique_ptr <Overlay>
make_Overlay (
    beast::Stoppable& parent,
    Resource::Manager& resourceManager,
    SiteFiles::Manager& siteFiles,
    beast::File const& pathToDbFileOrDirectory, 
    Resolver& resolver,
    boost::asio::io_service& io_service,
    boost::asio::ssl::context& ssl_context)
{
    return std::make_unique <OverlayImpl> (parent, resourceManager, siteFiles,
        pathToDbFileOrDirectory, resolver, io_service, ssl_context);
}

}
