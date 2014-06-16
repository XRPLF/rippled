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

#if 0

namespace ripple {
namespace PeerFinder {
namespace Sim {

class Link;
class Message;
class Network;
class Node;

// Maybe this should be std::set
typedef std::list <Link> Links;

//------------------------------------------------------------------------------

class Network
{
public:
    typedef std::list <Node> Peers;

    typedef ripple::unordered_map <
        IP::Endpoint, boost::reference_wrapper <Node>> Table;

    explicit Network (Params const& params,
        Journal journal = Journal());

    ~Network ();

    Params const& params() const { return m_params; }
    void prepare ();
    Journal journal () const;
    int next_node_id ();
    clock_type::time_point now ();
    Peers& nodes();
    Peers const& nodes() const;
    Node* find (IP::Endpoint const& address);
    void step ();

    template <typename Function>
    void post (Function f)
        { m_queue.post (f); }

private:
    Params m_params;
    Journal m_journal;
    int m_next_node_id;
    manual_clock <std::chrono::seconds> m_clock;
    Peers m_nodes;
    Table m_table;
    FunctionQueue m_queue;
};

//------------------------------------------------------------------------------

class Node;

// Represents a link between two peers.
// The link holds the messages the local node will receive.
//
class Link
{
public:
    typedef std::vector <Message> Messages;

    Link (
        Node& local_node,
        SlotImp::ptr const& slot,
        IP::Endpoint const& local_endpoint,
        Node& remote_node,
        IP::Endpoint const& remote_endpoint,
        bool inbound)
        : m_local_node (&local_node)
        , m_slot (slot)
        , m_local_endpoint (local_endpoint)
        , m_remote_node (&remote_node)
        , m_remote_endpoint (remote_endpoint)
        , m_inbound (inbound)
        , m_closed (false)
    {
    }

    // Indicates that the remote closed their end
    bool closed () const   { return m_closed; }

    bool inbound ()  const { return m_inbound; }
    bool outbound () const { return ! m_inbound; }

    IP::Endpoint const& remote_endpoint() const { return m_remote_endpoint; }
    IP::Endpoint const& local_endpoint()  const { return m_local_endpoint; }

    SlotImp::ptr const& slot   () const { return m_slot; }
    Node&       remote_node ()       { return *m_remote_node; }
    Node const& remote_node () const { return *m_remote_node; }
    Node&       local_node  ()       { return *m_local_node; }
    Node const& local_node  () const { return *m_local_node; }
        
    void post (Message const& m)
    {
        m_pending.push_back (m);
    }

    bool pending () const
    {
        return m_pending.size() > 0;
    }

    void close ()
    {
        m_closed = true;
    }

    void pre_step ()
    {
        std::swap (m_current, m_pending);
    }

    void step ();

private:
    Node* m_local_node;
    SlotImp::ptr m_slot;
    IP::Endpoint m_local_endpoint;
    Node* m_remote_node;
    IP::Endpoint m_remote_endpoint;
    bool m_inbound;
    bool m_closed;
    Messages m_current;
    Messages m_pending;
};

//--------------------------------------------------------------------------

class Node
    : public Callback
    , public Store
    , public Checker
{
private:
    typedef std::vector <SavedBootstrapAddress> SavedBootstrapAddresses;

public:
    struct Config
    {
        Config ()
            : canAccept (true)
        {
        }
        
        bool canAccept;
        IP::Endpoint listening_endpoint;
        IP::Endpoint well_known_endpoint;
        PeerFinder::Config config;
    };

    Links m_links;
    std::vector <Livecache::Histogram> m_livecache_history;

    Node (
        Network& network,
        Config const& config,
        clock_type& clock,
        Journal journal)
        : m_network (network)
        , m_id (network.next_node_id())
        , m_config (config)
        , m_node_id (RipplePublicKey::createFromInteger (m_id))
        , m_sink (prefix(), journal.sink())
        , m_journal (Journal (m_sink, journal.severity()), Reporting::node)
        , m_next_port (m_config.listening_endpoint.port() + 1)
        , m_logic (boost::in_place (
            std::ref (clock), std::ref (*this), std::ref (*this), std::ref (*this), m_journal))
        , m_when_expire (m_network.now() + std::chrono::seconds (1))
    {
        logic().setConfig (m_config.config);
        logic().load ();
    }

    ~Node ()
    {
        // Have to destroy the logic early because it calls back into us
        m_logic = boost::none;
    }

    void dump (Journal::ScopedStream& ss) const
    {
        ss << listening_endpoint();
        logic().dump (ss);
    }

    Links& links()
    {
        return m_links;
    }

    Links const& links() const
    {
        return m_links;
    }

    int id () const
    {
        return m_id;
    }

    RipplePublicKey const& node_id () const
    {
        return m_node_id;
    }

    Logic& logic ()
    {
        return m_logic.get();
    }

    Logic const& logic () const
    {
        return m_logic.get();
    }

    IP::Endpoint const& listening_endpoint () const
    {
        return m_config.listening_endpoint;
    }

    bool canAccept () const
    {
        return m_config.canAccept;
    }

    void receive (Link const& c, Message const& m)
    {
        logic().on_endpoints (c.slot (), m.payload());
    }

    void pre_step ()
    {
        for (Links::iterator iter (links().begin());
            iter != links().end();)
        {
            Links::iterator cur (iter++);
            cur->pre_step ();
        }
    }

    void step ()
    {
        for (Links::iterator iter (links().begin());
            iter != links().end();)
        {
            Links::iterator cur (iter++);
            //Link& link (*cur);
            cur->step ();
#if 0
            if (iter->closed ())
            {
                // Post notification?
                iter->local_node().logic().on_closed (
                    iter->remote_endpoint());
                iter = links().erase (iter);
            }
            else
#endif
        }

        logic().makeOutgoingConnections ();
        logic().sendEndpoints ();

        if (m_network.now() >= m_when_expire)
        {
            logic().expire();
            m_when_expire = m_network.now() + std::chrono::seconds (1);
        }

        m_livecache_history.emplace_back (
            logic().state().livecache.histogram());

        logic().periodicActivity();
    }

    //----------------------------------------------------------------------
    //
    // Callback
    //
    //----------------------------------------------------------------------

    void sendEndpoints (IP::Endpoint const& remote_endpoint,
        Endpoints const& endpoints)
    {
        m_network.post (std::bind (&Node::doSendEndpoints, this,
            remote_endpoint, endpoints));
    }

    void connectPeers (IPAddresses const& addresses)
    {
        m_network.post (std::bind (&Node::doConnectPeers, this,
            addresses));
    }

    void disconnectPeer (IP::Endpoint const& remote_endpoint, bool graceful)
    {
        m_network.post (std::bind (&Node::doDisconnectPeer, this,
            remote_endpoint, graceful));
    }

    void activatePeer (IP::Endpoint const& remote_endpoint)
    {
        /* no underlying peer to activate */
    }

    void doSendEndpoints (IP::Endpoint const& remote_endpoint,
        Endpoints const& endpoints)
    {
        Links::iterator const iter1 (std::find_if (
            links().begin (), links().end(),
                is_remote_endpoint (remote_endpoint)));
        if (iter1 != links().end())
        {
            // Drop the message if they closed their end
            if (iter1->closed ())
                return;
            Node& remote_node (iter1->remote_node());
            // Find their link to us
            Links::iterator const iter2 (std::find_if (
                remote_node.links().begin(), remote_node.links().end(),
                    is_remote_endpoint (iter1->local_endpoint ())));
            consistency_check (iter2 != remote_node.links().end());

            //
            // VFALCO NOTE This looks wrong! Shouldn't it call receive()
            //             on the link and not the Peer?
            //
            Message const m (endpoints);
            iter2->local_node().receive (*iter2, m);
            //iter2->post (m);
        }
    }

    void doCheckAccept (Node& remote_node, IP::Endpoint const& remote_endpoint)
    {
        // Find our link to the remote node
        Links::iterator iter (std::find_if (m_links.begin (),
            m_links.end(), is_remote_endpoint (remote_endpoint)));
        // See if the logic closed the connection
        if (iter == m_links.end())
            return;
        // Post notifications
        m_network.post (std::bind (&Logic::on_handshake,
            &remote_node.logic(), iter->local_endpoint(), node_id(), false));
        m_network.post (std::bind (&Logic::on_handshake,
            &logic(), remote_endpoint, remote_node.node_id(), false));
    }

    void doConnectPeers (IPAddresses const& addresses)
    {
        for (IPAddresses::const_iterator iter (addresses.begin());
            iter != addresses.end(); ++iter)
        {
            IP::Endpoint const& remote_endpoint (*iter);
            Node* const remote_node (m_network.find (remote_endpoint));
            // Acquire slot
            Slot::ptr const local_slot (
                m_logic->new_outbound_slot (remote_endpoint));
            if (! local_slot)
                continue;
            // See if the address is connectible
            if (remote_node == nullptr || ! remote_node->canAccept())
            {
                // Firewalled or no one listening
                // Post notification
                m_network.post (std::bind (&Logic::on_closed,
                    &logic(), local_slot));
                continue;
            }
            IP::Endpoint const local_endpoint (
                listening_endpoint().at_port (m_next_port++));
            // Acquire slot
            Slot::ptr const remote_slot (
                remote_node->logic().new_inbound_slot (
                    remote_endpoint, local_endpoint));
            if (! remote_slot)
                continue;
            // Connection established, create links
            m_links.emplace_back (*this, local_slot, local_endpoint,
                *remote_node, remote_endpoint, false);
            remote_node->m_links.emplace_back (*remote_node, remote_slot,
                remote_endpoint, *this, local_endpoint, true);
            // Post notifications
            m_network.post (std::bind (&Logic::on_connected,
                &logic(), local_endpoint, remote_endpoint));
            m_network.post (std::bind (&Node::doCheckAccept,
                remote_node, std::ref (*this), local_endpoint));
        }
    }

    void doClosed (IP::Endpoint const& remote_endpoint, bool graceful)
    {
        // Find our link to them
        Links::iterator const iter (std::find_if (
            m_links.begin(), m_links.end(),
                is_remote_endpoint (remote_endpoint)));
        // Must be connected!
        check_invariant (iter != m_links.end());
        // Must be closed!
        check_invariant (iter->closed());
        // Remove our link to them
        m_links.erase (iter);
        // Notify
        m_network.post (std::bind (&Logic::on_closed,
            &logic(), remote_endpoint));
    }

    void doDisconnectPeer (IP::Endpoint const& remote_endpoint, bool graceful)
    {
        // Find our link to them
        Links::iterator const iter1 (std::find_if (
            m_links.begin(), m_links.end(),
                is_remote_endpoint (remote_endpoint)));
        if (iter1 == m_links.end())
            return;
        Node& remote_node (iter1->remote_node());
        IP::Endpoint const local_endpoint (iter1->local_endpoint());
        // Find their link to us
        Links::iterator const iter2 (std::find_if (
            remote_node.links().begin(), remote_node.links().end(),
                is_remote_endpoint (local_endpoint)));
        if (iter2 != remote_node.links().end())
        {
            // Notify the remote that we closed
            check_invariant (! iter2->closed());
            iter2->close();
            m_network.post (std::bind (&Node::doClosed,
                &remote_node, local_endpoint, graceful));
        }
        if (! iter1->closed ())
        {
            // Remove our link to them
            m_links.erase (iter1);
            // Notify
            m_network.post (std::bind (&Logic::on_closed,
                &logic(), remote_endpoint));
        }

        /*
        if (! graceful || ! iter2->pending ())
        {
            remote_node.links().erase (iter2);
            remote_node.logic().on_closed (local_endpoint);
        }
        */
    }

    //----------------------------------------------------------------------
    //
    // Store
    //
    //----------------------------------------------------------------------

    std::vector <SavedBootstrapAddress> loadBootstrapCache ()
    {
        std::vector <SavedBootstrapAddress> result;
        SavedBootstrapAddress item;
        item.address = m_config.well_known_endpoint;
        item.cumulativeUptime = std::chrono::seconds (0);
        item.connectionValence = 0;
        result.push_back (item);
        return result;
    }

    void updateBootstrapCache (
        std::vector <SavedBootstrapAddress> const& list)
    {
        m_bootstrap_cache = list;
    }

    //
    // Checker
    //

    void cancel ()
    {
    }

    void async_test (IP::Endpoint const& address,
        asio::shared_handler <void (Result)> handler)
    {
        Node* const node (m_network.find (address));
        Checker::Result result;
        result.address = address;
        if (node != nullptr)
            result.canAccept = node->canAccept();
        else
            result.canAccept = false;
        handler (result);
    }

private:
    std::string prefix()
    {
        int const width (5);
        std::stringstream ss;
        ss << "#" << m_id << " ";
        std::string s (ss.str());
        s.insert (0, std::max (
            0, width - int(s.size())), ' ');
        return s;
    }

    Network& m_network;
    int const m_id;
    Config const m_config;
    RipplePublicKey m_node_id;
    WrappedSink m_sink;
    Journal m_journal;
    IP::Port m_next_port;
    boost::optional <Logic> m_logic;
    clock_type::time_point m_when_expire;
    SavedBootstrapAddresses m_bootstrap_cache;
};

//------------------------------------------------------------------------------

void Link::step ()
{
    for (Messages::const_iterator iter (m_current.begin());
        iter != m_current.end(); ++iter)
        m_local_node->receive (*this, *iter);
    m_current.clear();
}

//------------------------------------------------------------------------------

static IP::Endpoint next_endpoint (IP::Endpoint address)
{
    if (address.is_v4())
    {
        do
        {
            address = IP::Endpoint (IP::AddressV4 (
                address.to_v4().value + 1)).at_port (address.port());
        }
        while (! is_public (address));

        return address;
    }

    bassert (address.is_v6());
    // unimplemented
    bassertfalse;
    return IP::Endpoint();
}

Network::Network (

    Params const& params,
    Journal journal)
    : m_params (params)
    , m_journal (journal)
    , m_next_node_id (1)
{
}

void Network::prepare ()
{
    IP::Endpoint const well_known_endpoint (
        IP::Endpoint::from_string ("1.0.0.1").at_port (1));
    IP::Endpoint address (well_known_endpoint);

    for (int i = 0; i < params().nodes; ++i )
    {
        if (i == 0)
        {
            Node::Config config;
            config.canAccept = true;
            config.listening_endpoint = address;
            config.well_known_endpoint = well_known_endpoint;
            config.config.maxPeers = params().maxPeers;
            config.config.outPeers = params().outPeers;
            config.config.wantIncoming = true;
            config.config.autoConnect = true;
            config.config.listeningPort = address.port();
            m_nodes.emplace_back (
                *this,
                config,
                m_clock,
                m_journal);
            m_table.emplace (address, std::ref (m_nodes.back()));
            address = next_endpoint (address);
        }

        if (i != 0)
        {
            Node::Config config;
            config.canAccept = Random::getSystemRandom().nextInt (100) >=
                (m_params.firewalled * 100);
            config.listening_endpoint = address;
            config.well_known_endpoint = well_known_endpoint;
            config.config.maxPeers = params().maxPeers;
            config.config.outPeers = params().outPeers;
            config.config.wantIncoming = true;
            config.config.autoConnect = true;
            config.config.listeningPort = address.port();
            m_nodes.emplace_back (
                *this,
                config,
                m_clock,
                m_journal);
            m_table.emplace (address, std::ref (m_nodes.back()));
            address = next_endpoint (address);
        }
    }
}

Network::~Network ()
{
}

Journal Network::journal () const
{
    return m_journal;
}

int Network::next_node_id ()
{
    return m_next_node_id++;
}

clock_type::time_point Network::now ()
{
    return m_clock.now();
}

Network::Peers& Network::nodes()
{
    return m_nodes;
}

#if 0
Network::Peers const& Network::nodes() const
{
    return m_nodes;
}
#endif

Node* Network::find (IP::Endpoint const& address)
{
    Table::iterator iter (m_table.find (address));
    if (iter != m_table.end())
        return iter->second.get_pointer();
    return nullptr;
}

void Network::step ()
{
    for (Peers::iterator iter (m_nodes.begin());
        iter != m_nodes.end();)
        (iter++)->pre_step();

    for (Peers::iterator iter (m_nodes.begin());
        iter != m_nodes.end();)
        (iter++)->step();

    m_queue.run ();

    // Advance the manual clock so that
    // messages are broadcast at every step.
    //
    //m_clock += Tuning::secondsPerConnect;
    ++m_clock;
}

//------------------------------------------------------------------------------

template <>
struct VertexTraits <Node>
{
    typedef Links Edges;
    typedef Link  Edge;
    static Edges& edges (Node& node)
        { return node.links(); }
    static Node* vertex (Link& l)
        { return &l.remote_node(); }
};

//------------------------------------------------------------------------------

struct PeerStats
{
    PeerStats ()
        : inboundActive (0)
        , out_active (0)
        , inboundSlotsFree (0)
        , outboundSlotsFree (0)
    {
    }

    template <typename Peer>
    explicit PeerStats (Peer const& peer)
    {
        inboundActive = peer.logic().counts().inboundActive();
        out_active = peer.logic().counts().out_active();
        inboundSlotsFree = peer.logic().counts().inboundSlotsFree();
        outboundSlotsFree = peer.logic().counts().outboundSlotsFree();
    }

    PeerStats& operator+= (PeerStats const& rhs)
    {
        inboundActive += rhs.inboundActive;
        out_active += rhs.out_active;
        inboundSlotsFree += rhs.inboundSlotsFree;
        outboundSlotsFree += rhs.outboundSlotsFree;
        return *this;
    }

    int totalActive () const
        { return inboundActive + out_active; }

    int inboundActive;
    int out_active;
    int inboundSlotsFree;
    int outboundSlotsFree;
};

//------------------------------------------------------------------------------

inline PeerStats operator+ (PeerStats const& lhs, PeerStats& rhs)
{
    PeerStats result (lhs);
    result += rhs;
    return result;
}

//------------------------------------------------------------------------------

/** Aggregates statistics on the connected network. */
class CrawlState
{
public:
    explicit CrawlState (std::size_t step)
        : m_step (step)
        , m_size (0)
        , m_diameter (0)
    {
    }

    std::size_t step () const
        { return m_step; }

    std::size_t size () const
        { return m_size; }

    int diameter () const
        { return m_diameter; }

    PeerStats const& stats () const
        { return m_stats; }

    // network wide average
    double outPeers () const
    {
        if (m_size > 0)
            return double (m_stats.out_active) / m_size;
        return 0;
    }

    // Histogram, shows the number of peers that have a specific number of
    // active connections. The index into the array is the number of connections,
    // and the value is the number of peers.
    //
    std::vector <int> totalActiveHistogram;

    template <typename Peer>
    void operator() (Peer const& peer, int diameter)
    {
        ++m_size;
        PeerStats const stats (peer);
        int const bucket (stats.totalActive ());
        if (totalActiveHistogram.size() < bucket + 1)
            totalActiveHistogram.resize (bucket + 1);
        ++totalActiveHistogram [bucket];
        m_stats += stats;
        m_diameter = diameter;
    }

private:
    std::size_t m_step;
    std::size_t m_size;
    PeerStats m_stats;
    int m_diameter;
};

//------------------------------------------------------------------------------

/** Report the results of a network crawl. */
template <typename Stream, typename Crawl>
void report_crawl (Stream const& stream, Crawl const& c)
{
    if (! stream)
        return;
    stream
        << std::setw (6) << c.step()
        << std::setw (6) << c.size()
        << std::setw (6) << std::fixed << std::setprecision(2) << c.outPeers()
        << std::setw (6) << c.diameter()
        //<< to_string (c.totalActiveHistogram)
        ;
}

template <typename Stream, typename CrawlSequence>
void report_crawls (Stream const& stream, CrawlSequence const& c)
{
    if (! stream)
        return;
    stream 
        << "Crawl Report"
        << std::endl
        << std::setw (6) << "Step"
        << std::setw (6) << "Size"
        << std::setw (6) << "Out"
        << std::setw (6) << "Hops"
        //<< std::setw (6) << "Count"
        ;
    for (typename CrawlSequence::const_iterator iter (c.begin());
        iter != c.end(); ++iter)
        report_crawl (stream, *iter);
    stream << std::endl;
}

/** Report a table with aggregate information on each node. */
template <typename NodeSequence>
void report_nodes (NodeSequence const& nodes, Journal::Stream const& stream)
{
    stream <<
        divider() << std::endl <<
        "Nodes Report" << std::endl <<
        rfield ("ID") <<
        rfield ("Total") <<
        rfield ("In") <<
        rfield ("Out") <<
        rfield ("Tries") <<
        rfield ("Live") <<
        rfield ("Boot")
        ;

    for (typename NodeSequence::const_iterator iter (nodes.begin());
        iter != nodes.end(); ++iter)
    {
        typename NodeSequence::value_type const& node (*iter);
        Logic const& logic (node.logic());
        Logic::State const& state (logic.state());
        stream <<
            rfield (node.id ()) <<
            rfield (state.counts.totalActive ()) <<
            rfield (state.counts.inboundActive ()) <<
            rfield (state.counts.out_active ()) <<
            rfield (state.counts.connectCount ()) <<
            rfield (state.livecache.size ()) <<
            rfield (state.bootcache.size ())
            ;
    }
}

//------------------------------------------------------------------------------

/** Convert a sequence into a formatted delimited string.
    The range is [first, last)
*/
/** @{ */
template <typename InputIterator, class charT, class Traits>
std::basic_string <charT, Traits>
    sequence_to_string (InputIterator first, InputIterator last,
        std::basic_string <charT, Traits> const& sep = ",", int width = -1)
{
    std::basic_stringstream <charT, Traits> ss;
    while (first != last)
    {
        InputIterator const iter (first++);
        if (width > 0)
            ss << std::setw (width) << *iter;
        else
            ss << *iter;
        if (first != last)
            ss << sep;
    }
    return ss.str();
}

template <typename InputIterator>
std::string sequence_to_string (InputIterator first, InputIterator last,
    char const* sep, int width = -1)
{
    return sequence_to_string (first, last, std::string (sep), width);
}
/** @} */

/** Report the time-evolution of a specified node. */
template <typename Node, typename Stream>
void report_node_timeline (Node const& node, Stream const& stream)
{
    typename Livecache::Histogram::size_type const histw (
        3 * Livecache::Histogram::size() - 1);
    // Title
    stream <<
        divider () << std::endl <<
        "Node #" << node.id() << " History" << std::endl <<
        divider ();
    // Legend
    stream <<
        fpad (4) << fpad (2) <<
        fpad (2) << field ("Livecache entries by hops", histw) << fpad (2)
        ;
    {
        Journal::ScopedStream ss (stream);
        ss <<
            rfield ("Step",4) << fpad (2);
        ss << "[ ";
        for (typename Livecache::Histogram::size_type i (0);
            i < Livecache::Histogram::size(); ++i)
        {
            ss << rfield (i,2);
            if (i != Livecache::Histogram::size() - 1)
                ss << fpad (1);
        }
        ss << " ]";
    }

    // Entries
    typedef std::vector <Livecache::Histogram> History;
    History const& h (node.m_livecache_history);
    std::size_t step (0);
    for (typename History::const_iterator iter (h.begin());
        iter != h.end(); ++iter)
    {
        ++step;
        Livecache::Histogram const& t (*iter);
        stream <<
            rfield (step,4) << fpad (2) <<
            fpad (2) <<
                field (sequence_to_string (t.begin (), t.end(), " ", 2), histw) <<
                fpad (2)
                ;
    }        
}

//------------------------------------------------------------------------------

class PeerFinderTests : public UnitTest
{
public:
    void runTest ()
    {
        //Debug::setAlwaysCheckHeap (true);

        beginTestCase ("network");

        Params p;
        p.steps      = 200;
        p.nodes      = 1000;
        p.outPeers   = 9.5;
        p.maxPeers   = 200;
        p.firewalled = 0.80;

        Network n (p, Journal (journal(), Reporting::network));

        // Report network parameters
        if (Reporting::params)
        {
            Journal::Stream const stream (journal().info);

            if (stream)
            {
                stream
                    << "Network parameters"
                    << std::endl
                    << std::setw (6) << "Steps"
                    << std::setw (6) << "Nodes"
                    << std::setw (6) << "Out"
                    << std::setw (6) << "Max"
                    << std::setw (6) << "Fire"
                    ;

                stream
                    << std::setw (6) << p.steps
                    << std::setw (6) << p.nodes
                    << std::setw (6) << std::fixed << std::setprecision (1) << p.outPeers
                    << std::setw (6) << p.maxPeers
                    << std::setw (6) << int (p.firewalled * 100)
                    ;

                stream << std::endl;
            }
        }

        //
        // Run the simulation
        //
        n.prepare ();
        {
            // Note that this stream is only for the crawl,
            // The network has its own journal.
            Journal::Stream const stream (
                journal().info, Reporting::crawl);

            std::vector <CrawlState> crawls;
            if (Reporting::crawl)
                crawls.reserve (p.steps);

            // Iterate the network
            for (std::size_t step (0); step < p.steps; ++step)
            {
                if (Reporting::crawl)
                {
                    crawls.emplace_back (step);
                    CrawlState& c (crawls.back ());
                    breadth_first_traverse <Node, CrawlState&> (
                        n.nodes().front(), c);
                }
                n.journal().info <<
                    divider () << std::endl <<
                    "Time " << n.now ().time_since_epoch () << std::endl <<
                    divider ()
                    ;
                
                n.step();
                n.journal().info << std::endl;
            }
            n.journal().info << std::endl;

            // Report the crawls
            report_crawls (stream, crawls);
        }

        // Run detailed nodes dump report
        if (Reporting::dump_nodes)
        {
            Journal::Stream const stream (journal().info);
            for (Network::Peers::const_iterator iter (n.nodes().begin());
                iter != n.nodes().end(); ++iter)
            {
                Journal::ScopedStream ss (stream);
                Node const& node (*iter);
                ss << std::endl <<
                    "--------------" << std::endl <<
                    "#" << node.id() <<
                    " at " << node.listening_endpoint ();
                node.logic().dump (ss);
            }
        }

        // Run aggregate nodes report
        if (Reporting::nodes)
        {
            Journal::Stream const stream (journal().info);
            report_nodes (n.nodes (), stream);
            stream << std::endl;
        }

        // Run Node report
        {
            Journal::Stream const stream (journal().info);
            report_node_timeline (n.nodes().front(), stream);
            stream << std::endl;
        }

        pass();
    }

    PeerFinderTests () : UnitTest ("PeerFinder", "ripple", runManual)
    {
    }
};

static PeerFinderTests peerFinderTests;

}
}
}

#endif
