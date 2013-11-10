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

/*

PeerFinder
----------

    Implements the logic for announcing and discovering IP addresses for
    for connecting into the Ripple network.

Introduction
------------

Each Peer (a computer running rippled) on the Ripple network requires a certain
number of connections to other peers. These connections form an "overlay
network." When a new peer wants to join the network, they need a robust source
of network addresses (IP addresses) in order to establish outgoing connections.
Once they have joined the network, they need a method of announcing their
availaibility of accepting incoming connections.

The Ripple network, like all peer to peer networks, defines a "directed graph"
where each node represents a computer running the rippled software, and each
vertex indicates a network connection. The direction of the connection tells
us whether it is an outbound or inbound connection (from the perspective of
a particular node).

Fact #1:
    The total inbound and outbound connections of any overlay must be equal.

This follows that for each node that has an established outbound connection,
there must exist another node that has received the corresponding inbound
connection.

When a new peer joins the network it may or may not wish to receive inbound
connections. Some peers are unable to accept incoming connections for various.
For security reasons they may be behind a firewall that blocks accept requests.
The administers may decide they don't want the connection traffic. Or they
may wish to connect only to specific peers. Or they may simply be misconfigured.

If a peer decides that it wishes to receive incoming connections, it needs
a method to announce its IP address and port number, the features that it
offers (for example, that it also services client requests), and the number
of available connection slots. This is to handle the case where the peer
reaches its desired number of peer connections, but may still want to inform
the network that it will service clients. It may also be desired to indicate
the number of free client slots.

Once a peer is connected to the network we need a way both to inform our
neighbors of our status with respect to accepting connections, and also to
learn about new fresh addresses to connect to. For this we will define the
mtENDPOINTS message.

"Bootstrap Strategy"
--------------------

Nouns:

    bootstrap_ip
        numeric IPAddress
    
    bootstrap_domain
        domain name / port combinations, resolution only

    bootstrap_url
        URL leading to a text file, with a series of entries.

    ripple.txt
        Separately parsed entity outside of PeerFinder that can provide
        bootstrap_ip, bootstrap_domain, and bootstrap_url items.

The process of obtaining the initial peer connections for accessing the Ripple
peer to peer network, when there are no current connections, is called
"bootstrapping." The algorithm is as follows:

1. If (    unusedLiveEndpoints.count() > 0
        OR activeConnectionAttempts.count() > 0)
        Try addresses from unusedLiveEndpoints
        return;
2. If (    domainNames.count() > 0 AND (
               unusedBootstrapIPs.count() == 0
            OR activeNameResolutions.count() > 0) )
        ForOneOrMore (DomainName that hasn't been resolved recently)
            Contact DomainName and add entries to the unusedBootstrapIPs
        return;
3. If (unusedBootstrapIPs.count() > 0)
        Try addresses from unusedBootstrapIPs
        return;
4. Try entries from [ips]
5. Try entries from [ips_urls]
6. Increment generation number and go to 1

    - Keep a map of all current outgoing connection attempts


"Connection Strategy"
---------------------

This is the overall strategy a peer uses to maintain its position in the Ripple
network graph

We define these values:

    peerCount (calculated)
        The number of currently connected and established peers

    outCount (calculated)
        The number of peers in PeerCount that are outbound connections.

    MinOutCount (hard-coded constant)
        The minimum number of OutCount we want. This also puts a floor
        on PeerCount. This protects against sybil attacks and makes
        sure that ledgers can get retrieved reliably.
        10 is the proposed value.

    MaxPeerCount (a constant set in the rippled.cfg)
        The maximum number of peer connections, inbound or outbound,
        that a peer wishes to maintain. Setting MaxPeerCount equal to
        or below MinOutCount would disallow incoming connections.

    OutPercent (a baked-in program constant for now)
        The peer's target value for OutCount. When the value of OutCount
        is below this number, the peer will employ the Outgoing Strategy
        to raise its value of OutCount. This value is initially a constant
        in the program, defined by the developers. However, it
        may be changed through the consensus process.
        15% is a proposed value.

However, lets consider the case where OutDesired is exactly equal to MaxPeerCount / 2.
In this case, a stable state will be reached when every peer is full, and
has exactly the same number of inbound and outbound connections. The problem
here is that there are now no available incoming connection slots. No new
peers can enter the network.

Lets consider the case where OutDesired is exactly equal to (MaxPeerCount / 2) - 1.
The stable state for this network (assuming all peers can accept incoming) will
leave us with network degree equal to MaxPeerCount - 2, with all peers having two
available incoming connection slots. The global number of incoming connection slots
will be equal to twice the number of nodes on the network. While this might seem to
be a desirable outcome, note that the connectedness (degree of the overlay) plays
a large part in determining the levels of traffic and ability to receive validations
from desired nodes. Having every node with available incoming connections also
means that entries in pong caches will continually fall out with new values and
information will become less useful.

For this reason, we advise that the value of OutDesired be fractional. Upon startup,
a node will use its node ID (its 160 bit unique ID) to decide whether to round the
value of OutDesired up or down. Using this method, we can precisely control the
global number of available incoming connection slots.

"Outgoing Strategy"
-------------------

This is the method a peer uses to establish outgoing connections into the
Ripple network.

A peer whose PeerCount is zero will use these steps:
    1. Attempt addresses from a local database of addresses
    2. Attempt addresses from a set of "well known" domains in rippled.cfg


This is the method used by a peer that is already connected to the Ripple network,
to adjust the number of outgoing connections it is maintaining.


"Incoming Strategy"
------------------------------

This is the method used by a peer to announce its ability and desire to receive
incoming connections both for the purpose of obtaining additional peer connections
and also for receiving requests from clients.



Terms

Overlay Network
    http://en.wikipedia.org/wiki/Overlay_network

Directed Graph
    http://en.wikipedia.org/wiki/Directed_graph

References:

Gnutella 0.6 Protocol
    2.2.2   Ping (0x00)
    2.2.3   Pong (0x01)
    2.2.4   Use of Ping and Pong messages
    2.2.4.1   A simple pong caching scheme
    2.2.4.2   Other pong caching schemes
    http://rfc-gnutella.sourceforge.net/src/rfc-0_6-draft.html

Revised Gnutella Ping Pong Scheme
    By Christopher Rohrs and Vincent Falco
    http://rfc-gnutella.sourceforge.net/src/pong-caching.html
*/

namespace ripple {
namespace PeerFinder {

class ManagerImp
    : public Manager
    , public Thread
    , public SiteFiles::Listener
    , public DeadlineTimer::Listener
    , public LeakChecked <ManagerImp>
{
public:
    ServiceQueue m_queue;
    SiteFiles::Manager& m_siteFiles;
    Journal m_journal;
    StoreSqdb m_store;
    SerializedContext m_context;
    CheckerAdapter m_checker;
    LogicType <SimpleMonotonicClock> m_logic;
    DeadlineTimer m_connectTimer;
    DeadlineTimer m_messageTimer;
    DeadlineTimer m_cacheTimer;

    //--------------------------------------------------------------------------

    ManagerImp (
        Stoppable& stoppable,
        SiteFiles::Manager& siteFiles,
        Callback& callback,
        Journal journal)
        : Manager (stoppable)
        , Thread ("PeerFinder")
        , m_siteFiles (siteFiles)
        , m_journal (journal)
        , m_store (journal)
        , m_checker (m_context, m_queue)
        , m_logic (callback, m_store, m_checker, journal)
        , m_connectTimer (this)
        , m_messageTimer (this)
        , m_cacheTimer (this)
    {
    }

    ~ManagerImp ()
    {
        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // PeerFinder
    //
    //--------------------------------------------------------------------------

    void setConfig (Config const& config)
    {
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::setConfig, &m_logic,
                    config)));
    }

    void addFixedPeers (
        std::vector <std::string> const& strings)
    {
#if 1
        m_logic.addFixedPeers (strings);
#else
        m_queue.dispatch (m_context.wrap (
            bind (&Logic::addFixedPeers, &m_logic,
                std::vector <std::string> (strings))));
#endif
    }

    void addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (
                    &Logic::addStaticSource, &m_logic,
                        SourceStrings::New (name, strings))));
#endif
    }

    void addFallbackURL (std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    void onPeerConnectAttemptBegins (IPAddress const& address)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerConnectAttemptBegins, &m_logic,
                      address)));
#endif
    }

    void onPeerConnectAttemptCompletes (IPAddress const& address, bool success)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerConnectAttemptCompletes, &m_logic,
                      address, success)));
#endif
    }

    void onPeerConnected (const IPAddress &address, bool incoming)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerConnected, &m_logic,
                      address, incoming)));
#endif
    }

    void onPeerHandshake (PeerID const& id,
        IPAddress const& address, bool incoming)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerHandshake, &m_logic,
                    id, address, incoming)));
#endif
    }

    void onPeerDisconnected (const PeerID& id)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerDisconnected, &m_logic,
                    id)));
#endif
    }

    void onPeerLegacyEndpoint (IPAddress const& ep)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerLegacyEndpoint, &m_logic,
                    ep)));
#endif
    }

    void onPeerEndpoints (PeerID const& id,
        std::vector <Endpoint> const& endpoints)
    {
#if RIPPLE_USE_PEERFINDER
        m_queue.dispatch (
            beast::bind (&Logic::onPeerEndpoints, &m_logic,
                id, endpoints));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // SiteFiles
    //
    //--------------------------------------------------------------------------

    void parseBootstrapIPs (std::string const& name, SiteFiles::Section const& section)
    {
        std::size_t n (0);
        for (SiteFiles::Section::DataType::const_iterator iter (
            section.data().begin()); iter != section.data().end(); ++iter)
        {
            std::string const& s (*iter);
            IPAddress addr (IPAddress::from_string (s));
            if (addr.empty ())
                addr = IPAddress::from_string_altform(s);
            if (! addr.empty())
            {
                // add IPAddress to bootstrap cache
                ++n;
            }
        }

        m_journal.info <<
            "Added " << n <<
            " bootstrap IPs from " << name;
    }

    void parseFixedIPs (SiteFiles::Section const& section)
    {
        for (SiteFiles::Section::DataType::const_iterator iter (
            section.data().begin()); iter != section.data().end(); ++iter)
        {
            std::string const& s (*iter);
            IPAddress addr (IPAddress::from_string (s));
            if (addr.empty ())
                addr = IPAddress::from_string_altform(s);
            if (! addr.empty())
            {
                // add IPAddress to fixed peers
            }
        }
    }

    void onSiteFileFetch (
        std::string const& name, SiteFiles::SiteFile const& siteFile)
    {
        parseBootstrapIPs (name, siteFile["ips"]);

        //if (name == "local")
        //  parseFixedIPs (name, siteFile["ips_fixed"]);
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void onStart ()
    {
        startThread();
    }

    void onStop ()
    {
        m_journal.debug << "Stopping";
        m_checker.cancel ();
        m_logic.stop ();
        m_connectTimer.cancel();
        m_messageTimer.cancel();
        m_cacheTimer.cancel();
        m_queue.dispatch (
            m_context.wrap (
                bind (&Thread::signalThreadShouldExit, this)));
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (PropertyStream::Map& map)
    {
        SerializedContext::Scope scope (m_context);

        m_logic.onWrite (map);
    }

    //--------------------------------------------------------------------------

    void onDeadlineTimer (DeadlineTimer& timer)
    {
        if (timer == m_connectTimer)
        {
            m_queue.dispatch (
                m_context.wrap (
                    bind (&Logic::makeOutgoingConnections, &m_logic)));

            m_connectTimer.setExpiration (secondsPerConnect);
        }
        else if (timer == m_messageTimer)
        {
            m_queue.dispatch (
                m_context.wrap (
                    bind (&Logic::sendEndpoints, &m_logic)));

            m_messageTimer.setExpiration (secondsPerMessage);
        }
        else if (timer == m_cacheTimer)
        {
            m_queue.dispatch (
                m_context.wrap (
                    bind (&Logic::sweepCache, &m_logic)));

            m_cacheTimer.setExpiration (cacheSecondsToLive);
        }
    }

    void init ()
    {
        m_journal.debug << "Initializing";

        File const file (File::getSpecialLocation (
            File::userDocumentsDirectory).getChildFile ("PeerFinder.sqlite"));

        Error error (m_store.open (file));

        if (error)
        {
            m_journal.fatal <<
                "Failed to open '" << file.getFullPathName() << "'";
        }

        if (! error)
        {
            m_logic.load ();
        }

        m_connectTimer.setExpiration (secondsPerConnect);
        m_messageTimer.setExpiration (secondsPerMessage);
        m_cacheTimer.setExpiration (cacheSecondsToLive);
    
        m_queue.post (
            m_context.wrap (
                bind (&Logic::makeOutgoingConnections, &m_logic)));
    }

    void run ()
    {
        m_journal.debug << "Started";

        init ();

        m_siteFiles.addListener (*this);

        while (! this->threadShouldExit())
        {
            m_queue.run_one();
        }

        m_siteFiles.removeListener (*this);

        stopped();
    }
};

//------------------------------------------------------------------------------

Manager::Manager (Stoppable& parent)
    : Stoppable ("PeerFinder", parent)
    , PropertyStream::Source ("peerfinder")
{
}

Manager* Manager::New (
    Stoppable& parent,
    SiteFiles::Manager& siteFiles,
    Callback& callback,
    Journal journal)
{
    return new ManagerImp (parent, siteFiles, callback, journal);
}

}
}
