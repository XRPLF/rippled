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
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
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

namespace CrawlOptions {
static constexpr auto kDisabled = 0;
static constexpr auto kOverlay = (1 << 0);
static constexpr auto kServerInfo = (1 << 1);
static constexpr auto kServerCounts = (1 << 2);
static constexpr auto kUnl = (1 << 3);
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

OverlayImpl::Timer::Timer(OverlayImpl& overlay) : Child(overlay), timer(overlay_.ioContext_)
{
}

void
OverlayImpl::Timer::stop()
{
    // This method is only ever called from the same strand that calls
    // Timer::on_timer, ensuring they never execute concurrently.
    stopping = true;
    timer.cancel();
}

void
OverlayImpl::Timer::asyncWait()
{
    timer.expires_after(std::chrono::seconds(1));
    timer.async_wait(
        boost::asio::bind_executor(
            overlay_.strand_,
            std::bind(&Timer::onTimer, shared_from_this(), std::placeholders::_1)));
}

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
    if (overlay_.app_.config().txReduceRelayEnable)
        overlay_.sendTxQueue();

    if ((++overlay_.timerCount_ % Tuning::kCheckIdlePeers) == 0)
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
    , ioContext_(ioContext)
    , work_(std::in_place, boost::asio::make_work_guard(ioContext_))
    , strand_(boost::asio::make_strand(ioContext_))
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
    , nextId_(1)
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

Handoff
OverlayImpl::onHandoff(
    std::unique_ptr<stream_type>&& streamPtr,
    http_request_type&& request,
    endpoint_type remoteEndpoint)
{
    auto const id = nextId_++;
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
        // connection refused either IP limit exceeded or self-connect
        handoff.moved = false;
        JLOG(journal.debug()) << "Peer " << remoteEndpoint << " refused, " << to_string(result);
        return handoff;
    }

    // Validate HTTP request

    {
        auto const types = beast::rfc2616::splitCommas(request["Connect-As"]);
        if (std::ranges::find_if(types, [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response = makeRedirectResponse(slot, request, remoteEndpoint.address());
            handoff.keepAlive = beast::rfc2616::isKeepAlive(request);
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
        handoff.keepAlive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*streamPtr, journal);
    if (!sharedValue)
    {
        peerFinder_->onClosed(slot);
        handoff.moved = false;
        handoff.response =
            makeErrorResponse(slot, request, remoteEndpoint.address(), "Incorrect security cookie");
        handoff.keepAlive = false;
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
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
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
                handoff.keepAlive = false;
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
            // As we are not on the strand, run() must be called
            // while holding the lock, otherwise new I/O can be
            // queued after a call to stop().
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
        handoff.keepAlive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

bool
OverlayImpl::isPeerUpgrade(http_request_type const& request)
{
    if (!isUpgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

std::string
OverlayImpl::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

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

std::shared_ptr<Writer>
OverlayImpl::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remoteAddress,
    std::string const& text)
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
        ioContext_,
        beast::IPAddressConversion::toAsioEndpoint(remoteEndpoint),
        usage,
        setup_.context,
        nextId_++,
        slot,
        app_.getJournal("Peer"),
        *this);

    std::scoped_lock const lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

// Adds a peer that is already handshaked and active
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

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

void
OverlayImpl::remove(std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::scoped_lock const lock(mutex_);
    auto const iter = peers_.find(slot);
    XRPL_ASSERT(iter != peers_.end(), "xrpl::OverlayImpl::remove : valid input");
    peers_.erase(iter);
}

void
OverlayImpl::start()
{
    PeerFinder::Config const config = PeerFinder::Config::makeConfig(
        app_.config(),
        serverHandler_.setup().overlay.port(),
        app_.getValidationPublicKey().has_value(),
        setup_.ipLimit,
        setup_.verifyEndpoints);

    peerFinder_->setConfig(config);
    peerFinder_->start();

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps = app_.config().ips.empty() ? app_.config().ipsFixed : app_.config().ips;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.emplace_back("r.ripple.com 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.emplace_back("sahyadri.isrdc.in 51235");

        // Pool of servers operated by @Xrpkuwait - https://xrpkuwait.com
        bootstrapIps.emplace_back("hubs.xrpkuwait.com 51235");

        // Pool of servers operated by XRPL Commons - https://xrpl-commons.org
        bootstrapIps.emplace_back("hub.xrpl-commons.org 51235");
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
                    ips.push_back(to_string(addr.atPort(kDefaultPeerPort)));
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

    // Add the ips_fixed from the xrpld.cfg file
    if (!app_.config().standalone() && !app_.config().ipsFixed.empty())
    {
        resolver_.resolve(
            app_.config().ipsFixed,
            [this](std::string const& name, std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                    {
                        ips.emplace_back(addr.address(), kDefaultPeerPort);
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
/** A peer has connected successfully
    This is called after the peer handshake has been completed and during
    peer activation. At this point, the peer address and the public key
    are known.
*/
void
OverlayImpl::activate(std::shared_ptr<PeerImp> const& peer)
{
    beast::WrappedSink sink{journal_.sink(), peer->prefix()};
    beast::Journal const journal{sink};

    // Now track this peer
    {
        std::scoped_lock const lock(mutex_);
        auto const result(ids_.emplace(
            std::piecewise_construct, std::make_tuple(peer->id()), std::make_tuple(peer)));
        XRPL_ASSERT(result.second, "xrpl::OverlayImpl::activate : peer ID is inserted");
        (void)result.second;
    }

    JLOG(journal.debug()) << "activated";

    // We just accepted this peer so we have non-zero active peers
    XRPL_ASSERT(size(), "xrpl::OverlayImpl::activate : nonzero peers");
}

void
OverlayImpl::onPeerDeactivate(Peer::id_t id)
{
    std::scoped_lock const lock(mutex_);
    ids_.erase(id);
}

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
/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the XRPL protocol.
*/
std::size_t
OverlayImpl::size() const
{
    std::scoped_lock const lock(mutex_);
    return ids_.size();
}

int
OverlayImpl::limit()
{
    return peerFinder_->config().maxPeers;
}

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

json::Value
OverlayImpl::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    json::Value serverInfo = app_.getOPs().getServerInfo(humanReadable, admin, counters);

    // Filter out some information
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

json::Value
OverlayImpl::getServerCounts()
{
    return getCountsJson(app_, 10);
}

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

// Returns information on verified peers.
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
    if (req.target() != "/crawl" || setup_.crawlOptions == CrawlOptions::kDisabled)
        return false;

    boost::beast::http::response<JsonBody> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = json::Value(2u);

    if ((setup_.crawlOptions & CrawlOptions::kOverlay) != 0u)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if ((setup_.crawlOptions & CrawlOptions::kServerInfo) != 0u)
    {
        msg.body()["server"] = getServerInfo();
    }
    if ((setup_.crawlOptions & CrawlOptions::kServerCounts) != 0u)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if ((setup_.crawlOptions & CrawlOptions::kUnl) != 0u)
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
    // If the target is in the form "/vl/<validator_list_public_key>",
    // return the most recent validator list for that key.
    constexpr std::string_view kPrefix("/vl/");

    if (!req.target().starts_with(kPrefix) || !setup_.vlEnabled)
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

    std::string_view key = req.target().substr(kPrefix.size());

    if (auto slash = key.find('/'); slash != std::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    // find the list
    auto vl = app_.getValidators().getAvailable(key, version);

    if (!vl)
    {
        // 404 not found
        return fail(boost::beast::http::status::not_found);
    }
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
    // Take advantage of || short-circuiting
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}

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
            // tx reduced relay feature disabled
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

void
OverlayImpl::checkTracking(std::uint32_t index)
{
    forEach([index](std::shared_ptr<PeerImp> const& sp) { sp->checkTracking(index); });
}

std::shared_ptr<Peer>
OverlayImpl::findPeerByShortID(Peer::id_t const& id) const
{
    std::scoped_lock const lock(mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
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

void
OverlayImpl::broadcast(protocol::TMProposeSet const& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    forEach([&](std::shared_ptr<PeerImp> const& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(protocol::TMProposeSet const& m, uint256 const& uid, PublicKey const& validator)
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

void
OverlayImpl::broadcast(protocol::TMValidation const& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    forEach([sm](std::shared_ptr<PeerImp> const& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(protocol::TMValidation const& m, uint256 const& uid, PublicKey const& validator)
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
        if (!app_.config().txReduceRelayEnable)
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
    auto const minRelay = app_.config().txReduceRelayMinPeers + disabled;

    if (!app_.config().txReduceRelayEnable || total <= minRelay)
    {
        for (auto const& p : peers)
            p->send(sm);
        if (app_.config().txReduceRelayEnable || app_.config().txReduceRelayMetrics)
            txMetrics_.addMetrics(total, toSkip.size(), 0);
        return;
    }

    // We have more peers than the minimum (disabled + minimum enabled),
    // relay to all disabled and some randomly selected enabled that
    // do not have the transaction.
    auto const enabledTarget = app_.config().txReduceRelayMinPeers +
        ((total - minRelay) * app_.config().txRelayPercentage / 100);

    txMetrics_.addMetrics(enabledTarget, toSkip.size(), disabled);

    if (enabledTarget > enabledInSkip)
        std::shuffle(peers.begin(), peers.end(), defaultPrng());

    JLOG(journal_.trace()) << "relaying tx, total peers " << peers.size() << " selected "
                           << enabledTarget << " skip " << toSkip.size() << " disabled "
                           << disabled;

    // count skipped peers with the enabled feature towards the quota
    std::uint16_t enabledAndRelayed = enabledInSkip;
    for (auto const& p : peers)
    {
        // always relay to a peer with the disabled feature
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

void
OverlayImpl::remove(Child& child)
{
    std::scoped_lock const lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        cond_.notify_all();
}

void
OverlayImpl::stopChildren()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (OverlayImpl::remove() may be called on this same thread).  So
    // iterating directly over list_ to call child->stop() could lead to
    // undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees OverlayImpl::remove()
    // won't be called until vector<> children leaves scope.
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

void
OverlayImpl::autoConnect()
{
    auto const result = peerFinder_->autoconnect();
    for (auto const& addr : result)
        connect(addr);
}

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

void
OverlayImpl::sendTxQueue() const
{
    forEach([](auto const& p) {
        if (p->txReduceRelayEnabled())
            p->sendTxQueue();
    });
}

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

void
OverlayImpl::unsquelch(PublicKey const& validator, Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id); peer)
    {
        // optimize - multiple message with different
        // validator might be sent to the same peer
        peer->send(makeSquelchMessage(validator, false, 0));
    }
}

void
OverlayImpl::squelch(PublicKey const& validator, Peer::id_t id, uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id); peer)
    {
        peer->send(makeSquelchMessage(validator, true, squelchDuration));
    }
}

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
        {
            post(
                strand_,
                // Must capture copies of reference parameters (i.e. key, validator)
                [this, key = key, validator = validator, peer, type]() {
                    updateSlotAndSquelch(key, validator, peer, type);
                });
        }
        return;
    }

    slots_.updateSlotAndSquelch(key, validator, peer, type, [&]() {
        reportInboundTraffic(TrafficCount::Category::SquelchIgnored, 0);
    });
}

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

Overlay::Setup
setupOverlay(BasicConfig const& config, beast::Journal j)
{
    Overlay::Setup setup;

    {
        auto const& section = config.section(Sections::kOverlay);
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
            if (ec || !beast::IP::isPublic(setup.publicIp))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }

        set(setup.verifyEndpoints, true, "verify_endpoints", section);
        if (!setup.verifyEndpoints)
        {
            JLOG(j.warn()) << "Endpoint verification is disabled. This is a "
                              "security risk and should only be used for "
                              "testing.";
        }
    }

    {
        auto const& section = config.section(Sections::kCrawl);
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
            if (get<bool>(section, Keys::kOverlay, true))
            {
                setup.crawlOptions |= CrawlOptions::kOverlay;
            }
            if (get<bool>(section, Keys::kServer, true))
            {
                setup.crawlOptions |= CrawlOptions::kServerInfo;
            }
            if (get<bool>(section, Keys::kCounts, false))
            {
                setup.crawlOptions |= CrawlOptions::kServerCounts;
            }
            if (get<bool>(section, Keys::kUnl, true))
            {
                setup.crawlOptions |= CrawlOptions::kUnl;
            }
        }
    }
    {
        auto const& section = config.section(Sections::kVl);

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy(Sections::kNetworkId);

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
