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
of network addresses (IP adresses) in order to establish outgoing connections.
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

Pong
----

Once a peer is connected to the network we need a way both to inform our
neighbors of our status with respect to accepting connections, and also to
learn about new fresh addresses to connect to. For this we will define the "Pong"
message.

"Connection Strategy"
---------------------

This is the overall strategy a peer uses to maintain its position in the Ripple
network graph

We define these values:

    PeerCount (calculated)
        The number of currently connected and established peers

    OutCount (calculated)
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

    OutDesiredPercent (a baked-in program constant for now)
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
//------------------------------------------------------------------------------

class PeerFinderImp
    : public PeerFinder
    , private ThreadWithCallQueue::EntryPoints
    , private DeadlineTimer::Listener
    , LeakChecked <PeerFinderImp>
{
public:
    // Tunable constants
    enum
    {
        // How often our timer goes off to consult outside sources for IPs
        secondsPerUpdate = 1 * 60 * 60,     // once per hour
        // How often we announce our IP
        secondsPerBroadcast      = 5 * 60,

        // The minimum number of peers we want
        numberOfPeersMinimum     = 4,
        numberOfPeersMaximum     = 10,

		// The minimum number of seconds a connection ought to be sustained
		// before we consider it "stable"
		secondsForStability = 60,          // one minute
    };

    //--------------------------------------------------------------------------

    /** The Logic for maintaining the list of Peer addresses.
        We keep this in a separate class so it can be instantiated
        for unit tests.
    */
    class Logic
    {
        Callback &m_callback;

    public:
        explicit Logic (Callback& callback)
            : m_callback (callback)
        {
        }

        // Called on the PeerFinder thread
        void onUpdateConnectionsStatus (
            Connections const& connections)
        {
            if (connections.numberTotal () < numberOfPeersMinimum)
            {
                // do something
            }
            else
            {
                // do something?
            }
        }

		void onPeerConnected (
			const PeerId& id)
		{

		}

		void onPeerDisconnected (
			const PeerId& id)
		{

		}

        void onAcceptTimer() 
        {
            m_callback.onAnnounceAddress ();
        }
    };

    //--------------------------------------------------------------------------

public:
    explicit PeerFinderImp (Callback& callback)
        : m_logic (callback)
        , m_thread ("PeerFinder")
        , m_acceptTimer (this)
        , m_updateTimer (this)
    {
        m_thread.start (this);
    }

    ~PeerFinderImp ()
    {
    }

    void updateConnectionsStatus (Connections& connections)
    {
        // Queue the call to the logic
        m_thread.call (&Logic::onUpdateConnectionsStatus,
            &m_logic, connections);
    }

	void onPeerConnected(const PeerId& id)
	{
		m_thread.call (&Logic::onPeerConnected,
			&m_logic, id);
	}

	void onPeerDisconnected(const PeerId& id)
	{
		m_thread.call (&Logic::onPeerDisconnected,
			&m_logic, id);
	}

    //--------------------------------------------------------------------------
    void onAcceptTimer ()
    {
#if 0
        static int x = 0;

        if(x == 0)
            Debug::breakPoint ();

        x++;
#endif
    }

    void onDeadlineTimer (DeadlineTimer& timer)
    {
        // This will make us fall into the idle proc as needed
        //
        if (timer == m_updateTimer)
            m_thread.interrupt ();
        else if (timer == m_acceptTimer)
            m_thread.call (&Logic::onAcceptTimer, &m_logic);
    }

    void threadInit ()
    {
        m_updateTimer.setRecurringExpiration (secondsPerUpdate);
        m_acceptTimer.setRecurringExpiration (secondsPerBroadcast);
    }

    void threadExit ()
    {
    }

    bool threadIdle ()
    {
        bool interrupted = false;

        // This is where you can go into a loop and do stuff
        // like process the lists, and what not. Just be
        // sure to call:
        //
        // @code
        // interrupted = interruptionPoint ();
        // @encode
        //
        // From time to time. If it returns true then you
        // need to exit this function so that Thread can
        // process its asynchronous call queue and then come
        // back into threadIdle()

        return interrupted;
    }

private:
    Logic m_logic;
    ThreadWithCallQueue m_thread;
    DeadlineTimer m_acceptTimer;
    DeadlineTimer m_updateTimer;
};

//------------------------------------------------------------------------------

PeerFinder* PeerFinder::New (PeerFinder::Callback& callback)
{
    return new PeerFinderImp (callback);
}

//------------------------------------------------------------------------------

class PeerFinderTests : public UnitTest,
                        public PeerFinder::Callback
{
public:
    void testValidityChecks ()
    {
        beginTestCase ("ip validation");

        fail ("there's no code!");
    }

    void runTest ()
    {
        PeerFinderImp::Logic logic (*this);

        beginTestCase ("logic");
        logic.onAcceptTimer ();
    }

    void onAnnounceAddress ()
    {

    }

    PeerFinderTests () : UnitTest ("PeerFinder", "ripple", runManual)
    {
    }
};

static PeerFinderTests peerFinderTests;

