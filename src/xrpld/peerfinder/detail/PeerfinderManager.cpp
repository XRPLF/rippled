#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Checker.h>
#include <xrpld/peerfinder/detail/InMemoryStore.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/SourceStrings.h>
#include <xrpld/peerfinder/detail/StoreSqdb.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include "xrpl/basics/BasicConfig.h"
#include "xrpl/beast/insight/Collector.h"
#include "xrpl/beast/insight/Gauge.h"
#include "xrpl/beast/insight/Hook.h"
#include "xrpl/beast/net/IPEndpoint.h"
#include "xrpl/beast/utility/Journal.h"
#include "xrpl/beast/utility/PropertyStream.h"
#include "xrpl/protocol/PublicKey.h"
#include "xrpld/peerfinder/Slot.h"
#include <boost/asio/ip/tcp.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>


namespace xrpl::PeerFinder {

class ManagerImp : public Manager
{
public:
    boost::asio::io_context& io_context;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    clock_type& m_clock;
    beast::Journal m_journal;
    std::unique_ptr<Store> m_store;
    Checker<boost::asio::ip::tcp> checker;
    Logic<decltype(checker)> m_logic;
    BasicConfig const& m_config;

    //--------------------------------------------------------------------------

    ManagerImp(
        boost::asio::io_context& ioContext,
        clock_type& clock,
        beast::Journal journal,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector,
        bool useSqliteStore)
        : 
         io_context(ioContext)
        , work(std::in_place, boost::asio::make_work_guard(io_context))
        , m_clock(clock)
        , m_journal(journal)
        , m_store([&]() -> std::unique_ptr<Store> {
            if (useSqliteStore)
                return std::make_unique<StoreSqdb>(journal);

            return std::make_unique<InMemoryStore>();
        }())
        , checker(io_context)
        , m_logic(clock, *m_store, checker, journal)
        , m_config(config)
        , m_stats_(std::bind(&ManagerImp::collectMetrics, this), collector)
    {
    }

    ~ManagerImp() override
    {
        stop();
    }

    void
    stop() override
    {
        if (work)
        {
            work.reset();
            checker.stop();
            m_logic.stop();
        }
    }

    //--------------------------------------------------------------------------
    //
    // PeerFinder
    //
    //--------------------------------------------------------------------------

    void
    setConfig(Config const& config) override
    {
        m_logic.config(config);
    }

    Config
    config() override
    {
        return m_logic.config();
    }

    void
    addFixedPeer(std::string const& name, std::vector<beast::IP::Endpoint> const& addresses)
        override
    {
        m_logic.addFixedPeer(name, addresses);
    }

    void
    addFallbackStrings(std::string const& name, std::vector<std::string> const& strings) override
    {
        m_logic.addStaticSource(SourceStrings::New(name, strings));
    }

    void
    addFallbackURL(std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    std::pair<std::shared_ptr<Slot>, Result>
    newInboundSlot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint) override
    {
        return m_logic.newInboundSlot(localEndpoint, remoteEndpoint);
    }

    std::pair<std::shared_ptr<Slot>, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint) override
    {
        return m_logic.newOutboundSlot(remoteEndpoint);
    }

    void
    onEndpoints(std::shared_ptr<Slot> const& slot, Endpoints const& endpoints) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.onEndpoints(impl, endpoints);
    }

    void
    onClosed(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.onClosed(impl);
    }

    void
    onFailure(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.onFailure(impl);
    }

    void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remoteAddress,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) override
    {
        m_logic.onRedirects(eps.begin(), eps.end(), remoteAddress);
    }

    //--------------------------------------------------------------------------

    bool
    onConnected(std::shared_ptr<Slot> const& slot, beast::IP::Endpoint const& localEndpoint)
        override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.onConnected(impl, localEndpoint);
    }

    Result
    activate(std::shared_ptr<Slot> const& slot, PublicKey const& key, bool reserved) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.activate(impl, key, reserved);
    }

    std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.redirect(impl);
    }

    std::vector<beast::IP::Endpoint>
    autoconnect() override
    {
        return m_logic.autoconnect();
    }

    void
    oncePerSecond() override
    {
        m_logic.oncePerSecond();
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() override
    {
        return m_logic.buildEndpointsForPeers();
    }

    void
    start() override
    {
        if (auto* sqliteStore = dynamic_cast<StoreSqdb*>(m_store.get()))
            sqliteStore->open(m_config);

        m_logic.load();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void
    onWrite(beast::PropertyStream::Map& map) override
    {
        m_logic.onWrite(map);
    }

private:
    struct Stats
    {
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->makeHook(handler))
            , activeInboundPeers(collector->makeGauge("Peer_Finder", "Active_Inbound_Peers"))
            , activeOutboundPeers(collector->makeGauge("Peer_Finder", "Active_Outbound_Peers"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge activeInboundPeers;
        beast::insight::Gauge activeOutboundPeers;
    };

    std::mutex m_statsMutex_;
    Stats m_stats_;

    void
    collectMetrics()
    {
        std::scoped_lock const lock(m_statsMutex_);
        m_stats_.activeInboundPeers = m_logic.counts_.inboundActive();
        m_stats_.activeOutboundPeers = m_logic.counts_.outActive();
    }
};

//------------------------------------------------------------------------------

Manager::Manager() noexcept : beast::PropertyStream::Source("peerfinder")
{
}

std::unique_ptr<Manager>
make_Manager(
    boost::asio::io_context& ioContext,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector,
    bool useSqliteStore)
{
    return std::make_unique<ManagerImp>(
        ioContext, clock, journal, config, collector, useSqliteStore);
}

} // namespace xrpl::PeerFinder

