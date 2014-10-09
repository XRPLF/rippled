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
#include <ripple/peerfinder/impl/Checker.h>
#include <ripple/peerfinder/impl/Logic.h>
#include <ripple/peerfinder/impl/SourceStrings.h>
#include <ripple/peerfinder/impl/StoreSqdb.h>
#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include <thread>

#if DOXYGEN
#include <ripple/peerfinder/README.md>
#endif

namespace ripple {
namespace PeerFinder {

class ManagerImp
    : public Manager
    , public beast::LeakChecked <ManagerImp>
{
public:
    beast::File m_databaseFile;
    clock_type& m_clock;
    beast::Journal m_journal;
    StoreSqdb m_store;
    Checker<boost::asio::ip::tcp> checker_;
    Logic <decltype(checker_)> m_logic;

    // Temporary
    std::thread thread_;
    boost::asio::io_service io_service_;
    boost::optional <boost::asio::io_service::work> work_;

    //--------------------------------------------------------------------------

    ManagerImp (
        Stoppable& stoppable,
        beast::File const& pathToDbFileOrDirectory,
        clock_type& clock,
        beast::Journal journal)
        : Manager (stoppable)
        , m_databaseFile (pathToDbFileOrDirectory)
        , m_clock (clock)
        , m_journal (journal)
        , m_store (journal)
        , checker_ (io_service_)
        , m_logic (clock, m_store, checker_, journal)
    {
        if (m_databaseFile.isDirectory ())
            m_databaseFile = m_databaseFile.getChildFile("peerfinder.sqlite");

        work_ = boost::in_place (std::ref(io_service_));
        thread_ = std::thread ([&]() { this->io_service_.run(); });
    }

    ~ManagerImp()
    {
        stop();
    }

    void
    stop()
    {
        if (thread_.joinable())
        {
            work_ = boost::none;
            thread_.join();
        }
    }

    //--------------------------------------------------------------------------
    //
    // PeerFinder
    //
    //--------------------------------------------------------------------------

    void setConfig (Config const& config)
    {
        m_logic.config (config);
    }

    void addFixedPeer (std::string const& name,
        std::vector <beast::IP::Endpoint> const& addresses)
    {
        m_logic.addFixedPeer (name, addresses);
    }

    void
    addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings)
    {
        m_logic.addStaticSource (SourceStrings::New (name, strings));
    }

    void addFallbackURL (std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    Slot::ptr
    new_inbound_slot (
        beast::IP::Endpoint const& local_endpoint,
            beast::IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_inbound_slot (local_endpoint, remote_endpoint);
    }

    Slot::ptr
    new_outbound_slot (beast::IP::Endpoint const& remote_endpoint)
    {
        return m_logic.new_outbound_slot (remote_endpoint);
    }

    void
    on_endpoints (Slot::ptr const& slot, Endpoints const& endpoints)
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_endpoints (impl, endpoints);
    }

    void
    on_legacy_endpoints (IPAddresses const& addresses)
    {
        m_logic.on_legacy_endpoints (addresses);
    }

    void
    on_closed (Slot::ptr const& slot)
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
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void
    onStart()
    {
        m_journal.debug << "Initializing";
        beast::Error error (m_store.open (m_databaseFile));
        if (error)
            m_journal.fatal <<
                "Failed to open '" << m_databaseFile.getFullPathName() << "'";
        if (! error)
            m_logic.load ();
    }

    void onStop ()
    {
        m_journal.debug << "Stopping";
        checker_.stop();
        m_logic.stop();
        /*
        signalThreadShouldExit();
        m_queue.dispatch (m_context.wrap (
            std::bind (&Thread::signalThreadShouldExit, this)));
        */
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        m_logic.onWrite (map);
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
