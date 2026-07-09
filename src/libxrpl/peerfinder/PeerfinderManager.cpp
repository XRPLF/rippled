#include <xrpl/peerfinder/PeerfinderManager.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/peerfinder/Config.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Checker.h>
#include <xrpl/peerfinder/detail/Logic.h>
#include <xrpl/peerfinder/detail/SlotImp.h>
#include <xrpl/peerfinder/detail/SourceStrings.h>
#include <xrpl/peerfinder/detail/Store.h>
#include <xrpl/peerfinder/make_Manager.h>
#include <xrpl/protocol/PublicKey.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {

class ManagerImp : public Manager
{
public:
    // NOLINTBEGIN(readability-identifier-naming)
    boost::asio::io_context& io_context_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    clock_type& clock_;
    beast::Journal journal_;
    Store& store_;
    Checker<boost::asio::ip::tcp> checker_;
    Logic<decltype(checker_)> logic_;
    // NOLINTEND(readability-identifier-naming)

    //--------------------------------------------------------------------------

    ManagerImp(
        boost::asio::io_context& ioContext,
        clock_type& clock,
        beast::Journal journal,
        Store& store,
        beast::insight::Collector::ptr const& collector)
        : io_context_(ioContext)
        , work_(std::in_place, boost::asio::make_work_guard(io_context_))
        , clock_(clock)
        , journal_(journal)
        , store_(store)
        , checker_(io_context_)
        , logic_(clock, store_, checker_, journal)
        , stats_([this] { collectMetrics(); }, collector)
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
    addFixedPeer(std::string_view name, std::vector<beast::IP::Endpoint> const& addresses) override
    {
        logic_.addFixedPeer(name, addresses);
    }

    void
    addFallbackStrings(std::string const& name, std::vector<std::string> const& strings) override
    {
        logic_.addStaticSource(SourceStrings::make(name, strings));
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
        return logic_.newInboundSlot(localEndpoint, remoteEndpoint);
    }

    std::pair<std::shared_ptr<Slot>, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint) override
    {
        return logic_.newOutboundSlot(remoteEndpoint);
    }

    void
    onEndpoints(std::shared_ptr<Slot> const& slot, Endpoints const& endpoints) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.onEndpoints(impl, endpoints);
    }

    void
    onClosed(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.onClosed(impl);
    }

    void
    onFailure(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        logic_.onFailure(impl);
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
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.onConnected(impl, localEndpoint);
    }

    Result
    activate(std::shared_ptr<Slot> const& slot, PublicKey const& key, bool reserved) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.activate(impl, key, reserved);
    }

    std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) override
    {
        SlotImp::ptr const impl(std::dynamic_pointer_cast<SlotImp>(slot));
        return logic_.redirect(impl);
    }

    std::vector<beast::IP::Endpoint>
    autoconnect() override
    {
        return logic_.autoconnect();
    }

    void
    oncePerSecond() override
    {
        logic_.oncePerSecond();
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() override
    {
        return logic_.buildEndpointsForPeers();
    }

    void
    start() override
    {
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
            : hook(collector->makeHook(handler))
            , activeInboundPeers(collector->makeGauge("Peer_Finder", "Active_Inbound_Peers"))
            , activeOutboundPeers(collector->makeGauge("Peer_Finder", "Active_Outbound_Peers"))
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
        std::scoped_lock const lock(statsMutex_);
        stats_.activeInboundPeers = logic_.counts().inboundActive();
        stats_.activeOutboundPeers = logic_.counts().outActive();
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
    Store& store,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<ManagerImp>(ioContext, clock, journal, store, collector);
}

}  // namespace xrpl::PeerFinder
