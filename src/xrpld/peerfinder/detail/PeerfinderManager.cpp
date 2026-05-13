/** @file
 *  Concrete implementation of the PeerFinder `Manager` interface.
 *
 *  Defines `ManagerImp`, which wires together the SQLite bootcache store,
 *  the async TCP reachability checker, and the `Logic` policy engine into a
 *  single cohesive subsystem.  The class is intentionally hidden from all
 *  callers; the only externally visible symbols are the `Manager` base class
 *  (declared in `PeerfinderManager.h`) and the `makeManager()` factory below.
 */

#include <xrpld/peerfinder/PeerfinderManager.h>

#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Checker.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/SourceStrings.h>
#include <xrpld/peerfinder/detail/StoreSqdb.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/protocol/PublicKey.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {

/** Concrete implementation of the PeerFinder `Manager` interface.
 *
 *  `ManagerImp` is the assembly point for the XRPL peer-discovery subsystem.
 *  It composes a SQLite-backed address store (`StoreSqdb`), an async TCP
 *  reachability prober (`Checker`), and the central policy engine (`Logic`),
 *  then delegates every `Manager` virtual call to `Logic` via a one-liner
 *  forwarding pattern.
 *
 *  Slot events accept `std::shared_ptr<Slot>` (the opaque public type) and
 *  downcast to `SlotImp` before forwarding, enforcing the layering boundary
 *  between callers and the internal machinery.  The casts are safe because
 *  all `Slot` instances in circulation are created by `Logic` as `SlotImp`.
 *
 *  @note This class is not exposed in any header.  All consumer code must
 *      interact through the `Manager` interface returned by `makeManager()`.
 */
class ManagerImp : public Manager
{
public:
    // NOLINTBEGIN(readability-identifier-naming)
    /** The `io_context` shared with the rest of the node. */
    boost::asio::io_context& io_context_;

    /** Keeps `io_context_` running while the manager is active.
     *
     *  Constructed `std::in_place` during initialization and destroyed
     *  (via `work_.reset()`) in `stop()`.  This is the canonical Asio
     *  pattern for preventing a context from exiting when its queue is
     *  transiently empty.
     */
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;

    /** Abstract clock injected for testability. */
    clock_type& clock_;

    /** Diagnostic journal for all PeerFinder log output. */
    beast::Journal journal_;

    /** SQLite-backed persistence for the bootstrap address cache.
     *
     *  Not opened until `start()` is called; construction is therefore
     *  cheap and performs no disk I/O.
     */
    StoreSqdb store_;

    /** Async TCP prober that verifies candidate peers' listening ports.
     *
     *  Shares `io_context_` so its async operations participate in the same
     *  event loop as the rest of the node.
     */
    Checker<boost::asio::ip::tcp> checker_;

    /** Central policy engine for peer discovery and slot management.
     *
     *  Nearly every `Manager` virtual method is a one-liner that forwards
     *  to `logic_`.
     */
    Logic<decltype(checker_)> logic_;

    /** Server-level configuration used to open the SQLite store in `start()`. */
    BasicConfig const& config_;
    // NOLINTEND(readability-identifier-naming)

    //--------------------------------------------------------------------------

    /** Construct and wire all subsystem components.
     *
     *  The work guard is armed immediately so the `io_context` stays alive
     *  from construction onward.  The SQLite store is NOT opened here;
     *  call `start()` to perform disk I/O and populate the boot cache.
     *
     *  @param ioContext  The shared Asio I/O context.
     *  @param clock      Abstract clock for TTL and backoff calculations.
     *  @param journal    Diagnostic sink for PeerFinder log messages.
     *  @param config     Server-level configuration (used in `start()`).
     *  @param collector  Metrics collector for registering peer-count gauges.
     */
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

    /** Delegate to `stop()`, making explicit shutdown optional for callers. */
    ~ManagerImp() override
    {
        stop();
    }

    /** Shut down all subsystem components in dependency order.
     *
     *  Idempotent: guarded by `if (work_)` so calling twice — including
     *  from the destructor after an explicit `stop()` — is harmless.
     *
     *  Shutdown sequence:
     *  1. Reset the work guard to unblock `io_context_` after queued
     *     handlers drain.
     *  2. `checker_.stop()` — cancel all pending async TCP probe operations.
     *  3. `logic_.stop()` — cancel any in-flight source fetch.
     *
     *  The ordering is deliberate: `Logic` may hold shared references to
     *  `Checker` operations that must be cancelled before the checker itself
     *  can be destroyed.
     */
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
        logic_.addStaticSource(SourceStrings::make(name, strings));
    }

    /** @note Not yet implemented; body is intentionally empty.
     *
     *  The corresponding pure-virtual declaration is also commented out of
     *  the `Manager` base class header.  Fallback sources are currently
     *  supplied only as static string lists via `addFallbackStrings()`.
     */
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

    /** Open the SQLite store (running schema migration if needed) and
     *  populate the in-memory boot cache from persisted entries.
     *
     *  Must be called before any other method.  No disk I/O occurs before
     *  `start()`, making construction cheap and safe.
     */
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
    /** Metrics registration and live peer-count gauges.
     *
     *  The `hook` member is driven by the collector framework: the collector
     *  calls the registered handler whenever it wants fresh data rather than
     *  the application pushing on every change.  `statsMutex_` guards against
     *  concurrent calls from the collector, not from peer connection events.
     */
    struct Stats
    {
        /** Register the collection hook and create the two peer-count gauges.
         *
         *  @param handler   Callable invoked by the collector to refresh data;
         *      bound to `ManagerImp::collectMetrics`.
         *  @param collector The metrics backend that owns the hook and gauges.
         */
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->makeHook(handler))
            , activeInboundPeers(collector->makeGauge("Peer_Finder", "Active_Inbound_Peers"))
            , activeOutboundPeers(collector->makeGauge("Peer_Finder", "Active_Outbound_Peers"))
        {
        }

        /** Collector hook; its destructor deregisters the callback. */
        beast::insight::Hook hook;

        /** Gauge tracking the number of active inbound peer connections. */
        beast::insight::Gauge activeInboundPeers;

        /** Gauge tracking the number of active outbound peer connections. */
        beast::insight::Gauge activeOutboundPeers;
    };

    /** Guards `stats_` against concurrent metric-collection calls. */
    std::mutex statsMutex_;

    Stats stats_;

    /** Refresh peer-count gauges from live `Logic` counts.
     *
     *  Invoked by the collector framework via `stats_.hook`; protected by
     *  `statsMutex_` to guard against concurrent collector calls.
     */
    void
    collectMetrics()
    {
        std::scoped_lock const lock(statsMutex_);
        stats_.activeInboundPeers = logic_.counts().inboundActive();
        stats_.activeOutboundPeers = logic_.counts().outActive();
    }
};

//------------------------------------------------------------------------------

/** Register the `Manager` as a `beast::PropertyStream::Source` named "peerfinder". */
Manager::Manager() noexcept : beast::PropertyStream::Source("peerfinder")
{
}

/** Create the concrete `Manager` implementation.
 *
 *  Instantiates `ManagerImp` and returns it as a `std::unique_ptr<Manager>`,
 *  keeping the implementation type entirely hidden from callers.  Call
 *  `start()` on the returned object before issuing any other methods.
 *
 *  @param ioContext  The Asio I/O context shared with the rest of the node.
 *  @param clock      Abstract clock for TTL and backoff calculations.
 *  @param journal    Diagnostic sink for PeerFinder log messages.
 *  @param config     Server-level configuration; passed through to `start()`.
 *  @param collector  Metrics backend for registering peer-count gauges.
 *  @return A fully constructed (but not yet started) `Manager`.
 */
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

}  // namespace xrpl::PeerFinder
