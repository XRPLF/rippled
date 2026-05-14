/** @file
 *  Concrete implementation of the XRPL peer-to-peer overlay network.
 *
 *  Implements `OverlayImpl`, the operational heart of an XRPL node's P2P
 *  layer.  The class manages the full lifecycle of peer connections — from
 *  initial TLS acceptance and HTTP upgrade through cryptographic handshake,
 *  protocol negotiation, active message relay, and eventual teardown.  It
 *  also exposes three internal HTTP endpoints (`/crawl`, `/health`, `/vl/`)
 *  consumed by network topology tools, monitoring systems, and validator-list
 *  clients respectively.
 *
 *  Also defines the `setupOverlay` config parser and the `makeOverlay`
 *  factory, keeping the concrete type out of translation units that only
 *  need the `Overlay` interface.
 */
#include <xrpld/overlay/detail/OverlayImpl.h>

#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/detail/ConnectAttempt.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/TrafficCount.h>
#include <xrpld/overlay/detail/Tuning.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/make_Manager.h>
#include <xrpld/rpc/ServerHandler.h>
#include <xrpld/rpc/handlers/admin/status/GetCounts.h>
#include <xrpld/rpc/json_body.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Resolver.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/Manifest.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/server/SimpleWriter.h>
#include <xrpl/server/Wallet.h>
#include <xrpl/server/Writer.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/lexical_cast/try_lexical_convert.hpp>

#include <xrpl.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

/** Bitmask flags that control which sections appear in `/crawl` responses.
 *
 *  Composed from the `[crawl]` config section.  A value of `kDISABLED`
 *  (0) suppresses the endpoint entirely.
 */
namespace CrawlOptions {
/** `/crawl` endpoint is disabled; no response is served. */
static constexpr auto kDISABLED = 0;
/** Include peer-connectivity topology in the `overlay` field. */
static constexpr auto kOVERLAY = (1 << 0);
/** Include local server status info in the `server` field. */
static constexpr auto kSERVER_INFO = (1 << 1);
/** Include server performance counters in the `counts` field. */
static constexpr auto kSERVER_COUNTS = (1 << 2);
/** Include UNL / validator-list info in the `unl` field. */
static constexpr auto kUNL = (1 << 3);
}  // namespace CrawlOptions

//------------------------------------------------------------------------------

OverlayImpl::Child::Child(OverlayImpl& overlay) : overlay_(overlay)
{
}

OverlayImpl::Child::~Child()
{
    overlay_.remove(*this);
}

//------------------------------------------------------------------------------

OverlayImpl::Timer::Timer(OverlayImpl& overlay) : Child(overlay), timer(overlay_.io_context_)
{
}

/** Cancel the timer and prevent any further rescheduling.
 *
 *  Always called from the overlay strand, so it never races with `onTimer`.
 *  Sets `stopping` before cancelling so the in-flight handler (if any)
 *  sees the flag and exits without rescheduling.
 */
void
OverlayImpl::Timer::stop()
{
    stopping = true;
    timer.cancel();
}

/** Schedule the next one-second tick on the overlay strand. */
void
OverlayImpl::Timer::asyncWait()
{
    timer.expires_after(std::chrono::seconds(1));
    timer.async_wait(
        boost::asio::bind_executor(
            overlay_.strand_,
            std::bind(&Timer::onTimer, shared_from_this(), std::placeholders::_1)));
}

/** Execute per-second overlay maintenance tasks.
 *
 *  On each tick: drives `PeerFinder::oncePerSecond`, pushes endpoint
 *  advertisements, opens new outbound connections, and (when TX
 *  reduce-relay is enabled) flushes the per-peer TX hash queues.
 *  Every `Tuning::kCHECK_IDLE_PEERS` ticks, purges stale squelch slots
 *  via `deleteIdlePeers`.  Reschedules itself at the end of each
 *  successful tick.
 *
 *  A deliberate `operation_aborted` cancel (from `stop()`) is silently
 *  ignored; any other ASIO error is logged at error level.
 *
 *  @param ec ASIO error code from the expired timer wait.
 */
void
OverlayImpl::Timer::onTimer(error_code ec)
{
    if (ec || stopping)
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(overlay_.journal_.error()) << "on_timer: " << ec.message();
        }
        return;
    }

    overlay_.peerFinder_->oncePerSecond();
    overlay_.sendEndpoints();
    overlay_.autoConnect();
    if (overlay_.app_.config().TX_REDUCE_RELAY_ENABLE)
        overlay_.sendTxQueue();

    if ((++overlay_.timer_count_ % Tuning::kCHECK_IDLE_PEERS) == 0)
        overlay_.deleteIdlePeers();

    asyncWait();
}

//------------------------------------------------------------------------------

OverlayImpl::OverlayImpl(
    Application& app,
    Setup setup,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_context& ioContext,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : app_(app)
    , io_context_(ioContext)
    , work_(std::in_place, boost::asio::make_work_guard(io_context_))
    , strand_(boost::asio::make_strand(io_context_))
    , setup_(std::move(setup))
    , journal_(app_.getJournal("Overlay"))
    , serverHandler_(serverHandler)
    , resourceManager_(resourceManager)
    , peerFinder_(
          PeerFinder::makeManager(
              ioContext,
              stopwatch(),
              app_.getJournal("PeerFinder"),
              config,
              collector))
    , resolver_(resolver)
    , next_id_(1)
    , slots_(app, *this, app.config())
    , stats_(
          std::bind(&OverlayImpl::collectMetrics, this),
          collector,
          [counts = traffic_.getCounts(), collector]() {
              std::unordered_map<TrafficCount::Category, TrafficGauges> ret;

              for (auto const& pair : counts)
                  ret.emplace(pair.first, TrafficGauges(pair.second.name, collector));

              return ret;
          }())
{
    beast::PropertyStream::Source::add(peerFinder_.get());
}

/** Accept an inbound TLS connection and either handle it as a built-in HTTP
 *  request or upgrade it to a peer session.
 *
 *  Non-peer requests (`/crawl`, `/health`, `/vl/`) are dispatched to
 *  `processRequest` and return immediately.  For genuine peer upgrade
 *  requests the method performs sequential gating checks — resource limit,
 *  slot availability (IP-limit / self-connect), protocol-version negotiation,
 *  TLS channel-binding cookie, and cryptographic handshake — before creating
 *  a `PeerImp` and registering it in `peers_` and `list_`.
 *
 *  `peer->run()` is called while holding `mutex_` so that a concurrent
 *  `stop()` cannot drain the list before the peer has started its I/O.
 *  On any failure the PeerFinder slot is released via `onClosed` to keep
 *  slot accounting accurate.
 *
 *  @param streamPtr Ownership of the established TLS stream.
 *  @param request   The parsed HTTP upgrade (or plain) request.
 *  @param remoteEndpoint TCP endpoint of the connecting peer.
 *  @return A `Handoff` that is either consumed (moved) or carries an HTTP
 *      response to be written back to the caller.
 *  @throws Nothing — all `verifyHandshake` exceptions are caught internally
 *      and converted to HTTP 400 responses.
 */
Handoff
OverlayImpl::onHandoff(
    std::unique_ptr<stream_type>&& streamPtr,
    http_request_type&& request,
    endpoint_type remoteEndpoint)
{
    auto const id = next_id_++;
    auto peerJournal = app_.getJournal("Peer");
    beast::WrappedSink sink(peerJournal.sink(), makePrefix(id));
    beast::Journal const journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remoteEndpoint;

    error_code ec;
    auto const localEndpoint(streamPtr->next_layer().socket().local_endpoint(ec));
    if (ec)
    {
        JLOG(journal.debug()) << remoteEndpoint << " failed: " << ec.message();
        return handoff;
    }

    auto consumer =
        resourceManager_.newInboundEndpoint(beast::IPAddressConversion::fromAsio(remoteEndpoint));
    if (consumer.disconnect(journal))
        return handoff;

    auto const [slot, result] = peerFinder_->newInboundSlot(
        beast::IPAddressConversion::fromAsio(localEndpoint),
        beast::IPAddressConversion::fromAsio(remoteEndpoint));

    if (slot == nullptr)
    {
        handoff.moved = false;
        JLOG(journal.debug()) << "Peer " << remoteEndpoint << " refused, " << to_string(result);
        return handoff;
    }

    {
        auto const types = beast::rfc2616::splitCommas(request["Connect-As"]);
        if (std::ranges::find_if(types, [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response = makeRedirectResponse(slot, request, remoteEndpoint.address());
            handoff.keep_alive = beast::rfc2616::isKeepAlive(request);
            return handoff;
        }
    }

    auto const negotiatedVersion = negotiateProtocolVersion(request["Upgrade"]);
    if (!negotiatedVersion)
    {
        peerFinder_->onClosed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot, request, remoteEndpoint.address(), "Unable to agree on a protocol version");
        handoff.keep_alive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*streamPtr, journal);
    if (!sharedValue)
    {
        peerFinder_->onClosed(slot);
        handoff.moved = false;
        handoff.response =
            makeErrorResponse(slot, request, remoteEndpoint.address(), "Incorrect security cookie");
        handoff.keep_alive = false;
        return handoff;
    }

    try
    {
        auto publicKey = verifyHandshake(
            request,
            *sharedValue,
            setup_.networkID,
            setup_.publicIp,
            remoteEndpoint.address(),
            app_);

        consumer.setPublicKey(publicKey);

        {
            bool const reserved = static_cast<bool>(app_.getCluster().member(publicKey)) ||
                app_.getPeerReservations().contains(publicKey);
            auto const result = peerFinder_->activate(slot, publicKey, reserved);
            if (result != PeerFinder::Result::Success)
            {
                peerFinder_->onClosed(slot);
                JLOG(journal.debug())
                    << "Peer " << remoteEndpoint << " redirected, " << to_string(result);
                handoff.moved = false;
                handoff.response = makeRedirectResponse(slot, request, remoteEndpoint.address());
                handoff.keep_alive = false;
                return handoff;
            }
        }

        auto const peer = std::make_shared<PeerImp>(
            app_,
            id,
            slot,
            std::move(request),
            publicKey,
            *negotiatedVersion,
            consumer,
            std::move(streamPtr),
            *this);
        {
            std::scoped_lock const lock(mutex_);
            {
                auto const result = peers_.emplace(peer->slot(), peer);
                XRPL_ASSERT(result.second, "xrpl::OverlayImpl::onHandoff : peer is inserted");
                (void)result.second;
            }
            list_.emplace(peer.get(), peer);

            peer->run();
        }
        handoff.moved = true;
        return handoff;
    }
    catch (std::exception const& e)
    {
        JLOG(journal.debug()) << "Peer " << remoteEndpoint << " fails handshake (" << e.what()
                              << ")";

        peerFinder_->onClosed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(slot, request, remoteEndpoint.address(), e.what());
        handoff.keep_alive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

/** Return true if the request is an HTTP upgrade listing at least one
 *  recognized XRPL protocol version.
 *
 *  @param request The incoming HTTP request to inspect.
 *  @return `true` when the request should be treated as a peer connection.
 */
bool
OverlayImpl::isPeerUpgrade(http_request_type const& request)
{
    if (!isUpgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

/** Format a zero-padded three-digit peer ID prefix for log sinks.
 *
 *  @param id Short numeric peer identifier.
 *  @return String of the form `"[NNN] "` for use with `beast::WrappedSink`.
 */
std::string
OverlayImpl::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

/** Build an HTTP 503 response carrying a JSON `peer-ips` redirect list.
 *
 *  Sent when a new inbound connection is refused due to slot limits.
 *  The body contains alternative peer addresses obtained from
 *  `PeerFinder::redirect()` so the rejected peer can try elsewhere.
 *
 *  @param slot          The refused PeerFinder slot (used for redirect list).
 *  @param request       The original HTTP request (version is echoed back).
 *  @param remoteAddress Source IP echoed in the `Remote-Address` header.
 *  @return A `Writer` wrapping the serialised HTTP response.
 */
std::shared_ptr<Writer>
OverlayImpl::makeRedirectResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remoteAddress)
{
    boost::beast::http::response<JsonBody> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::service_unavailable);
    msg.insert("Server", BuildInfo::getFullVersionString());
    {
        std::ostringstream ostr;
        ostr << remoteAddress;
        msg.insert("Remote-Address", ostr.str());
    }
    msg.insert("Content-Type", "application/json");
    msg.insert(boost::beast::http::field::connection, "close");
    msg.body() = json::ValueType::Object;
    {
        json::Value& ips = (msg.body()["peer-ips"] = json::ValueType::Array);
        for (auto const& _ : peerFinder_->redirect(slot))
            ips.append(_.address.toString());
    }
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

/** Build an HTTP 400 response for a failed handshake or protocol error.
 *
 *  @param slot          The refused slot (unused in body, kept for symmetry
 *      with `makeRedirectResponse`).
 *  @param request       The original HTTP request (version is echoed back).
 *  @param remoteAddress Source IP echoed in the `Remote-Address` header.
 *  @param text          Human-readable reason appended to the status line.
 *  @return A `Writer` wrapping the serialised HTTP 400 response.
 */
std::shared_ptr<Writer>
OverlayImpl::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remoteAddress,
    std::string text)
{
    boost::beast::http::response<boost::beast::http::empty_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::bad_request);
    msg.reason("Bad Request (" + text + ")");
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Remote-Address", remoteAddress.to_string());
    msg.insert(boost::beast::http::field::connection, "close");
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

//------------------------------------------------------------------------------

/** Initiate an outbound connection to a remote peer endpoint.
 *
 *  Checks the resource manager and PeerFinder slot availability before
 *  creating a `ConnectAttempt`.  Silently returns if the resource limit
 *  is exceeded or no outbound slot is available.
 *
 *  @param remoteEndpoint The target address and port to connect to.
 */
void
OverlayImpl::connect(beast::IP::Endpoint const& remoteEndpoint)
{
    XRPL_ASSERT(work_, "xrpl::OverlayImpl::connect : work is set");

    auto usage = resourceManager().newOutboundEndpoint(remoteEndpoint);
    if (usage.disconnect(journal_))
    {
        JLOG(journal_.info()) << "Over resource limit: " << remoteEndpoint;
        return;
    }

    auto const [slot, result] = peerFinder().newOutboundSlot(remoteEndpoint);
    if (slot == nullptr)
    {
        JLOG(journal_.debug()) << "Connect: No slot for " << remoteEndpoint << ": "
                               << to_string(result);
        return;
    }

    auto const p = std::make_shared<ConnectAttempt>(
        app_,
        io_context_,
        beast::IPAddressConversion::toAsioEndpoint(remoteEndpoint),
        usage,
        setup_.context,
        next_id_++,
        slot,
        app_.getJournal("Peer"),
        *this);

    std::scoped_lock const lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

/** Register a fully handshaked outbound peer in both peer registries.
 *
 *  Called by `ConnectAttempt` after a successful outbound handshake.
 *  Populates both `peers_` (slot → peer) and `ids_` (id → peer) atomically
 *  under `mutex_`, then calls `peer->run()` while still holding the lock so
 *  that a concurrent `stop()` cannot drain the list before I/O begins.
 *
 *  @param peer The newly activated outbound peer.
 */
void
OverlayImpl::addActive(std::shared_ptr<PeerImp> const& peer)
{
    beast::WrappedSink sink{journal_.sink(), peer->prefix()};
    beast::Journal const journal{sink};

    std::scoped_lock const lock(mutex_);

    {
        auto const result = peers_.emplace(peer->slot(), peer);
        XRPL_ASSERT(result.second, "xrpl::OverlayImpl::addActive : peer is inserted");
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct, std::make_tuple(peer->id()), std::make_tuple(peer));
        XRPL_ASSERT(result.second, "xrpl::OverlayImpl::addActive : peer ID is inserted");
        (void)result.second;
    }

    list_.emplace(peer.get(), peer);

    JLOG(journal.debug()) << "activated";

    peer->run();
}

/** Remove a peer from the slot-keyed registry when its PeerFinder slot closes.
 *
 *  @param slot The PeerFinder slot whose associated peer entry should be erased.
 */
void
OverlayImpl::remove(std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::scoped_lock const lock(mutex_);
    auto const iter = peers_.find(slot);
    XRPL_ASSERT(iter != peers_.end(), "xrpl::OverlayImpl::remove : valid input");
    peers_.erase(iter);
}

/** Start the overlay: configure PeerFinder, seed the boot cache, and arm
 *  the per-second timer.
 *
 *  Bootstrap IPs are sourced in priority order: `[ips]` → `[ips_fixed]` →
 *  four hardcoded well-known nodes (Ripple Labs, ISRDC, XRPL Kuwait, XRPL
 *  Commons).  All resolution is asynchronous; `resolver_.resolve()` callbacks
 *  push results into PeerFinder's fallback list.  Fixed peers (`[ips_fixed]`)
 *  are registered separately as always-reconnect entries.
 */
void
OverlayImpl::start()
{
    PeerFinder::Config const config = PeerFinder::Config::makeConfig(
        app_.config(),
        serverHandler_.setup().overlay.port(),
        app_.getValidationPublicKey().has_value(),
        setup_.ipLimit);

    peerFinder_->setConfig(config);
    peerFinder_->start();

    auto bootstrapIps = app_.config().IPS.empty() ? app_.config().IPS_FIXED : app_.config().IPS;

    if (bootstrapIps.empty())
    {
        bootstrapIps.emplace_back("r.ripple.com 51235");         // Ripple Labs Inc.
        bootstrapIps.emplace_back("sahyadri.isrdc.in 51235");    // ISRDC
        bootstrapIps.emplace_back("hubs.xrpkuwait.com 51235");   // @Xrpkuwait
        bootstrapIps.emplace_back("hub.xrpl-commons.org 51235"); // XRPL Commons
    }

    resolver_.resolve(
        bootstrapIps,
        [this](std::string const& name, std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                {
                    ips.push_back(to_string(addr.atPort(kDEFAULT_PEER_PORT)));
                }
                else
                {
                    ips.push_back(to_string(addr));
                }
            }

            std::string const base("config: ");
            if (!ips.empty())
                peerFinder_->addFallbackStrings(base + name, ips);
        });

    if (!app_.config().standalone() && !app_.config().IPS_FIXED.empty())
    {
        resolver_.resolve(
            app_.config().IPS_FIXED,
            [this](std::string const& name, std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                    {
                        ips.emplace_back(addr.address(), kDEFAULT_PEER_PORT);
                    }
                    else
                    {
                        ips.emplace_back(addr);
                    }
                }

                if (!ips.empty())
                    peerFinder_->addFixedPeer(name, ips);
            });
    }
    auto const timer = std::make_shared<Timer>(*this);
    std::scoped_lock const lock(mutex_);
    list_.emplace(timer.get(), timer);
    timer_ = timer;
    timer->asyncWait();
}

/** Shut down the overlay and block until all children have stopped.
 *
 *  Dispatches `stopChildren` to the strand, then waits on `cond_` until
 *  `list_` drains to empty — each child's destructor signals `cond_` via
 *  `remove(Child&)`.  Stops `peerFinder_` after the drain.
 */
void
OverlayImpl::stop()
{
    boost::asio::dispatch(strand_, std::bind(&OverlayImpl::stopChildren, this));
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cond_.wait(lock, [this] { return list_.empty(); });
    }
    peerFinder_->stop();
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
OverlayImpl::onWrite(beast::PropertyStream::Map& stream)
{
    beast::PropertyStream::Set set("traffic", stream);
    auto const stats = traffic_.getCounts();
    for (auto const& pair : stats)
    {
        beast::PropertyStream::Map item(set);
        item["category"] = pair.second.name;
        item["bytes_in"] = std::to_string(pair.second.bytesIn.load());
        item["messages_in"] = std::to_string(pair.second.messagesIn.load());
        item["bytes_out"] = std::to_string(pair.second.bytesOut.load());
        item["messages_out"] = std::to_string(pair.second.messagesOut.load());
    }
}

//------------------------------------------------------------------------------
/** Register an inbound peer in the ID-keyed relay registry after handshake.
 *
 *  Called once the protocol handshake is complete and the peer's public key
 *  is known.  Adds the peer to `ids_` so it can receive broadcast and relay
 *  messages.  (For inbound peers, `peers_` was populated earlier in
 *  `onHandoff`; for outbound peers `addActive` populates both maps together.)
 *
 *  @param peer The newly activated inbound peer.
 */
void
OverlayImpl::activate(std::shared_ptr<PeerImp> const& peer)
{
    beast::WrappedSink sink{journal_.sink(), peer->prefix()};
    beast::Journal const journal{sink};

    {
        std::scoped_lock const lock(mutex_);
        auto const result(ids_.emplace(
            std::piecewise_construct, std::make_tuple(peer->id()), std::make_tuple(peer)));
        XRPL_ASSERT(result.second, "xrpl::OverlayImpl::activate : peer ID is inserted");
        (void)result.second;
    }

    JLOG(journal.debug()) << "activated";
    XRPL_ASSERT(size(), "xrpl::OverlayImpl::activate : nonzero peers");
}

/** Remove a peer from the ID-keyed relay registry on deactivation.
 *
 *  @param id Short peer identifier to erase from `ids_`.
 */
void
OverlayImpl::onPeerDeactivate(Peer::id_t id)
{
    std::scoped_lock const lock(mutex_);
    ids_.erase(id);
}

/** Process a received `TMManifests` message and relay newly accepted entries.
 *
 *  Each manifest is applied via `ValidatorManifests::applyManifest`.  Those
 *  with `ManifestDisposition::Accepted` are republished to the application
 *  layer (`pubManifest`) and, if the master key is listed in the validator
 *  set, persisted to the wallet database.  All accepted entries are
 *  forwarded to every active peer as a new `TMManifests` message.
 *
 *  @param m    The incoming manifest batch.
 *  @param from The peer that sent the message (used for journal context).
 */
void
OverlayImpl::onManifests(
    std::shared_ptr<protocol::TMManifests> const& m,
    std::shared_ptr<PeerImp> const& from)
{
    auto const n = m->list_size();
    auto const& journal = from->pJournal();

    protocol::TMManifests relay;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto& s = m->list().Get(i).stobject();

        if (auto mo = deserializeManifest(s))
        {
            auto const serialized = mo->serialized;

            auto const result = app_.getValidatorManifests().applyManifest(std::move(*mo));

            if (result == ManifestDisposition::Accepted)
            {
                relay.add_list()->set_stobject(s);

                // N.B.: this is important; the applyManifest call above moves
                //       the loaded Manifest out of the optional so we need to
                //       reload it here.
                mo = deserializeManifest(serialized);
                XRPL_ASSERT(
                    mo,
                    "xrpl::OverlayImpl::onManifests : manifest "
                    "deserialization succeeded");
                // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above
                app_.getOPs().pubManifest(*mo);

                if (app_.getValidators().listed(mo->masterKey))
                {
                    auto db = app_.getWalletDB().checkoutDb();
                    addValidatorManifest(*db, serialized);
                }
                // NOLINTEND(bugprone-unchecked-optional-access)
            }
        }
        else
        {
            JLOG(journal.debug()) << "Malformed manifest #" << i + 1 << ": " << strHex(s);
            continue;
        }
    }

    if (!relay.list().empty())
    {
        forEach([m2 = std::make_shared<Message>(relay, protocol::mtMANIFESTS)](
                    std::shared_ptr<PeerImp> const& p) { p->send(m2); });
    }
}

void
OverlayImpl::reportInboundTraffic(TrafficCount::Category cat, int size)
{
    traffic_.addCount(cat, true, size);
}

void
OverlayImpl::reportOutboundTraffic(TrafficCount::Category cat, int size)
{
    traffic_.addCount(cat, false, size);
}
/** Return the number of fully-activated peers running the XRPL protocol.
 *
 *  Counts only peers that have completed the handshake (present in `ids_`).
 *  Peers still in the TLS/HTTP upgrade phase are not counted.
 *
 *  @return Current active peer count.
 */
std::size_t
OverlayImpl::size() const
{
    std::scoped_lock const lock(mutex_);
    return ids_.size();
}

/** Return the configured maximum number of active peers.
 *
 *  @return The `maxPeers` value from the current PeerFinder configuration.
 */
int
OverlayImpl::limit()
{
    return peerFinder_->config().maxPeers;
}

/** Build the `overlay.active` JSON array for the `/crawl` endpoint.
 *
 *  Each entry contains public key (base64), connection direction, uptime,
 *  and optionally IP/port when the peer's `crawl()` flag permits disclosure.
 *  Ledger range is included when the peer has reported non-zero bounds.
 *
 *  @return JSON object with an `active` array of peer descriptors.
 */
json::Value
OverlayImpl::getOverlayInfo() const
{
    using namespace std::chrono;
    json::Value jv;
    auto& av = jv[jss::active] = json::Value(json::ValueType::Array);

    forEach([&](std::shared_ptr<PeerImp> const& sp) {
        auto& pv = av.append(json::Value(json::ValueType::Object));
        pv[jss::public_key] = base64Encode(sp->getNodePublic().data(), sp->getNodePublic().size());
        pv[jss::type] = sp->slot()->inbound() ? jss::in : jss::out;
        pv[jss::uptime] = static_cast<std::uint32_t>(duration_cast<seconds>(sp->uptime()).count());
        if (sp->crawl())
        {
            pv[jss::ip] = sp->getRemoteAddress().address().to_string();
            if (sp->slot()->inbound())
            {
                if (auto port = sp->slot()->listeningPort())
                    pv[jss::port] = *port;
            }
            else
            {
                pv[jss::port] = sp->getRemoteAddress().port();
            }
        }

        {
            auto version{sp->getVersion()};
            if (!version.empty())
            {
                // Could move here if json::value supported moving from strings
                pv[jss::version] = std::string{version};
            }
        }

        std::uint32_t minSeq = 0, maxSeq = 0;
        sp->ledgerRange(minSeq, maxSeq);
        if (minSeq != 0 || maxSeq != 0)
            pv[jss::complete_ledgers] = std::to_string(minSeq) + "-" + std::to_string(maxSeq);
    });

    return jv;
}

/** Build a filtered server-info JSON object for the `/crawl` endpoint.
 *
 *  Calls `NetworkOPs::getServerInfo` with public (non-admin, non-human)
 *  settings and strips fields not intended for external consumption:
 *  `hostid`, escalation and queue load factors, quorum, and the raw fee
 *  fields from `validated_ledger`.
 *
 *  @return Filtered JSON object describing local server status.
 */
json::Value
OverlayImpl::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    json::Value serverInfo = app_.getOPs().getServerInfo(humanReadable, admin, counters);

    serverInfo.removeMember(jss::hostid);
    serverInfo.removeMember(jss::load_factor_fee_escalation);
    serverInfo.removeMember(jss::load_factor_fee_queue);
    serverInfo.removeMember(jss::validation_quorum);

    if (serverInfo.isMember(jss::validated_ledger))
    {
        json::Value& validatedLedger = serverInfo[jss::validated_ledger];

        validatedLedger.removeMember(jss::base_fee);
        validatedLedger.removeMember(jss::reserve_base_xrp);
        validatedLedger.removeMember(jss::reserve_inc_xrp);
    }

    return serverInfo;
}

/** Return server performance counters for the `/crawl` endpoint.
 *
 *  @return JSON object from `getCountsJson` with a minimum-threshold of 10.
 */
json::Value
OverlayImpl::getServerCounts()
{
    return getCountsJson(app_, 10);
}

/** Build a filtered UNL/validator-list JSON object for the `/crawl` endpoint.
 *
 *  Returns validator and publisher-list metadata with sensitive fields
 *  stripped (`list` entries per publisher, `signing_keys`,
 *  `trusted_validator_keys`, `validation_quorum`).  Appends
 *  `validator_sites` from `ValidatorSites`.
 *
 *  @return Filtered JSON object describing the node's validator configuration.
 */
json::Value
OverlayImpl::getUnlInfo()
{
    json::Value validators = app_.getValidators().getJson();

    if (validators.isMember(jss::publisher_lists))
    {
        json::Value& publisherLists = validators[jss::publisher_lists];

        for (auto& publisher : publisherLists)
        {
            publisher.removeMember(jss::list);
        }
    }

    validators.removeMember(jss::signing_keys);
    validators.removeMember(jss::trusted_validator_keys);
    validators.removeMember(jss::validation_quorum);

    json::Value validatorSites = app_.getValidatorSites().getJson();

    if (validatorSites.isMember(jss::validator_sites))
    {
        validators[jss::validator_sites] = std::move(validatorSites[jss::validator_sites]);
    }

    return validators;
}

/** Return a JSON array of per-peer status objects for all active peers.
 *
 *  @return JSON array where each element is the result of `Peer::json()`.
 */
json::Value
OverlayImpl::json()
{
    json::Value json;
    for (auto const& peer : getActivePeers())
    {
        json.append(peer->json());
    }
    return json;
}

bool
OverlayImpl::processCrawl(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/crawl" || setup_.crawlOptions == CrawlOptions::kDISABLED)
        return false;

    boost::beast::http::response<JsonBody> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = json::Value(2u);

    if ((setup_.crawlOptions & CrawlOptions::kOVERLAY) != 0u)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if ((setup_.crawlOptions & CrawlOptions::kSERVER_INFO) != 0u)
    {
        msg.body()["server"] = getServerInfo();
    }
    if ((setup_.crawlOptions & CrawlOptions::kSERVER_COUNTS) != 0u)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if ((setup_.crawlOptions & CrawlOptions::kUNL) != 0u)
    {
        msg.body()["unl"] = getUnlInfo();
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processValidatorList(http_request_type const& req, Handoff& handoff)
{
    constexpr std::string_view kPREFIX("/vl/");

    if (!req.target().starts_with(kPREFIX) || !setup_.vlEnabled)
        return false;

    std::uint32_t version = 1;

    boost::beast::http::response<JsonBody> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto fail = [&msg, &handoff](auto status) {
        msg.result(status);
        msg.insert("Content-Length", "0");

        msg.body() = json::ValueType::Null;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    };

    std::string_view key = req.target().substr(kPREFIX.size());

    if (auto slash = key.find('/'); slash != std::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    auto vl = app_.getValidators().getAvailable(key, version);

    if (!vl)
        return fail(boost::beast::http::status::not_found);
    if (!*vl)
    {
        return fail(boost::beast::http::status::bad_request);
    }

    msg.result(boost::beast::http::status::ok);

    msg.body() = *vl;

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

/** Classify node health and respond with an HTTP status that encodes the result.
 *
 *  Health is the maximum severity of any triggered condition:
 *
 *  | Condition                              | Warning | Critical |
 *  |----------------------------------------|---------|----------|
 *  | Validated-ledger age 7–19 s            | ✓       |          |
 *  | Validated-ledger age ≥ 20 s or missing |         | ✓        |
 *  | Amendment blocked                      |         | ✓        |
 *  | 1–7 peers                              | ✓       |          |
 *  | 0 peers                                |         | ✓        |
 *  | Server in syncing/tracking/connected   | ✓       |          |
 *  | Server in any other non-operational    |         | ✓        |
 *  | Load factor 100–999                    | ✓       |          |
 *  | Load factor ≥ 1000                     |         | ✓        |
 *
 *  HTTP status encodes the result directly (200 / 503 / 500) so load
 *  balancers can gate on status without parsing JSON.
 *
 *  @param req     The incoming HTTP request.
 *  @param handoff Populated with the response writer on match.
 *  @return `true` if the request was for `/health` and was handled.
 */
bool
OverlayImpl::processHealth(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/health")
        return false;
    boost::beast::http::response<JsonBody> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto info = getServerInfo();

    int lastValidatedLedgerAge = -1;
    if (info.isMember(jss::validated_ledger))
        lastValidatedLedgerAge = info[jss::validated_ledger][jss::age].asInt();
    bool amendmentBlocked = false;
    if (info.isMember(jss::amendment_blocked))
        amendmentBlocked = true;
    int const numberPeers = info[jss::peers].asInt();
    std::string const serverState = info[jss::server_state].asString();
    auto loadFactor = info[jss::load_factor_server].asDouble() / info[jss::load_base].asDouble();

    enum class HealthState { Healthy, Warning, Critical };
    auto health = HealthState::Healthy;
    auto setHealth = [&health](HealthState state) { health = std::max(health, state); };

    msg.body()[jss::info] = json::ValueType::Object;
    if (lastValidatedLedgerAge >= 7 || lastValidatedLedgerAge < 0)
    {
        msg.body()[jss::info][jss::validated_ledger] = lastValidatedLedgerAge;
        if (lastValidatedLedgerAge < 20)
        {
            setHealth(HealthState::Warning);
        }
        else
        {
            setHealth(HealthState::Critical);
        }
    }

    if (amendmentBlocked)
    {
        msg.body()[jss::info][jss::amendment_blocked] = true;
        setHealth(HealthState::Critical);
    }

    if (numberPeers <= 7)
    {
        msg.body()[jss::info][jss::peers] = numberPeers;
        if (numberPeers != 0)
        {
            setHealth(HealthState::Warning);
        }
        else
        {
            setHealth(HealthState::Critical);
        }
    }

    if (!(serverState == "full" || serverState == "validating" || serverState == "proposing"))
    {
        msg.body()[jss::info][jss::server_state] = serverState;
        if (serverState == "syncing" || serverState == "tracking" || serverState == "connected")
        {
            setHealth(HealthState::Warning);
        }
        else
        {
            setHealth(HealthState::Critical);
        }
    }

    if (loadFactor > 100)
    {
        msg.body()[jss::info][jss::load_factor] = loadFactor;
        if (loadFactor < 1000)
        {
            setHealth(HealthState::Warning);
        }
        else
        {
            setHealth(HealthState::Critical);
        }
    }

    switch (health)
    {
        case HealthState::Healthy:
            msg.result(boost::beast::http::status::ok);
            break;
        case HealthState::Warning:
            msg.result(boost::beast::http::status::service_unavailable);
            break;
        case HealthState::Critical:
            msg.result(boost::beast::http::status::internal_server_error);
            break;
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processRequest(http_request_type const& req, Handoff& handoff)
{
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}

/** Return a snapshot of all fully-activated peers.
 *
 *  @return Vector of shared peer pointers for all peers in `ids_`.
 */
Overlay::PeerSequence
OverlayImpl::getActivePeers() const
{
    Overlay::PeerSequence ret;
    ret.reserve(size());

    forEach([&ret](std::shared_ptr<PeerImp> const& sp) { ret.emplace_back(sp); });

    return ret;
}

Overlay::PeerSequence
OverlayImpl::getActivePeers(
    std::set<Peer::id_t> const& toSkip,
    std::size_t& active,
    std::size_t& disabled,
    std::size_t& enabledInSkip) const
{
    Overlay::PeerSequence ret;
    std::scoped_lock const lock(mutex_);

    active = ids_.size();
    disabled = enabledInSkip = 0;
    ret.reserve(ids_.size());

    // NOTE The purpose of p is to delay the destruction of PeerImp
    std::shared_ptr<PeerImp> p;
    for (auto& [id, w] : ids_)
    {
        if (p = w.lock(); p != nullptr)
        {
            bool const reduceRelayEnabled = p->txReduceRelayEnabled();
            if (!reduceRelayEnabled)
                ++disabled;

            if (!toSkip.contains(id))
            {
                ret.emplace_back(std::move(p));
            }
            else if (reduceRelayEnabled)
            {
                ++enabledInSkip;
            }
        }
    }

    return ret;
}

/** Notify all active peers of the current validated ledger sequence index.
 *
 *  Each peer compares `index` to its own tracked range to decide whether
 *  it should transition the `tracking_` state between `converged` and
 *  `diverged`.
 *
 *  @param index The latest fully-validated ledger sequence number.
 */
void
OverlayImpl::checkTracking(std::uint32_t index)
{
    forEach([index](std::shared_ptr<PeerImp> const& sp) { sp->checkTracking(index); });
}

/** Look up an active peer by its short numeric ID.
 *
 *  @param id The peer's short ID assigned at connection time.
 *  @return The peer if found and still alive, or `nullptr`.
 */
std::shared_ptr<Peer>
OverlayImpl::findPeerByShortID(Peer::id_t const& id) const
{
    std::scoped_lock const lock(mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

/** Look up an active peer by its node public key via linear scan.
 *
 *  A dedicated hash map was not used because the connect/disconnect overhead
 *  of maintaining it outweighs the cost of a linear search over the (small)
 *  active-peer set.
 *
 *  @param pubKey The node's Ed25519 or secp256k1 public key.
 *  @return The peer if found and still alive, or `nullptr`.
 */
std::shared_ptr<Peer>
OverlayImpl::findPeerByPublicKey(PublicKey const& pubKey)
{
    std::scoped_lock const lock(mutex_);
    // NOTE The purpose of peer is to delay the destruction of PeerImp
    std::shared_ptr<PeerImp> peer;
    for (auto const& e : ids_)
    {
        if (peer = e.second.lock(); peer != nullptr)
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

/** Send a consensus proposal to every active peer without deduplication.
 *
 *  @param m The proposal message to broadcast.
 */
void
OverlayImpl::broadcast(protocol::TMProposeSet& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    forEach([&](std::shared_ptr<PeerImp> const& p) { p->send(sm); });
}

/** Relay a consensus proposal, skipping peers that already have it.
 *
 *  Consults `HashRouter::shouldRelay` for deduplication.  Returns the set
 *  of peer IDs that already relayed this message (the skip set) so callers
 *  can track which peers to exclude in future rounds.  Returns an empty set
 *  if the message has already been relayed and should be suppressed.
 *
 *  @param m         The proposal message to relay.
 *  @param uid       Unique message hash used for hash-router lookup.
 *  @param validator Public key of the originating validator.
 *  @return Skip-set of peer IDs that already received this message, or empty
 *      if relay was suppressed.
 */
std::set<Peer::id_t>
OverlayImpl::relay(protocol::TMProposeSet& m, uint256 const& uid, PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER, validator);
        forEach([&](std::shared_ptr<PeerImp> const& p) {
            if (!toSkip->contains(p->id()))
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

/** Send a validation to every active peer without deduplication.
 *
 *  @param m The validation message to broadcast.
 */
void
OverlayImpl::broadcast(protocol::TMValidation& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    forEach([sm](std::shared_ptr<PeerImp> const& p) { p->send(sm); });
}

/** Relay a validation, skipping peers that already have it.
 *
 *  @param m         The validation message to relay.
 *  @param uid       Unique message hash used for hash-router deduplication.
 *  @param validator Public key of the originating validator.
 *  @return Skip-set of peer IDs that already received this message, or empty
 *      if relay was suppressed.
 */
std::set<Peer::id_t>
OverlayImpl::relay(protocol::TMValidation& m, uint256 const& uid, PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION, validator);
        forEach([&](std::shared_ptr<PeerImp> const& p) {
            if (!toSkip->contains(p->id()))
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

/** Return a lazily built, cached `TMManifests` protocol message.
 *
 *  Rebuilds the message only when the `ValidatorManifests` sequence number
 *  has advanced since the last call.  The message and its sequence number are
 *  guarded by `manifestLock_`.  Hash-router suppression entries are added for
 *  each manifest so they are not re-relayed by the caller.
 *
 *  @return The cached manifest message, or `nullptr` if there are no manifests.
 */
std::shared_ptr<Message>
OverlayImpl::getManifestsMessage()
{
    std::scoped_lock const g(manifestLock_);

    if (auto seq = app_.getValidatorManifests().sequence(); seq != manifestListSeq_)
    {
        protocol::TMManifests tm;

        app_.getValidatorManifests().forEachManifest(
            [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
            [&tm, &hr = app_.getHashRouter()](Manifest const& manifest) {
                tm.add_list()->set_stobject(manifest.serialized.data(), manifest.serialized.size());
                hr.addSuppression(manifest.hash());
            });

        manifestMessage_.reset();

        if (tm.list_size() != 0)
            manifestMessage_ = std::make_shared<Message>(tm, protocol::mtMANIFESTS);

        manifestListSeq_ = seq;
    }

    return manifestMessage_;
}

/** Relay a transaction (or its hash) to peers not in the skip set.
 *
 *  Pseudo-transactions are never relayed.  When TX reduce-relay is disabled
 *  the full message is sent to all peers outside `toSkip`.  When enabled and
 *  the peer count exceeds the reduce-relay threshold, a quota of peers
 *  is computed:
 *  @code
 *  enabledTarget = TX_REDUCE_RELAY_MIN_PEERS
 *                + (total - minRelay) * TX_RELAY_PERCENTAGE / 100
 *  @endcode
 *  Peers with the feature disabled always receive the full message for
 *  backward compatibility.  Peers above the quota receive only the hash via
 *  `addTxQueue()`.  The peer list is shuffled before selection to prevent
 *  systematic bias.
 *
 *  If `tx` is `nullopt`, the caller signals a hash-only announcement;
 *  the hash is queued on all reachable peers when reduce-relay is active,
 *  or the call is a no-op when it is not.
 *
 *  @param hash    SHA-256 transaction hash.
 *  @param tx      The transaction message, or `nullopt` for hash-only relay.
 *  @param toSkip  Peer IDs to exclude (already have the transaction).
 */
void
OverlayImpl::relay(
    uint256 const& hash,
    std::optional<std::reference_wrapper<protocol::TMTransaction>> tx,
    std::set<Peer::id_t> const& toSkip)
{
    bool relay = tx.has_value();
    if (relay)
    {
        auto& txn = tx->get();
        SerialIter sit(makeSlice(txn.rawtransaction()));
        try
        {
            relay = !isPseudoTx(STTx{sit});
        }
        catch (std::exception const&)
        {
            // Could not construct STTx, not relaying
            JLOG(journal_.debug()) << "Could not construct STTx: " << hash;
            return;
        }
    }

    Overlay::PeerSequence peers = {};
    std::size_t total = 0;
    std::size_t disabled = 0;
    std::size_t enabledInSkip = 0;

    if (!relay)
    {
        if (!app_.config().TX_REDUCE_RELAY_ENABLE)
            return;

        peers = getActivePeers(toSkip, total, disabled, enabledInSkip);
        JLOG(journal_.trace()) << "not relaying tx, total peers " << peers.size();
        for (auto const& p : peers)
            p->addTxQueue(hash);
        return;
    }

    auto& txn = tx->get();
    auto const sm = std::make_shared<Message>(txn, protocol::mtTRANSACTION);
    peers = getActivePeers(toSkip, total, disabled, enabledInSkip);
    auto const minRelay = app_.config().TX_REDUCE_RELAY_MIN_PEERS + disabled;

    if (!app_.config().TX_REDUCE_RELAY_ENABLE || total <= minRelay)
    {
        for (auto const& p : peers)
            p->send(sm);
        if (app_.config().TX_REDUCE_RELAY_ENABLE || app_.config().TX_REDUCE_RELAY_METRICS)
            txMetrics_.addMetrics(total, toSkip.size(), 0);
        return;
    }

    auto const enabledTarget = app_.config().TX_REDUCE_RELAY_MIN_PEERS +
        ((total - minRelay) * app_.config().TX_RELAY_PERCENTAGE / 100);

    txMetrics_.addMetrics(enabledTarget, toSkip.size(), disabled);

    if (enabledTarget > enabledInSkip)
        std::shuffle(peers.begin(), peers.end(), defaultPrng());

    JLOG(journal_.trace()) << "relaying tx, total peers " << peers.size() << " selected "
                           << enabledTarget << " skip " << toSkip.size() << " disabled "
                           << disabled;

    std::uint16_t enabledAndRelayed = enabledInSkip;
    for (auto const& p : peers)
    {
        if (!p->txReduceRelayEnabled())
        {
            p->send(sm);
        }
        else if (enabledAndRelayed < enabledTarget)
        {
            enabledAndRelayed++;
            p->send(sm);
        }
        else
        {
            p->addTxQueue(hash);
        }
    }
}

//------------------------------------------------------------------------------

/** Erase a child from the lifetime registry and signal `stop()` if empty.
 *
 *  Called from `Child::~Child`.  When the last child is removed, notifies
 *  `cond_` to wake the thread blocked in `stop()`.
 *
 *  @param child The child object being destroyed.
 */
void
OverlayImpl::remove(Child& child)
{
    std::scoped_lock const lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        cond_.notify_all();
}

/** Signal all registered children to stop and release the io_context work guard.
 *
 *  Must run on the overlay strand.  Children's `stop()` calls may re-enter
 *  `remove(Child&)` (and thus `list_.erase`) on the same thread, so all
 *  child pointers are snapshotted into a local vector before any `stop()` is
 *  invoked — iterating `list_` directly while modifying it is undefined.
 *  Resetting `work_` lets the `io_context` drain once the last async op
 *  completes.
 */
void
OverlayImpl::stopChildren()
{
    std::vector<std::shared_ptr<Child>> children;
    {
        std::scoped_lock const lock(mutex_);
        if (!work_)
            return;
        work_ = std::nullopt;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

/** Ask PeerFinder for new outbound connection targets and connect to them. */
void
OverlayImpl::autoConnect()
{
    auto const result = peerFinder_->autoconnect();
    for (auto const& addr : result)
        connect(addr);
}

/** Compute and dispatch peer endpoint advertisements via PeerFinder. */
void
OverlayImpl::sendEndpoints()
{
    auto const result = peerFinder_->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        std::shared_ptr<PeerImp> peer;
        {
            std::scoped_lock const lock(mutex_);
            auto const iter = peers_.find(e.first);
            if (iter != peers_.end())
                peer = iter->second.lock();
        }
        if (peer)
            peer->sendEndpoints(e.second.begin(), e.second.end());
    }
}

/** Flush per-peer TX hash queues to all peers that support reduce-relay.
 *
 *  Called once per second by `onTimer` when `TX_REDUCE_RELAY_ENABLE` is
 *  active.  Peers that did not negotiate the feature are skipped.
 */
void
OverlayImpl::sendTxQueue() const
{
    forEach([](auto const& p) {
        if (p->txReduceRelayEnabled())
            p->sendTxQueue();
    });
}

/** Build a `TMSquelch` protocol message.
 *
 *  @param validator      Public key of the validator whose messages should be
 *      squelched or unsquelched.
 *  @param squelch        `true` to squelch, `false` to unsquelch.
 *  @param squelchDuration Duration in seconds (only set when squelching).
 *  @return Shared `Message` ready to be sent over the wire.
 */
std::shared_ptr<Message>
makeSquelchMessage(PublicKey const& validator, bool squelch, uint32_t squelchDuration)
{
    protocol::TMSquelch m;
    m.set_squelch(squelch);
    m.set_validatorpubkey(validator.data(), validator.size());
    if (squelch)
        m.set_squelchduration(squelchDuration);
    return std::make_shared<Message>(m, protocol::mtSQUELCH);
}

/** Send a `TMSquelch` unsquelch message to the specified peer.
 *
 *  @param validator Public key of the validator to unsquelch.
 *  @param id        Short ID of the peer to notify.
 *  @note Multiple unsquelch messages for different validators may be batched
 *      to the same peer; each is sent individually as they arrive.
 */
void
OverlayImpl::unsquelch(PublicKey const& validator, Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id); peer)
        peer->send(makeSquelchMessage(validator, false, 0));
}

/** Send a `TMSquelch` squelch message to the specified peer.
 *
 *  @param validator       Public key of the validator to squelch.
 *  @param id              Short ID of the peer to notify.
 *  @param squelchDuration How long (seconds) the peer should suppress messages
 *      for this validator.
 */
void
OverlayImpl::squelch(PublicKey const& validator, Peer::id_t id, uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id); peer)
        peer->send(makeSquelchMessage(validator, true, squelchDuration));
}

/** Update squelch slots for a batch of peers and send `TMSquelch` as needed.
 *
 *  Dispatches to the overlay strand if called from another thread — `Slots`
 *  is not thread-safe.  Reference parameters (`key`, `validator`) are
 *  captured by value when posting to avoid dangling references.
 *  No-ops when `baseSquelchReady()` returns false (warmup period).
 *
 *  @param key       Unique message hash identifying the validator message.
 *  @param validator Validator whose per-peer message count is updated.
 *  @param peers     Set of peer IDs that received the message.
 *  @param type      Received protocol message type.
 */
void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    std::set<Peer::id_t>&& peers,
    protocol::MessageType type)
{
    if (!slots_.baseSquelchReady())
        return;

    if (!strand_.running_in_this_thread())
    {
        post(
            strand_,
            // Must capture copies of reference parameters (i.e. key, validator)
            [this, key = key, validator = validator, peers = std::move(peers), type]() mutable {
                updateSlotAndSquelch(key, validator, std::move(peers), type);
            });

        return;
    }

    for (auto id : peers)
    {
        slots_.updateSlotAndSquelch(key, validator, id, type, [&]() {
            reportInboundTraffic(TrafficCount::Category::SquelchIgnored, 0);
        });
    }
}

/** Single-peer overload of `updateSlotAndSquelch` to avoid set allocation.
 *
 *  Dispatches to the overlay strand if called from another thread.
 *  Reference parameters are captured by value in the posted lambda.
 *
 *  @param key       Unique message hash identifying the validator message.
 *  @param validator Validator whose per-peer message count is updated.
 *  @param peer      Peer ID that received the message.
 *  @param type      Received protocol message type.
 */
void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t peer,
    protocol::MessageType type)
{
    if (!slots_.baseSquelchReady())
        return;

    if (!strand_.running_in_this_thread())
    {
        post(
            strand_,
            // Must capture copies of reference parameters (i.e. key, validator)
            [this, key = key, validator = validator, peer, type]() {
                updateSlotAndSquelch(key, validator, peer, type);
            });
        return;
    }

    slots_.updateSlotAndSquelch(key, validator, peer, type, [&]() {
        reportInboundTraffic(TrafficCount::Category::SquelchIgnored, 0);
    });
}

/** Remove a peer's squelch slot, unsquelching peers if it was a selected source.
 *
 *  Dispatches to the overlay strand.  If the deleted peer was one of the
 *  selected sources for a validator, the squelched peers are unsquelched so
 *  they may resume forwarding that validator's messages.
 *
 *  @param id Short ID of the peer being removed.
 */
void
OverlayImpl::deletePeer(Peer::id_t id)
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&OverlayImpl::deletePeer, this, id));
        return;
    }

    slots_.deletePeer(id, true);
}

/** Purge squelch slots for peers that have gone idle.
 *
 *  Dispatches to the overlay strand if not already on it, ensuring
 *  thread-safe access to `slots_`.  Called every
 *  `Tuning::kCHECK_IDLE_PEERS` timer ticks.
 */
void
OverlayImpl::deleteIdlePeers()
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&OverlayImpl::deleteIdlePeers, this));
        return;
    }

    slots_.deleteIdlePeers();
}

//------------------------------------------------------------------------------

/** Parse config sections into an `Overlay::Setup` struct.
 *
 *  Reads `[overlay]`, `[crawl]`, `[vl]`, and `[network_id]` sections.
 *  Creates an SSL context and validates all fields:
 *  - `ip_limit`: must be non-negative.
 *  - `public_ip`: must be a valid, non-private IP address.
 *  - `[network_id]`: may be a decimal number or one of the symbolic names
 *    `main` (0), `testnet` (1), `devnet` (2).
 *
 *  @param config The application config to parse.
 *  @return Populated `Overlay::Setup` ready to pass to `makeOverlay`.
 *  @throws std::runtime_error on any invalid configuration value.
 */
Overlay::Setup
setupOverlay(BasicConfig const& config)
{
    Overlay::Setup setup;

    {
        auto const& section = config.section("overlay");
        setup.context = makeSslContext("");

        set(setup.ipLimit, "ip_limit", section);
        if (setup.ipLimit < 0)
            Throw<std::runtime_error>("Configured IP limit is invalid");

        std::string ip;
        set(ip, "public_ip", section);
        if (!ip.empty())
        {
            boost::system::error_code ec;
            setup.publicIp = boost::asio::ip::make_address(ip, ec);
            if (ec || beast::IP::isPrivate(setup.publicIp))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }
    }

    {
        auto const& section = config.section("crawl");
        auto const& values = section.values();

        if (values.size() > 1)
        {
            Throw<std::runtime_error>("Configured [crawl] section is invalid, too many values");
        }

        bool crawlEnabled = true;

        // Only allow "0|1" as a value
        if (values.size() == 1)
        {
            try
            {
                crawlEnabled = boost::lexical_cast<bool>(values.front());
            }
            catch (boost::bad_lexical_cast const&)
            {
                Throw<std::runtime_error>(
                    "Configured [crawl] section has invalid value: " + values.front());
            }
        }

        if (crawlEnabled)
        {
            if (get<bool>(section, "overlay", true))
            {
                setup.crawlOptions |= CrawlOptions::kOVERLAY;
            }
            if (get<bool>(section, "server", true))
            {
                setup.crawlOptions |= CrawlOptions::kSERVER_INFO;
            }
            if (get<bool>(section, "counts", false))
            {
                setup.crawlOptions |= CrawlOptions::kSERVER_COUNTS;
            }
            if (get<bool>(section, "unl", true))
            {
                setup.crawlOptions |= CrawlOptions::kUNL;
            }
        }
    }
    {
        auto const& section = config.section("vl");

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy("network_id");

        if (!id.empty())
        {
            if (id == "main")
                id = "0";

            if (id == "testnet")
                id = "1";

            if (id == "devnet")
                id = "2";

            setup.networkID = beast::lexicalCastThrow<std::uint32_t>(id);
        }
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            "Configured [network_id] section is invalid: must be a number "
            "or one of the strings 'main', 'testnet' or 'devnet'.");
    }

    return setup;
}

/** Factory function that constructs an `OverlayImpl` and returns it as
 *  `unique_ptr<Overlay>`, keeping the concrete type out of translation
 *  units that only need the `Overlay` interface.
 *
 *  @param app            The application instance.
 *  @param setup          Pre-parsed overlay configuration from `setupOverlay`.
 *  @param serverHandler  HTTP server handler for inbound upgrade requests.
 *  @param resourceManager Resource manager for connection rate limiting.
 *  @param resolver       Async DNS resolver used during bootstrap.
 *  @param ioContext       ASIO io_context that owns the overlay strand.
 *  @param config         Full application config (passed to PeerFinder).
 *  @param collector      Metrics collector for traffic gauges.
 *  @return Owning pointer to the newly constructed overlay.
 */
std::unique_ptr<Overlay>
makeOverlay(
    Application& app,
    Overlay::Setup const& setup,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_context& ioContext,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<OverlayImpl>(
        app, setup, serverHandler, resourceManager, resolver, ioContext, config, collector);
}

}  // namespace xrpl
