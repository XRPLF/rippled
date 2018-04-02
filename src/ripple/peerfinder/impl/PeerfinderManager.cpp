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

#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/impl/Checker.h>
#include <ripple/peerfinder/impl/Logic.h>
#include <ripple/peerfinder/impl/SourceStrings.h>
#include <ripple/peerfinder/impl/StoreSqdb.h>
#include <ripple/core/SociDB.h>
#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <memory>
#include <thread>

namespace ripple {
namespace PeerFinder {

class ManagerImp
    : public Manager
{
public:
    boost::asio::io_service &io_service_;
    boost::optional <boost::asio::io_service::work> work_;
    clock_type& m_clock;
    beast::Journal m_journal;
    StoreSqdb m_store;
    Checker<boost::asio::ip::tcp> checker_;
    Logic <decltype(checker_)> m_logic;
    SociConfig m_sociConfig;

    //--------------------------------------------------------------------------

    ManagerImp (
        Stoppable& stoppable,
        boost::asio::io_service& io_service,
        clock_type& clock,
        beast::Journal journal,
        BasicConfig const& config)
        : Manager (stoppable)
        , io_service_(io_service)
        , work_(boost::in_place(std::ref(io_service_)))
        , m_clock (clock)
        , m_journal (journal)
        , m_store (journal)
        , checker_ (io_service_)
        , m_logic (clock, m_store, checker_, journal)
        , m_sociConfig (config, "peerfinder")
    {
    }

    ~ManagerImp()
    {
        close();
    }

    void
    close()
    {
        if (work_)
        {
            work_ = boost::none;
            checker_.stop();
            m_logic.stop();
        }
    }

    //--------------------------------------------------------------------------
    //
    // PeerFinder
    //
    //--------------------------------------------------------------------------

    void setConfig (Config const& config) override
    {
        m_logic.config (config);
    }

    Config
    config() override
    {
        return m_logic.config();
    }

    void addFixedPeer (std::string const& name,
        std::vector <beast::IP::Endpoint> const& addresses) override
    {
        m_logic.addFixedPeer (name, addresses);
    }

    void
    addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings) override
    {
        m_logic.addStaticSource (SourceStrings::New (name, strings));
    }

    void addFallbackURL (std::string const& name,
        std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    Slot::ptr
    new_inbound_slot (
        beast::IP::Endpoint const& local_endpoint,
            beast::IP::Endpoint const& remote_endpoint) override
    {
        return m_logic.new_inbound_slot (local_endpoint, remote_endpoint);
    }

    Slot::ptr
    new_outbound_slot (beast::IP::Endpoint const& remote_endpoint) override
    {
        return m_logic.new_outbound_slot (remote_endpoint);
    }

    void
    on_endpoints (Slot::ptr const& slot,
        Endpoints const& endpoints)  override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_endpoints (impl, endpoints);
    }

    void
    on_closed (Slot::ptr const& slot)  override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_closed (impl);
    }

    void
    on_failure (Slot::ptr const& slot)  override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        m_logic.on_failure (impl);
    }

    void
    onRedirects (boost::asio::ip::tcp::endpoint const& remote_address,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) override
    {
        m_logic.onRedirects(eps.begin(), eps.end(), remote_address);
    }

    //--------------------------------------------------------------------------

    bool
    onConnected (Slot::ptr const& slot,
        beast::IP::Endpoint const& local_endpoint) override
    {
        SlotImp::ptr impl (std::dynamic_pointer_cast <SlotImp> (slot));
        return m_logic.onConnected (impl, local_endpoint);
    }

    Result
    activate (Slot::ptr const& slot,
        PublicKey const& key, bool cluster) override
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
    once_per_second() override
    {
        m_logic.once_per_second();
    }

    std::vector<std::pair<Slot::ptr, std::vector<Endpoint>>>
    buildEndpointsForPeers() override
    {
        return m_logic.buildEndpointsForPeers();
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void
    onPrepare () override
    {
        m_store.open (m_sociConfig);
        m_logic.load ();
    }

    void
    onStart() override
    {
    }

    void onStop () override
    {
        close();
        stopped();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map) override
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

std::unique_ptr<Manager>
make_Manager (Stoppable& parent, boost::asio::io_service& io_service,
        clock_type& clock, beast::Journal journal, BasicConfig const& config)
{
    return std::make_unique<ManagerImp> (
        parent, io_service, clock, journal, config);
}

}
}
