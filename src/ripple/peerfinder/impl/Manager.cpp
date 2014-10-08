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

#include <ripple/peerfinder/Manager.h>
#include <ripple/peerfinder/impl/CheckerAdapter.h>
#include <ripple/peerfinder/impl/Logic.h>
#include <ripple/peerfinder/impl/SourceStrings.h>
#include <ripple/peerfinder/impl/StoreSqdb.h>

#if DOXYGEN
#include <ripple/peerfinder/README.md>
#endif

namespace ripple {
namespace PeerFinder {

class ManagerImp
    : public Manager
    , public beast::Thread
    , public beast::LeakChecked <ManagerImp>
{
public:
    beast::ServiceQueue m_queue;
    beast::File m_databaseFile;
    clock_type& m_clock;
    beast::Journal m_journal;
    StoreSqdb m_store;
    SerializedContext m_context;
    CheckerAdapter m_checker;
    Logic m_logic;
    
    //--------------------------------------------------------------------------

    ManagerImp (
        Stoppable& stoppable,
        beast::File const& pathToDbFileOrDirectory,
        clock_type& clock,
        beast::Journal journal)
        : Manager (stoppable)
        , Thread ("PeerFinder")
        , m_databaseFile (pathToDbFileOrDirectory)
        , m_clock (clock)
        , m_journal (journal)
        , m_store (journal)
        , m_checker (m_context, m_queue)
        , m_logic (clock, m_store, m_checker, journal)
    {
        if (m_databaseFile.isDirectory ())
            m_databaseFile = m_databaseFile.getChildFile("peerfinder.sqlite");
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
                std::bind (&Logic::setConfig, &m_logic,
                    config)));
    }

    void addFixedPeer (std::string const& name,
        std::vector <beast::IP::Endpoint> const& addresses)
    {
        m_queue.dispatch (
            m_context.wrap (
                std::bind (&Logic::addFixedPeer, &m_logic,
                    name, addresses)));
    }

    void addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings)
    {
        m_queue.dispatch (
            m_context.wrap (
                std::bind (&Logic::addStaticSource, &m_logic,
                    SourceStrings::New (name, strings))));
    }

    void addFallbackURL (std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    Slot::ptr new_inbound_slot (
        beast::IP::Endpoint const& local_endpoint,
            beast::IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_inbound_slot (local_endpoint, remote_endpoint);
    }

    Slot::ptr new_outbound_slot (beast::IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_outbound_slot (remote_endpoint);
    }

    void on_endpoints (Slot::ptr const& slot,
        Endpoints const& endpoints)
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_endpoints (impl, endpoints);
    }

    void on_legacy_endpoints (IPAddresses const& addresses)
    {
        m_logic.on_legacy_endpoints (addresses);
    }

    void on_closed (Slot::ptr const& slot)
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_closed (impl);
    }

    //--------------------------------------------------------------------------

    bool
    connected (Slot::ptr const& slot,
        beast::IP::Endpoint const& local_endpoint) override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        return m_logic.connected (impl, local_endpoint);
    }

    Result
    activate (Slot::ptr const& slot,
        RipplePublicKey const& key, bool cluster) override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        return m_logic.activate (impl, key, cluster);
    }

    std::vector <Endpoint>
    redirect (Slot::ptr const& slot) override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        return m_logic.redirect (impl);
    }

    std::vector <beast::IP::Endpoint>
    autoconnect() override
    {
        return m_logic.autoconnect();
    }

    void
    once_per_second()
    {
        m_logic.once_per_second();
    }

    std::vector<std::pair<Slot::ptr, std::vector<Endpoint>>>
    sendpeers() override
    {
        return m_logic.sendpeers();
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
            beast::IP::Endpoint addr (beast::IP::Endpoint::from_string (s));
            if (is_unspecified (addr))
                addr = beast::IP::Endpoint::from_string_altform(s);
            if (! is_unspecified (addr))
            {
                // add IP::Endpoint to bootstrap cache
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
            beast::IP::Endpoint addr (beast::IP::Endpoint::from_string (s));
            if (is_unspecified (addr))
                addr = beast::IP::Endpoint::from_string_altform(s);
            if (! is_unspecified (addr))
            {
                // add IP::Endpoint to fixed peers
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
        m_queue.dispatch (
            m_context.wrap (
                std::bind (&Thread::signalThreadShouldExit, this)));
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        SerializedContext::Scope scope (m_context);

        m_logic.onWrite (map);
    }

    //--------------------------------------------------------------------------

    void init ()
    {
        m_journal.debug << "Initializing";

        beast::Error error (m_store.open (m_databaseFile));

        if (error)
        {
            m_journal.fatal <<
                "Failed to open '" << m_databaseFile.getFullPathName() << "'";
        }

        if (! error)
        {
            m_logic.load ();
        }
    }

    void run ()
    {
        m_journal.debug << "Started";

        init ();

        while (! this->threadShouldExit())
        {
            m_queue.run_one();
        }

        stopped();
    }
};

//------------------------------------------------------------------------------

Manager::Manager (Stoppable& parent)
    : Stoppable ("PeerFinder", parent)
    , beast::PropertyStream::Source ("peerfinder")
{
}

Manager* Manager::New (Stoppable& parent, beast::File const& databaseFile,
    clock_type& clock, beast::Journal journal)
{
    return new ManagerImp (parent, databaseFile, clock, journal);
}

}
}
