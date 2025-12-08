#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Checker.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/SourceStrings.h>
#include <xrpld/peerfinder/detail/StoreSqdb.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <memory>
#include <optional>

namespace ripple {
namespace PeerFinder {

class ManagerImp : public Manager
{
public:
    boost::asio::io_context& io_context_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    clock_type& m_clock;
    beast::Journal m_journal;
    StoreSqdb m_store;
    Checker<boost::asio::ip::tcp> checker_;
    Logic<decltype(checker_)> m_logic;
    BasicConfig const& m_config;

    //--------------------------------------------------------------------------

    ManagerImp(
        boost::asio::io_context& io_context,
        clock_type& clock,
        beast::Journal journal,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector)
        : Manager()
        , io_context_(io_context)
        , work_(std::in_place, boost::asio::make_work_guard(io_context_))
        , m_clock(clock)
        , m_journal(journal)
        , m_store(journal)
        , checker_(io_context_)
        , m_logic(clock, m_store, checker_, journal)
        , m_config(config)
        , m_stats(std::bind(&ManagerImp::collect_metrics, this), collector)
    {
    }

    ~ManagerImp() override
    {
        stop();
    }

    void
    stop() override
    {
        if (work_)
        {
            work_.reset();
            checker_.stop();
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
    addFixedPeer(
        std::string const& name,
        std::vector<beast::IP::Endpoint> const& addresses) override
    {
        m_logic.addFixedPeer(name, addresses);
    }

    void
    addFallbackStrings(
        std::string const& name,
        std::vector<std::string> const& strings) override
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
    new_inbound_slot(
        beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint) override
    {
        return m_logic.new_inbound_slot(local_endpoint, remote_endpoint);
    }

    std::pair<std::shared_ptr<Slot>, Result>
    new_outbound_slot(beast::IP::Endpoint const& remote_endpoint) override
    {
        return m_logic.new_outbound_slot(remote_endpoint);
    }

    void
    on_endpoints(std::shared_ptr<Slot> const& slot, Endpoints const& endpoints)
        override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.on_endpoints(impl, endpoints);
    }

    void
    on_closed(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.on_closed(impl);
    }

    void
    on_failure(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        m_logic.on_failure(impl);
    }

    void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remote_address,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) override
    {
        m_logic.onRedirects(eps.begin(), eps.end(), remote_address);
    }

    //--------------------------------------------------------------------------

    bool
    onConnected(
        std::shared_ptr<Slot> const& slot,
        beast::IP::Endpoint const& local_endpoint) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.onConnected(impl, local_endpoint);
    }

    Result
    activate(
        std::shared_ptr<Slot> const& slot,
        PublicKey const& key,
        bool reserved) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.activate(impl, key, reserved);
    }

    std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return m_logic.redirect(impl);
    }

    std::vector<beast::IP::Endpoint>
    autoconnect() override
    {
        return m_logic.autoconnect();
    }

    void
    once_per_second() override
    {
        m_logic.once_per_second();
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() override
    {
        return m_logic.buildEndpointsForPeers();
    }

    void
    start() override
    {
        m_store.open(m_config);
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
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , activeInboundPeers(
                  collector->make_gauge("Peer_Finder", "Active_Inbound_Peers"))
            , activeOutboundPeers(
                  collector->make_gauge("Peer_Finder", "Active_Outbound_Peers"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge activeInboundPeers;
        beast::insight::Gauge activeOutboundPeers;
    };

    std::mutex m_statsMutex;
    Stats m_stats;

    void
    collect_metrics()
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.activeInboundPeers = m_logic.counts_.inboundActive();
        m_stats.activeOutboundPeers = m_logic.counts_.out_active();
    }
};

//------------------------------------------------------------------------------

Manager::Manager() noexcept : beast::PropertyStream::Source("peerfinder")
{
}

std::unique_ptr<Manager>
make_Manager(
    boost::asio::io_context& io_context,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<ManagerImp>(
        io_context, clock, journal, config, collector);
}

}  // namespace PeerFinder
}  // namespace ripple
