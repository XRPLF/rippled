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

#include "PeerDoor.h"
#include <boost/config.hpp>
#include <condition_variable>
#include <mutex>

namespace ripple {

class PeersLog;
template <> char const* LogPartition::getPartitionName <PeersLog> () { return "Peers"; }

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

    void operator() (Peer::ref peer)
    {
        json.append (peer->json ());
    }

    Json::Value operator() ()
    {
        return json;
    }
};

//------------------------------------------------------------------------------

class PeersImp
    : public Peers
    , public PeerFinder::Callback
    , public LeakChecked <PeersImp>
{    
public:
    typedef boost::unordered_map <IPAddress, Peer::pointer> PeerByIP;

    typedef boost::unordered_map <
        RippleAddress, Peer::pointer> PeerByPublicKey;

    typedef boost::unordered_map <
        Peer::ShortId, Peer::pointer> PeerByShortId;

    std::recursive_mutex m_mutex;

    // Blocks us until dependent objects have been destroyed
    std::condition_variable_any m_cond;

    // Number of dependencies that must be destroyed before we can stop
    std::size_t m_child_count;

    Journal m_journal;
    Resource::Manager& m_resourceManager;

    std::unique_ptr <PeerFinder::Manager> m_peerFinder;

    boost::asio::io_service& m_io_service;
    boost::asio::ssl::context& m_ssl_context;

    /** Tracks peers by their IP address and port */
    PeerByIP m_ipMap;

    /** Tracks peers by their public key */
    PeerByPublicKey m_publicKeyMap;

    /** Tracks peers by their session ID */
    PeerByShortId m_shortIdMap;

    /** Tracks all instances of peer objects */
    List <Peer> m_list;

    /** The peer door for regular SSL connections */
    std::unique_ptr <PeerDoor> m_doorDirect;

    /** The peer door for proxy connections */
    std::unique_ptr <PeerDoor> m_doorProxy;

    /** The resolver we use for peer hostnames */
    Resolver& m_resolver;

    /** Monotically increasing identifiers for peers */
    Atomic <Peer::ShortId> m_nextShortId;

    //--------------------------------------------------------------------------
    //
    // Peers
    //
    //--------------------------------------------------------------------------

    PeersImp (Stoppable& parent,
        Resource::Manager& resourceManager,
            SiteFiles::Manager& siteFiles,
                Resolver& resolver,
                    boost::asio::io_service& io_service,
                        boost::asio::ssl::context& ssl_context)
        : Peers (parent)
        , m_child_count (1)
        , m_journal (LogPartition::getJournal <PeersLog> ())
        , m_resourceManager (resourceManager)
        , m_peerFinder (add (PeerFinder::Manager::New (
            *this,
            siteFiles,
            *this,
            get_seconds_clock (),
            LogPartition::getJournal <PeerFinderLog> ())))
        , m_io_service (io_service)
        , m_ssl_context (ssl_context)
        , m_resolver (resolver)
    {

    }

    ~PeersImp ()
    {
        // Block until dependent objects have been destroyed.
        // This is just to catch improper use of the Stoppable API.
        //
        std::unique_lock <decltype(m_mutex)> lock (m_mutex);
        m_cond.wait (lock, [this] {
            return this->m_child_count == 0; });
    }

    void accept (
        bool proxyHandshake,
        boost::shared_ptr <NativeSocketType> const& socket)
    {
        Peer::accept (
            socket,
            *this,
            m_resourceManager,
            *m_peerFinder,
            m_ssl_context,
            proxyHandshake);
    }

    void connect (IP::Endpoint const& remote_address)
    {
        Peer::connect (
            remote_address,
            m_io_service,
            *this,
            m_resourceManager,
            *m_peerFinder,
            m_ssl_context);
    }

    //--------------------------------------------------------------------------

    // Check for the stopped condition
    // Caller must hold the mutex
    void check_stopped ()
    {
        // To be stopped, child Stoppable objects must be stopped
        // and the count of dependent objects must be zero
        if (areChildrenStopped () && m_child_count == 0)
        {
            m_cond.notify_all ();
            stopped ();
        }
    }

    // Increment the count of dependent objects
    // Caller must hold the mutex
    void addref ()
    {
        ++m_child_count;
    }

    // Decrement the count of dependent objects
    // Caller must hold the mutex
    void release ()
    {
        if (--m_child_count == 0)
            check_stopped ();
    }

    void peerCreated (Peer* peer)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        m_list.push_back (*peer);
        addref();
    }

    void peerDestroyed (Peer* peer)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        m_list.erase (m_list.iterator_to (*peer));
        release();
    }

    //--------------------------------------------------------------------------
    //
    // PeerFinder::Callback
    //
    //--------------------------------------------------------------------------

    void connectPeers (std::vector <IPAddress> const& list)
    {
        for (std::vector <IPAddress>::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
            connect (*iter);
    }

    void disconnectPeer (IPAddress const& address, bool graceful)
    {
        m_journal.trace <<
            "disconnectPeer (" << address <<
            ", " << graceful << ")";

        std::lock_guard <decltype(m_mutex)> lock (m_mutex);

        PeerByIP::iterator const it (m_ipMap.find (address));

        if (it != m_ipMap.end ())
            it->second->detach ("disc", false);
    }

    void activatePeer (IPAddress const& remote_address)
    {
        m_journal.trace <<
            "activatePeer (" << remote_address << ")";

        std::lock_guard <decltype(m_mutex)> lock (m_mutex);

        PeerByIP::iterator const it (m_ipMap.find (remote_address));

        if (it != m_ipMap.end ())
            it->second->activate();
    }

    void sendEndpoints (IPAddress const& remote_address,
        std::vector <PeerFinder::Endpoint> const& endpoints)
    {
        bassert (! endpoints.empty());
        typedef std::vector <PeerFinder::Endpoint> List;
        protocol::TMEndpoints tm;
        for (List::const_iterator iter (endpoints.begin());
            iter != endpoints.end(); ++iter)
        {
            PeerFinder::Endpoint const& ep (*iter);
            protocol::TMEndpoint& tme (*tm.add_endpoints());
            if (ep.address.is_v4())
                tme.mutable_ipv4()->set_ipv4(
                    toNetworkByteOrder (ep.address.to_v4().value));
            else
                tme.mutable_ipv4()->set_ipv4(0);
            tme.mutable_ipv4()->set_ipv4port (ep.address.port());

            tme.set_hops (ep.hops);
        }

        tm.set_version (1);

        PackedMessage::pointer msg (
            boost::make_shared <PackedMessage> (
                tm, protocol::mtENDPOINTS));

        {
            std::lock_guard <decltype(m_mutex)> lock (m_mutex);
            PeerByIP::iterator const iter (m_ipMap.find (remote_address));
            // Address must exist!
            check_postcondition (iter != m_ipMap.end());
            Peer::pointer peer (iter->second);
            // VFALCO TODO Why are we checking isConnected? That should not be needed
            if (peer->isConnected())
                peer->sendPacket (msg, false);
        }
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
        PeerFinder::Config config;

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

        // Add the static IPs from the rippled.cfg file
        m_peerFinder->addFallbackStrings ("rippled.cfg", getConfig().IPS);

        // Add the ips_fixed from the rippled.cfg file
        if (! getConfig ().RUN_STANDALONE && !getConfig ().IPS_FIXED.empty ())
        {
            struct resolve_fixed_peers
            {
                PeerFinder::Manager* m_peerFinder;

                resolve_fixed_peers (PeerFinder::Manager* peerFinder)
                    : m_peerFinder (peerFinder)
                { }

                void operator()(std::string const& name,
                    std::vector <IPAddress> const& address)
                {
                    if (!address.empty())
                        m_peerFinder->addFixedPeer (name, address);
                }
            };

            m_resolver.resolve (getConfig ().IPS_FIXED,
                resolve_fixed_peers (m_peerFinder.get ()));
        }

        // Configure the peer doors, which allow the server to accept incoming
        // peer connections:
        // Create the listening sockets for peers
        //
        m_doorDirect.reset (PeerDoor::New (
            PeerDoor::sslRequired,
            *this,
            getConfig ().PEER_IP,
            getConfig ().peerListeningPort,
            m_io_service));

        if (getConfig ().peerPROXYListeningPort != 0)
        {
            m_doorProxy.reset (PeerDoor::New (
                PeerDoor::sslAndPROXYRequired,
                *this,
                getConfig ().PEER_IP,
                getConfig ().peerPROXYListeningPort,
                m_io_service));
        }
    }

    void onStart ()
    {
    }

    // Close all peer connections. If graceful is true then the peer objects
    // will wait for pending i/o before closing the socket. No new data will
    // be sent.
    //
    // The caller must hold the mutex
    //
    // VFALCO TODO implement the graceful flag
    //
    void close_all (bool graceful)
    {
        for (List <Peer>::iterator iter (m_list.begin ());
            iter != m_list.end(); ++iter)
            iter->detach ("stop", false);
    }

    void onStop ()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        // Take off the extra count we added in the constructor
        release();
        // Close all peers
        close_all (true);
    }

    void onChildrenStopped ()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        check_stopped ();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (PropertyStream& stream)
    {
    }

    //--------------------------------------------------------------------------
    /** A peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void onPeerActivated (Peer::ref peer)
    {
        // First assign this peer a new short ID
        peer->setShortId(++m_nextShortId);

        std::lock_guard <decltype(m_mutex)> lock (m_mutex);

        // Now track this peer
        std::pair<PeerByShortId::iterator, bool> idResult(
            m_shortIdMap.emplace (
                boost::unordered::piecewise_construct,
                boost::make_tuple (peer->getShortId()),
                boost::make_tuple (peer)));
        check_postcondition(idResult.second);

        std::pair<PeerByPublicKey::iterator, bool> keyResult(
            m_publicKeyMap.emplace (
                boost::unordered::piecewise_construct,
                boost::make_tuple (peer->getNodePublic()),
                boost::make_tuple (peer)));
        check_postcondition(keyResult.second);

        m_journal.debug << 
            "activated " << peer->getRemoteAddress() <<
            " (" << peer->getShortId() << 
            ":" << RipplePublicKey(peer->getNodePublic()) << ")";

        // We just accepted this peer so we have non-zero active peers
        check_postcondition(size() != 0);
    }

    /** A peer is being disconnected
        This is called during the disconnection of a known, activated peer. It
        will not be called for outbound peer connections that don't succeed or
        for connections of peers that are dropped prior to being activated.
    */
    void onPeerDisconnect (Peer::ref peer)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        m_shortIdMap.erase (peer->getShortId ());
        m_publicKeyMap.erase (peer->getNodePublic ());
    }

    /** The number of active peers on the network
        Active peers are only those peers that have completed the handshake
        and are running the Ripple protocol.
    */
    std::size_t size ()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        return m_publicKeyMap.size ();
    }

    // Returns information on verified peers.
    Json::Value json ()
    {
        return foreach (get_peer_json());
    }

    Peers::PeerSequence getActivePeers ()
    {
        Peers::PeerSequence ret;

        std::lock_guard <decltype(m_mutex)> lock (m_mutex);

        ret.reserve (m_publicKeyMap.size ());

        BOOST_FOREACH (PeerByPublicKey::value_type const& pair, m_publicKeyMap)
        {
            assert (!!pair.second);
            ret.push_back (pair.second);
        }

        return ret;
    }

    Peer::pointer findPeerByShortID (Peer::ShortId const& id)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        PeerByShortId::iterator const iter (
            m_shortIdMap.find (id));
        if (iter != m_shortIdMap.end ())
            iter->second;
        return Peer::pointer();
    }

    // TODO NIKB Rename these two functions. It's not immediately clear
    //           what they do: create a tracking entry for a peer by
    //           the peer's remote IP.
    /** Start tracking a peer */
    void addPeer (Peer::Ptr const& peer)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);

        check_precondition (! isStopping ());

        m_journal.error << "Adding peer: " << peer->getRemoteAddress();
        
        std::pair <PeerByIP::iterator, bool> result (m_ipMap.emplace (
            boost::unordered::piecewise_construct,
                boost::make_tuple (peer->getRemoteAddress()),
                    boost::make_tuple (peer)));

        check_postcondition (result.second);
    }

    /** Stop tracking a peer */
    void removePeer (Peer::Ptr const& peer)
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        m_ipMap.erase (peer->getRemoteAddress());
    }
};

//------------------------------------------------------------------------------

Peers::~Peers ()
{
}

Peers* Peers::New (Stoppable& parent,
    Resource::Manager& resourceManager,
        SiteFiles::Manager& siteFiles,
            Resolver& resolver,
                boost::asio::io_service& io_service,
                    boost::asio::ssl::context& ssl_context)
{
    return new PeersImp (parent, resourceManager, siteFiles, 
        resolver, io_service, ssl_context);
}

}
