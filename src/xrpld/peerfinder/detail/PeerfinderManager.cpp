#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Checker.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/SourceStrings.h>
#include <xrpld/peerfinder/detail/StoreSqdb.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <memory>
#include <optional>

namespace xrpl {
namespace PeerFinder {

class ManagerImp : public Manager
{
public:
    // NOLINTBEGIN(readability-identifier-naming)
    boost::asio::io_context& io_context_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    clock_type& clock_;
    beast::Journal journal_;
    StoreSqdb store_;
    Checker<boost::asio::ip::tcp> checker_;
    Logic<decltype(checker_)> logic_;
    BasicConfig const& config_;
    // NOLINTEND(readability-identifier-naming)

    //--------------------------------------------------------------------------

    ManagerImp(
        boost::asio::io_context& ioContext,
        clock_type& clock,
        beast::Journal journal,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector)
        : io_context_(ioContext)
        , work_(std::in_place, boost::asio::make_work_guard(io_context_))
        , clock_(clock)
        , journal_(journal)
        , store_(journal)
        , checker_(io_context_)
        , logic_(clock, store_, checker_, journal)
        , config_(config)
        , stats_(std::bind(&ManagerImp::collectMetrics, this), collector)
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
            logic_.stop();
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
        logic_.config(config);
    }

    Config
    config() override
    {
        return logic_.config();
    }

    void
    addFixedPeer(std::string const& name, std::vector<beast::IP::Endpoint> const& addresses)
        override
    {
        logic_.addFixedPeer(name, addresses);
    }

    void
    addFallbackStrings(std::string const& name, std::vector<std::string> const& strings) override
    {
        logic_.addStaticSource(SourceStrings::New(name, strings));
    }

    void
    addFallbackURL(std::string const& name, std::string const& url)
    {
        // VFALCO TODO This needs to be implemented
    }

    //--------------------------------------------------------------------------

    std::pair<std::shared_ptr<Slot>, Result>
    new_inbound_slot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint) override
    {
        return logic_.new_inbound_slot(localEndpoint, remoteEndpoint);
    }

    std::pair<std::shared_ptr<Slot>, Result>
    new_outbound_slot(beast::IP::Endpoint const& remoteEndpoint) override
    {
        return logic_.new_outbound_slot(remoteEndpoint);
    }

    void
    on_endpoints(std::shared_ptr<Slot> const& slot, Endpoints const& endpoints) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.on_endpoints(impl, endpoints);
    }

    void
    on_closed(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.on_closed(impl);
    }

    void
    on_failure(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.on_failure(impl);
    }

    void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remoteAddress,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) override
    {
        logic_.onRedirects(eps.begin(), eps.end(), remoteAddress);
    }

    //--------------------------------------------------------------------------

    bool
    onConnected(std::shared_ptr<Slot> const& slot, beast::IP::Endpoint const& localEndpoint)
        override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.onConnected(impl, localEndpoint);
    }

    Result
    activate(std::shared_ptr<Slot> const& slot, PublicKey const& key, bool reserved) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.activate(impl, key, reserved);
    }

    std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.redirect(impl);
    }

    std::vector<beast::IP::Endpoint>
    autoconnect() override
    {
        return logic_.autoconnect();
    }

    void
    once_per_second() override
    {
        logic_.once_per_second();
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() override
    {
        return logic_.buildEndpointsForPeers();
    }

    void
    start() override
    {
        store_.open(config_);
        logic_.load();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void
    onWrite(beast::PropertyStream::Map& map) override
    {
        logic_.onWrite(map);
    }

private:
    struct Stats
    {
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , activeInboundPeers(collector->make_gauge("Peer_Finder", "Active_Inbound_Peers"))
            , activeOutboundPeers(collector->make_gauge("Peer_Finder", "Active_Outbound_Peers"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge activeInboundPeers;
        beast::insight::Gauge activeOutboundPeers;
    };

    std::mutex statsMutex_;
    Stats stats_;

    void
    collectMetrics()
    {
        std::lock_guard lock(statsMutex_);
        stats_.activeInboundPeers = logic_.counts_.inboundActive();
        stats_.activeOutboundPeers = logic_.counts_.out_active();
    }
};

//------------------------------------------------------------------------------

Manager::Manager() noexcept : beast::PropertyStream::Source("peerfinder")
{
}

std::unique_ptr<Manager>
makeManager(
    boost::asio::io_context& ioContext,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<ManagerImp>(ioContext, clock, journal, config, collector);
}

}  // namespace PeerFinder
}  // namespace xrpl
