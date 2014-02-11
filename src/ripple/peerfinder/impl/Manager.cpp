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
    clock_type& m_clock;
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
        clock_type& clock,
        Journal journal)
        : Manager (stoppable)
        , Thread ("PeerFinder")
        , m_siteFiles (siteFiles)
        , m_clock (clock)
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

    void addFixedPeer (std::string const& name,
        std::vector <IPAddress> const& addresses)
    {
        m_queue.dispatch (
            m_context.wrap (
                boost::bind (&Logic::addFixedPeer, &m_logic,
                    name, addresses)));
    }

    void addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings)
    {
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::addStaticSource, &m_logic,
                    SourceStrings::New (name, strings))));
    }

    void addFallbackURL (std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    Slot::ptr new_inbound_slot (
        IP::Endpoint const& local_endpoint,
            IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_inbound_slot (local_endpoint, remote_endpoint);
    }

    Slot::ptr new_outbound_slot (IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_outbound_slot (remote_endpoint);
    }

    void on_connected (Slot::ptr const& slot,
        IP::Endpoint const& local_endpoint)
    {
        m_logic.on_connected (slot, local_endpoint);
    }

    void on_handshake (Slot::ptr const& slot,
        RipplePublicKey const& key, bool cluster)
    {
        m_logic.on_handshake (slot, key, cluster);
    }

    void on_closed (Slot::ptr const& slot)
    {
        m_logic.on_closed (slot);
    }

    void on_endpoints (Slot::ptr const& slot,
        Endpoints const& endpoints)
    {
        m_logic.on_endpoints (slot, endpoints);
    }

    void on_legacy_endpoints (IPAddresses const& addresses)
    {
        m_logic.on_legacy_endpoints (addresses);
    }
    
    //--------------------------------------------------------------------------
    void onPeerAddressChanged (
        IPAddress const& currentAddress, IPAddress const& newAddress)
    {
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::onPeerAddressChanged, &m_logic,
                    currentAddress, newAddress)));
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
            if (is_unspecified (addr))
                addr = IPAddress::from_string_altform(s);
            if (! is_unspecified (addr))
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
            if (is_unspecified (addr))
                addr = IPAddress::from_string_altform(s);
            if (! is_unspecified (addr))
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

            m_connectTimer.setExpiration (Tuning::secondsPerConnect);
        }
        else if (timer == m_messageTimer)
        {
            m_queue.dispatch (
                m_context.wrap (
                    bind (&Logic::sendEndpoints, &m_logic)));

            m_messageTimer.setExpiration (Tuning::secondsPerMessage);
        }
        else if (timer == m_cacheTimer)
        {
            m_queue.dispatch (
                m_context.wrap (
                    bind (&Logic::sweepCache, &m_logic)));

            m_cacheTimer.setExpiration (Tuning::liveCacheSecondsToLive);
        }

        // VFALCO NOTE Bit of a hack here...
        m_queue.dispatch (
            m_context.wrap (
                bind (&Logic::periodicActivity, &m_logic)));
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

        m_connectTimer.setExpiration (Tuning::secondsPerConnect);
        m_messageTimer.setExpiration (Tuning::secondsPerMessage);
        m_cacheTimer.setExpiration (Tuning::liveCacheSecondsToLive);

        m_queue.post (m_context.wrap (
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
    clock_type& clock,
    Journal journal)
{
    return new ManagerImp (parent, siteFiles, callback, clock, journal);
}

}
}
