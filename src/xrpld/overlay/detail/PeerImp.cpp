#include <xrpld/overlay/detail/PeerImp.h>

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/consensus/Validations.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/ClusterNode.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/TrafficCount.h>
#include <xrpld/overlay/detail/Tuning.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/ToString.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Disposition.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/Gossip.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/tx/apply.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/ostream.hpp>
#include <boost/beast/core/stream_traits.hpp>

#include <google/protobuf/message.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace xrpl {

namespace {
/** The threshold above which we treat a peer connection as high latency */
constexpr std::chrono::milliseconds kPeerHighLatency{300};

/** How often we PING the peer to check for latency and sendq probe */
constexpr std::chrono::seconds kPeerTimerInterval{60};

/** The timeout for a shutdown timer */
constexpr std::chrono::seconds kShutdownTimerInterval{5};

}  // namespace

// TODO: Remove this exclusion once unit tests are added after the hotfix
// release.

PeerImp::PeerImp(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& streamPtr,
    OverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , fingerprint_(getFingerprint(slot->remoteEndpoint(), publicKey, to_string(id)))
    , prefix_(makePrefix(fingerprint_))
    , sink_(app_.getJournal("Peer"), prefix_)
    , pSink_(app_.getJournal("Protocol"), prefix_)
    , journal_(sink_)
    , pJournal_(pSink_)
    , streamPtr_(std::move(streamPtr))
    , socket_(streamPtr_->next_layer().socket())
    , stream_(*streamPtr_)
    , strand_(boost::asio::make_strand(socket_.get_executor()))
    , timer_(waitable_timer{socket_.get_executor()})
    , remoteAddress_(slot->remoteEndpoint())
    , overlay_(overlay)
    , inbound_(true)
    , protocol_(std::move(protocol))
    , tracking_(Tracking::Unknown)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.getJournal("Squelch"))
    , usage_(consumer)
    , fee_{.fee = Resource::kFeeTrivialPeer, .context = ""}
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, kFeatureCompr, "lz4", app_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
    , txReduceRelayEnabled_(
          peerFeatureEnabled(headers_, kFeatureTxrr, app_.config().TX_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(
          peerFeatureEnabled(headers_, kFeatureLedgerReplay, app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.info()) << "compression enabled " << (compressionEnabled_ == Compressed::On)
                          << " vp reduce-relay base squelch enabled "
                          << peerFeatureEnabled(
                                 headers_,
                                 kFeatureVprr,
                                 app_.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
                          << " tx reduce-relay enabled " << txReduceRelayEnabled_;
}

PeerImp::~PeerImp()
{
    bool const inCluster{cluster()};

    overlay_.deletePeer(id_);
    overlay_.onPeerDeactivate(id_);
    overlay_.peerFinder().onClosed(slot_);
    overlay_.remove(slot_);

    if (inCluster)
    {
        JLOG(journal_.warn()) << name() << " left cluster";
    }
}

// Helper function to check for valid uint256 values in protobuf buffers
static bool
stringIsUInt256Sized(std::string const& pBuffStr)
{
    return pBuffStr.size() == uint256::size();
}

void
PeerImp::run()
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::run, shared_from_this()));
        return;
    }

    auto parseLedgerHash = [](std::string_view value) -> std::optional<uint256> {
        if (uint256 ret; ret.parseHex(value))
            return ret;

        if (auto const s = base64Decode(value); s.size() == uint256::size())
            return uint256::fromRaw(s);

        return std::nullopt;
    };

    std::optional<uint256> closed;
    std::optional<uint256> previous;

    if (auto const iter = headers_.find("Closed-Ledger"); iter != headers_.end())
    {
        closed = parseLedgerHash(iter->value());

        if (!closed)
            fail("Malformed handshake data (1)");
    }

    if (auto const iter = headers_.find("Previous-Ledger"); iter != headers_.end())
    {
        previous = parseLedgerHash(iter->value());

        if (!previous)
            fail("Malformed handshake data (2)");
    }

    if (previous && !closed)
        fail("Malformed handshake data (3)");

    {
        std::scoped_lock const sl(recentLock_);
        if (closed)
            closedLedgerHash_ = *closed;
        if (previous)
            previousLedgerHash_ = *previous;
    }

    if (inbound_)
    {
        doAccept();
    }
    else
    {
        doProtocolStart();
    }

    // Anything else that needs to be done with the connection should be
    // done in doProtocolStart
}

void
PeerImp::stop()
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::stop, shared_from_this()));
        return;
    }

    if (!socket_.is_open())
        return;

    // The rationale for using different severity levels is that
    // outbound connections are under our control and may be logged
    // at a higher level, but inbound connections are more numerous and
    // uncontrolled so to prevent log flooding the severity is reduced.
    JLOG(journal_.debug()) << "stop: Stop";

    shutdown();
}

//------------------------------------------------------------------------------

void
PeerImp::send(std::shared_ptr<Message> const& m)
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::send, shared_from_this(), m));
        return;
    }

    if (!socket_.is_open())
        return;

    // we are in progress of closing the connection
    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    auto validator = m->getValidatorKey();
    if (validator && !squelch_.expireSquelch(*validator))
    {
        overlay_.reportOutboundTraffic(
            TrafficCount::Category::SquelchSuppressed,
            static_cast<int>(m->getBuffer(compressionEnabled_).size()));
        return;
    }

    // report categorized outgoing traffic
    overlay_.reportOutboundTraffic(
        safeCast<TrafficCount::Category>(m->getCategory()),
        static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    // report total outgoing traffic
    overlay_.reportOutboundTraffic(
        TrafficCount::Category::Total, static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    auto sendqSize = sendQueue_.size();

    if (sendqSize < Tuning::kTargetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        largeSendq_ = 0;
    }
    else if (auto sink = journal_.debug(); sink && (sendqSize % Tuning::kSendQueueLogFreq) == 0)
    {
        std::string const n = name();
        sink << n << " sendq: " << sendqSize;
    }

    sendQueue_.push(m);

    if (sendqSize != 0)
        return;

    writePending_ = true;
    boost::asio::async_write(
        stream_,
        boost::asio::buffer(sendQueue_.front()->getBuffer(compressionEnabled_)),
        bind_executor(
            strand_,
            std::bind(
                &PeerImp::onWriteMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

void
PeerImp::sendTxQueue()
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::sendTxQueue, shared_from_this()));
        return;
    }

    if (!txQueue_.empty())
    {
        protocol::TMHaveTransactions ht;
        std::ranges::for_each(
            txQueue_, [&](auto const& hash) { ht.add_hashes(hash.data(), hash.size()); });
        JLOG(pJournal_.trace()) << "sendTxQueue " << txQueue_.size();
        txQueue_.clear();
        send(std::make_shared<Message>(ht, protocol::mtHAVE_TRANSACTIONS));
    }
}

void
PeerImp::addTxQueue(uint256 const& hash)
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::addTxQueue, shared_from_this(), hash));
        return;
    }

    if (txQueue_.size() == reduce_relay::kMaxTxQueueSize)
    {
        JLOG(pJournal_.warn()) << "addTxQueue exceeds the cap";
        sendTxQueue();
    }

    txQueue_.insert(hash);
    JLOG(pJournal_.trace()) << "addTxQueue " << txQueue_.size();
}

void
PeerImp::removeTxQueue(uint256 const& hash)
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::removeTxQueue, shared_from_this(), hash));
        return;
    }

    auto removed = txQueue_.erase(hash);
    JLOG(pJournal_.trace()) << "removeTxQueue " << removed;
}

void
PeerImp::charge(Resource::Charge const& fee, std::string const& context)
{
    if ((usage_.charge(fee, context) == Resource::Disposition::Drop) &&
        usage_.disconnect(pJournal_) && strand_.running_in_this_thread())
    {
        // Sever the connection
        overlay_.incPeerDisconnectCharges();
        fail("charge: Resources");
    }
}

//------------------------------------------------------------------------------

bool
PeerImp::crawl() const
{
    auto const iter = headers_.find("Crawl");
    if (iter == headers_.end())
        return false;
    return boost::iequals(iter->value(), "public");
}

bool
PeerImp::cluster() const
{
    return static_cast<bool>(app_.getCluster().member(publicKey_));
}

std::string
PeerImp::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"];
    return headers_["Server"];
}

json::Value
PeerImp::json()
{
    json::Value ret(json::ValueType::Object);

    ret[jss::public_key] = toBase58(TokenType::NodePublic, publicKey_);
    ret[jss::address] = remoteAddress_.toString();

    if (inbound_)
        ret[jss::inbound] = true;

    if (cluster())
    {
        ret[jss::cluster] = true;

        if (auto const n = name(); !n.empty())
        {
            // Could move here if json::Value supported moving from a string
            ret[jss::name] = n;
        }
    }

    if (auto const d = domain(); !d.empty())
        ret[jss::server_domain] = std::string{d};

    if (auto const nid = headers_["Network-ID"]; !nid.empty())
        ret[jss::network_id] = std::string{nid};

    ret[jss::load] = usage_.balance();

    if (auto const version = getVersion(); !version.empty())
        ret[jss::version] = std::string{version};

    ret[jss::protocol] = to_string(protocol_);

    {
        std::scoped_lock const sl(recentLock_);
        if (latency_)
            ret[jss::latency] = static_cast<json::UInt>(latency_->count());
    }

    ret[jss::uptime] =
        static_cast<json::UInt>(std::chrono::duration_cast<std::chrono::seconds>(uptime()).count());

    std::uint32_t minSeq = 0, maxSeq = 0;
    ledgerRange(minSeq, maxSeq);

    if ((minSeq != 0) || (maxSeq != 0))
        ret[jss::complete_ledgers] = std::to_string(minSeq) + " - " + std::to_string(maxSeq);

    switch (tracking_.load())
    {
        case Tracking::Diverged:
            ret[jss::track] = "diverged";
            break;

        case Tracking::Unknown:
            ret[jss::track] = "unknown";
            break;

        case Tracking::Converged:
            // Nothing to do here
            break;
    }

    uint256 closedLedgerHash;
    protocol::TMStatusChange lastStatus;
    {
        std::scoped_lock const sl(recentLock_);
        closedLedgerHash = closedLedgerHash_;
        lastStatus = lastStatus_;
    }

    if (closedLedgerHash != beast::kZero)
        ret[jss::ledger] = to_string(closedLedgerHash);

    if (lastStatus.has_newstatus())
    {
        switch (lastStatus.newstatus())
        {
            case protocol::nsCONNECTING:
                ret[jss::status] = "connecting";
                break;

            case protocol::nsCONNECTED:
                ret[jss::status] = "connected";
                break;

            case protocol::nsMONITORING:
                ret[jss::status] = "monitoring";
                break;

            case protocol::nsVALIDATING:
                ret[jss::status] = "validating";
                break;

            case protocol::nsSHUTTING:
                ret[jss::status] = "shutting";
                break;

            default:
                JLOG(pJournal_.warn()) << "Unknown status: " << lastStatus.newstatus();
        }
    }

    ret[jss::metrics] = json::Value(json::ValueType::Object);
    ret[jss::metrics][jss::total_bytes_recv] = std::to_string(metrics_.recv.totalBytes());
    ret[jss::metrics][jss::total_bytes_sent] = std::to_string(metrics_.sent.totalBytes());
    ret[jss::metrics][jss::avg_bps_recv] = std::to_string(metrics_.recv.averageBytes());
    ret[jss::metrics][jss::avg_bps_sent] = std::to_string(metrics_.sent.averageBytes());

    return ret;
}

bool
PeerImp::supportsFeature(ProtocolFeature f) const
{
    switch (f)
    {
        case ProtocolFeature::ValidatorListPropagation:
            return protocol_ >= makeProtocol(2, 1);
        case ProtocolFeature::ValidatorList2Propagation:
            return protocol_ >= makeProtocol(2, 2);
        case ProtocolFeature::LedgerReplay:
            return ledgerReplayEnabled_;
    }
    return false;
}

//------------------------------------------------------------------------------

bool
PeerImp::hasLedger(uint256 const& hash, std::uint32_t seq) const
{
    {
        std::scoped_lock const sl(recentLock_);
        if ((seq != 0) && (seq >= minLedger_) && (seq <= maxLedger_) &&
            (tracking_.load() == Tracking::Converged))
            return true;
        if (std::ranges::find(recentLedgers_, hash) != recentLedgers_.end())
            return true;
    }
    return false;
}

void
PeerImp::ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const
{
    std::scoped_lock const sl(recentLock_);

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

bool
PeerImp::hasTxSet(uint256 const& hash) const
{
    std::scoped_lock const sl(recentLock_);
    return std::ranges::find(recentTxSets_, hash) != recentTxSets_.end();
}

void
PeerImp::cycleStatus()
{
    // Operations on closedLedgerHash_ and previousLedgerHash_ must be
    // guarded by recentLock_.
    std::scoped_lock const sl(recentLock_);
    previousLedgerHash_ = closedLedgerHash_;
    closedLedgerHash_.zero();
}

bool
PeerImp::hasRange(std::uint32_t uMin, std::uint32_t uMax)
{
    std::scoped_lock const sl(recentLock_);
    return (tracking_ != Tracking::Diverged) && (uMin >= minLedger_) && (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

void
PeerImp::fail(std::string const& name, error_code ec)
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::PeerImp::fail : strand in this thread");

    if (!socket_.is_open())
        return;

    JLOG(journal_.warn()) << name << ": " << ec.message();

    shutdown();
}

void
PeerImp::fail(std::string const& reason)
{
    if (!strand_.running_in_this_thread())
    {
        post(
            strand_,
            std::bind(
                (void (Peer::*)(std::string const&))&PeerImp::fail, shared_from_this(), reason));
        return;
    }

    if (!socket_.is_open())
        return;

    // Call to name() locks, log only if the message will be outputted
    if (journal_.active(beast::Severity::Warning))
    {
        std::string const n = name();
        JLOG(journal_.warn()) << n << " failed: " << reason;
    }

    shutdown();
}

void
PeerImp::tryAsyncShutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "xrpl::PeerImp::tryAsyncShutdown : strand in this thread");

    if (!shutdown_ || shutdownStarted_)
        return;

    if (readPending_ || writePending_)
        return;

    shutdownStarted_ = true;

    setTimer(kShutdownTimerInterval);

    // gracefully shutdown the SSL socket, performing a shutdown handshake
    stream_.async_shutdown(bind_executor(
        strand_, std::bind(&PeerImp::onShutdown, shared_from_this(), std::placeholders::_1)));
}

void
PeerImp::shutdown()
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::PeerImp::shutdown: strand in this thread");

    if (!socket_.is_open() || shutdown_)
        return;

    shutdown_ = true;

    boost::beast::get_lowest_layer(stream_).cancel();

    tryAsyncShutdown();
}

void
PeerImp::onShutdown(error_code ec)
{
    cancelTimer();
    if (ec)
    {
        // - eof: the stream was cleanly closed
        // - operation_aborted: an expired timer (slow shutdown)
        // - stream_truncated: the tcp connection closed (no handshake) it could
        // occur if a peer does not perform a graceful disconnect
        // - broken_pipe: the peer is gone
        bool const shouldLog =
            (ec != boost::asio::error::eof && ec != boost::asio::error::operation_aborted &&
             ec.message().find("application data after close notify") == std::string::npos);

        if (shouldLog)
        {
            JLOG(journal_.debug()) << "onShutdown: " << ec.message();
        }
    }

    close();
}

void
PeerImp::close()
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::PeerImp::close : strand in this thread");

    if (!socket_.is_open())
        return;

    cancelTimer();

    error_code ec;
    socket_.close(ec);  // NOLINT(bugprone-unused-return-value)

    overlay_.incPeerDisconnect();

    // The rationale for using different severity levels is that
    // outbound connections are under our control and may be logged
    // at a higher level, but inbound connections are more numerous and
    // uncontrolled so to prevent log flooding the severity is reduced.
    JLOG((inbound_ ? journal_.debug() : journal_.info())) << "close: Closed";
}

//------------------------------------------------------------------------------

void
PeerImp::setTimer(std::chrono::seconds interval)
{
    try
    {
        timer_.expires_after(interval);
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "setTimer: " << ex.what();
        shutdown();
        return;
    }

    timer_.async_wait(bind_executor(
        strand_, std::bind(&PeerImp::onTimer, shared_from_this(), std::placeholders::_1)));
}

//------------------------------------------------------------------------------

std::string
PeerImp::makePrefix(std::string const& fingerprint)
{
    std::stringstream ss;
    ss << "[" << fingerprint << "] ";
    return ss.str();
}

void
PeerImp::onTimer(error_code const& ec)
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::PeerImp::onTimer : strand in this thread");

    if (!socket_.is_open())
        return;

    if (ec)
    {
        // do not initiate shutdown, timers are frequently cancelled
        if (ec == boost::asio::error::operation_aborted)
            return;

        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        close();
        return;
    }

    // the timer expired before the shutdown completed
    // force close the connection
    if (shutdown_)
    {
        JLOG(journal_.debug()) << "onTimer: shutdown timer expired";
        close();
        return;
    }

    if (largeSendq_++ >= Tuning::kSendqIntervals)
    {
        fail("Large send queue");
        return;
    }

    if (auto const t = tracking_.load(); !inbound_ && t != Tracking::Converged)
    {
        clock_type::duration duration;

        {
            std::scoped_lock const sl(recentLock_);
            duration = clock_type::now() - trackingTime_;
        }

        if ((t == Tracking::Diverged && (duration > app_.config().MAX_DIVERGED_TIME)) ||
            (t == Tracking::Unknown && (duration > app_.config().MAX_UNKNOWN_TIME)))
        {
            overlay_.peerFinder().onFailure(slot_);
            fail("Not useful");
            return;
        }
    }

    // Already waiting for PONG
    if (lastPingSeq_)
    {
        fail("Ping Timeout");
        return;
    }

    lastPingTime_ = clock_type::now();
    lastPingSeq_ = randInt<std::uint32_t>();

    protocol::TMPing message;
    message.set_type(protocol::TMPing::ptPING);
    message.set_seq(*lastPingSeq_);

    send(std::make_shared<Message>(message, protocol::mtPING));

    setTimer(kPeerTimerInterval);
}

void
PeerImp::cancelTimer() noexcept
{
    try
    {
        timer_.cancel();
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "cancelTimer: " << ex.what();
    }
}

//------------------------------------------------------------------------------
void
PeerImp::doAccept()
{
    XRPL_ASSERT(readBuffer_.size() == 0, "xrpl::PeerImp::doAccept : empty read buffer");

    JLOG(journal_.debug()) << "doAccept";

    // a shutdown was initiated before the handshake, there is nothing to do
    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
    {
        fail("makeSharedValue: Unexpected failure");
        return;
    }

    JLOG(journal_.debug()) << "Protocol: " << to_string(protocol_);

    if (auto member = app_.getCluster().member(publicKey_))
    {
        {
            std::unique_lock const lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    overlay_.activate(shared_from_this());

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto writeBuffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*writeBuffer) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().publicIp,
        remoteAddress_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocol_,
        app_);

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        writeBuffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, writeBuffer, self = shared_from_this()](
                error_code ec, std::size_t bytesTransferred) {
                if (!socket_.is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                {
                    tryAsyncShutdown();
                    return;
                }
                if (ec)
                {
                    fail("onWriteResponse", ec);
                    return;
                }
                if (writeBuffer->size() == bytesTransferred)
                {
                    doProtocolStart();
                    return;
                }
                fail("Failed to write header");
                return;
            }));
}

std::string
PeerImp::name() const
{
    std::shared_lock const readLock{nameMutex_};
    return name_;
}

std::string
PeerImp::domain() const
{
    return headers_["Server-Domain"];
}

//------------------------------------------------------------------------------

// Protocol logic

void
PeerImp::doProtocolStart()
{
    // a shutdown was initiated before the handshare, there is nothing to do
    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    onReadMessage(error_code(), 0);

    // Send all the validator lists that have been loaded
    if (inbound_ && supportsFeature(ProtocolFeature::ValidatorListPropagation))
    {
        app_.getValidators().forEachAvailable(
            [&](std::string const& manifest,
                std::uint32_t version,
                std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
                PublicKey const& pubKey,
                std::size_t maxSequence,
                uint256 const& hash) {
                ValidatorList::sendValidatorList(
                    *this,
                    0,
                    pubKey,
                    maxSequence,
                    version,
                    manifest,
                    blobInfos,
                    app_.getHashRouter(),
                    pJournal_);

                // Don't send it next time.
                app_.getHashRouter().addSuppressionPeer(hash, id_);
            });
    }

    if (auto m = overlay_.getManifestsMessage())
        send(m);

    setTimer(kPeerTimerInterval);
}

// Called repeatedly with protocol message data
void
PeerImp::onReadMessage(error_code ec, std::size_t bytesTransferred)
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(), "xrpl::PeerImp::onReadMessage : strand in this thread");

    readPending_ = false;

    if (!socket_.is_open())
        return;

    if (ec)
    {
        if (ec == boost::asio::error::eof)
        {
            JLOG(journal_.debug()) << "EOF";
            shutdown();
            return;
        }

        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onReadMessage", ec);
        return;
    }
    // we started shutdown, no reason to process further data
    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    if (auto stream = journal_.trace())
    {
        stream << "onReadMessage: "
               << (bytesTransferred > 0 ? to_string(bytesTransferred) + " bytes" : "");
    }

    metrics_.recv.addMessage(bytesTransferred);

    readBuffer_.commit(bytesTransferred);

    auto hint = Tuning::kReadBufferBytes;

    while (readBuffer_.size() > 0)
    {
        std::size_t bytesConsumed = 0;

        using namespace std::chrono_literals;
        std::tie(bytesConsumed, ec) = perf::measureDurationAndLog(
            [&]() { return invokeProtocolMessage(readBuffer_.data(), *this, hint); },
            "invokeProtocolMessage",
            350ms,
            journal_);

        if (!socket_.is_open())
            return;

        // the error_code is produced by invokeProtocolMessage
        // it could be due to a bad message
        if (ec)
        {
            fail("onReadMessage", ec);
            return;
        }

        if (bytesConsumed == 0)
            break;

        readBuffer_.consume(bytesConsumed);
    }

    // check if a shutdown was initiated while processing messages
    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    readPending_ = true;

    XRPL_ASSERT(!shutdownStarted_, "xrpl::PeerImp::onReadMessage : shutdown started");

    // Timeout on writes only
    stream_.async_read_some(
        readBuffer_.prepare(std::max(Tuning::kReadBufferBytes, hint)),
        bind_executor(
            strand_,
            std::bind(
                &PeerImp::onReadMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

void
PeerImp::onWriteMessage(error_code ec, std::size_t bytesTransferred)
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(), "xrpl::PeerImp::onWriteMessage : strand in this thread");

    writePending_ = false;

    if (!socket_.is_open())
        return;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onWriteMessage", ec);
        return;
    }

    if (auto stream = journal_.trace())
    {
        stream << "onWriteMessage: "
               << (bytesTransferred > 0 ? to_string(bytesTransferred) + " bytes" : "");
    }

    metrics_.sent.addMessage(bytesTransferred);

    XRPL_ASSERT(!sendQueue_.empty(), "xrpl::PeerImp::onWriteMessage : non-empty send buffer");
    sendQueue_.pop();

    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    if (!sendQueue_.empty())
    {
        writePending_ = true;
        XRPL_ASSERT(!shutdownStarted_, "xrpl::PeerImp::onWriteMessage : shutdown started");

        // Timeout on writes only
        boost::asio::async_write(
            stream_,
            boost::asio::buffer(sendQueue_.front()->getBuffer(compressionEnabled_)),
            bind_executor(
                strand_,
                std::bind(
                    &PeerImp::onWriteMessage,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
        return;
    }
}

//------------------------------------------------------------------------------
//
// ProtocolHandler
//
//------------------------------------------------------------------------------

void
PeerImp::onMessageUnknown(std::uint16_t type)
{
    // TODO
}

void
PeerImp::onMessageBegin(
    std::uint16_t type,
    std::shared_ptr<::google::protobuf::Message> const& m,
    std::size_t size,
    std::size_t uncompressedSize,
    bool isCompressed)
{
    auto const name = protocolMessageName(type);
    loadEvent_ = app_.getJobQueue().makeLoadEvent(JtPeer, name);
    fee_ = {.fee = Resource::kFeeTrivialPeer, .context = name};

    auto const category =
        TrafficCount::categorize(*m, static_cast<protocol::MessageType>(type), true);

    // report total incoming traffic
    overlay_.reportInboundTraffic(TrafficCount::Category::Total, static_cast<int>(size));

    // increase the traffic received for a specific category
    overlay_.reportInboundTraffic(category, static_cast<int>(size));

    using namespace protocol;
    if ((type == MessageType::mtTRANSACTION || type == MessageType::mtHAVE_TRANSACTIONS ||
         type == MessageType::mtTRANSACTIONS ||
         // GET_OBJECTS
         category == TrafficCount::Category::GetTransactions ||
         // GET_LEDGER
         category == TrafficCount::Category::LdTscGet ||
         category == TrafficCount::Category::LdTscShare ||
         // LEDGER_DATA
         category == TrafficCount::Category::GlTscShare ||
         category == TrafficCount::Category::GlTscGet) &&
        (txReduceRelayEnabled() || app_.config().TX_REDUCE_RELAY_METRICS))
    {
        overlay_.addTxMetrics(static_cast<MessageType>(type), static_cast<std::uint64_t>(size));
    }
    JLOG(journal_.trace()) << "onMessageBegin: " << type << " " << size << " " << uncompressedSize
                           << " " << isCompressed;
}

void
PeerImp::onMessageEnd(std::uint16_t, std::shared_ptr<::google::protobuf::Message> const&)
{
    loadEvent_.reset();
    charge(fee_.fee, fee_.context);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMManifests> const& m)
{
    auto const s = m->list_size();

    if (s == 0)
    {
        fee_.update(Resource::kFeeUselessData, "empty");
        return;
    }

    if (s > 100)
        fee_.update(Resource::kFeeModerateBurdenPeer, "oversize");

    app_.getJobQueue().addJob(JtManifest, "RcvManifests", [this, that = shared_from_this(), m]() {
        overlay_.onManifests(m, that);
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPing> const& m)
{
    if (m->type() == protocol::TMPing::ptPING)
    {
        // We have received a ping request, reply with a pong
        fee_.update(Resource::kFeeModerateBurdenPeer, "ping request");
        m->set_type(protocol::TMPing::ptPONG);
        send(std::make_shared<Message>(*m, protocol::mtPING));
        return;
    }

    if (m->type() == protocol::TMPing::ptPONG && m->has_seq())
    {
        // Only reset the ping sequence if we actually received a
        // PONG with the correct cookie. That way, any peers which
        // respond with incorrect cookies will eventually time out.
        if (m->seq() == lastPingSeq_)
        {
            lastPingSeq_.reset();

            // Update latency estimate
            auto const rtt =
                std::chrono::round<std::chrono::milliseconds>(clock_type::now() - lastPingTime_);

            std::scoped_lock const sl(recentLock_);

            if (latency_)
            {
                latency_ = (*latency_ * 7 + rtt) / 8;
            }
            else
            {
                latency_ = rtt;
            }
        }

        return;
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMCluster> const& m)
{
    // VFALCO NOTE I think we should drop the peer immediately
    if (!cluster())
    {
        fee_.update(Resource::kFeeUselessData, "unknown cluster");
        return;
    }

    for (int i = 0; i < m->clusternodes().size(); ++i)
    {
        protocol::TMClusterNode const& node = m->clusternodes(i);

        std::string name;
        if (node.has_nodename())
            name = node.nodename();

        auto const publicKey = parseBase58<PublicKey>(TokenType::NodePublic, node.publickey());

        // NIKB NOTE We should drop the peer immediately if
        // they send us a public key we can't parse
        if (publicKey)
        {
            auto const reportTime = NetClock::time_point{NetClock::duration{node.reporttime()}};

            app_.getCluster().update(*publicKey, name, node.nodeload(), reportTime);
        }
    }

    int const loadSources = m->loadsources().size();
    if (loadSources != 0)
    {
        Resource::Gossip gossip;
        gossip.items.reserve(loadSources);
        for (int i = 0; i < m->loadsources().size(); ++i)
        {
            protocol::TMLoadSource const& node = m->loadsources(i);
            Resource::Gossip::Item item;
            item.address = beast::IP::Endpoint::fromString(node.name());
            item.balance = node.cost();
            if (item.address != beast::IP::Endpoint())
                gossip.items.push_back(item);
        }
        overlay_.resourceManager().importConsumers(name(), gossip);
    }

    // Calculate the cluster fee:
    auto const thresh = app_.getTimeKeeper().now() - 90s;
    std::uint32_t clusterFee = 0;

    std::vector<std::uint32_t> fees;
    fees.reserve(app_.getCluster().size());

    app_.getCluster().forEach([&fees, thresh](ClusterNode const& status) {
        if (status.getReportTime() >= thresh)
            fees.push_back(status.getLoadFee());
    });

    if (!fees.empty())
    {
        auto const index = fees.size() / 2;
        std::nth_element(fees.begin(), fees.begin() + index, fees.end());
        clusterFee = fees[index];
    }

    app_.getFeeTrack().setClusterFee(clusterFee);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMEndpoints> const& m)
{
    // Don't allow endpoints from peers that are not known tracking or are
    // not using a version of the message that we support:
    if (tracking_.load() != Tracking::Converged || m->version() != 2)
        return;

    // The number is arbitrary and doesn't have any real significance or
    // implication for the protocol.
    if (m->endpoints_v2().size() >= 1024)
    {
        fee_.update(Resource::kFeeUselessData, "endpoints too large");
        return;
    }

    std::vector<PeerFinder::Endpoint> endpoints;
    endpoints.reserve(m->endpoints_v2().size());

    auto malformed = 0;
    for (auto const& tm : m->endpoints_v2())
    {
        auto result = beast::IP::Endpoint::fromStringChecked(tm.endpoint());

        if (!result)
        {
            JLOG(pJournal_.error())
                << "failed to parse incoming endpoint: {" << tm.endpoint() << "}";
            malformed++;
            continue;
        }

        // If hops == 0, this Endpoint describes the peer we are connected
        // to -- in that case, we take the remote address seen on the
        // socket and store that in the IP::Endpoint. If this is the first
        // time, then we'll verify that their listener can receive incoming
        // by performing a connectivity test.  if hops > 0, then we just
        // take the address/port we were given
        if (tm.hops() == 0)
            result = remoteAddress_.atPort(result->port());

        endpoints.emplace_back(*result, tm.hops());
    }

    // Charge the peer for each malformed endpoint. As there still may be
    // multiple valid endpoints we don't return early.
    if (malformed > 0)
    {
        fee_.update(
            Resource::kFeeInvalidData * malformed,
            std::to_string(malformed) + " malformed endpoints");
    }

    if (!endpoints.empty())
        overlay_.peerFinder().onEndpoints(slot_, endpoints);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransaction> const& m)
{
    handleTransaction(m, true, false);
}

void
PeerImp::handleTransaction(
    std::shared_ptr<protocol::TMTransaction> const& m,
    bool eraseTxQueue,
    bool batch)
{
    XRPL_ASSERT(eraseTxQueue != batch, ("xrpl::PeerImp::handleTransaction : valid inputs"));
    if (tracking_.load() == Tracking::Diverged)
        return;

    if (app_.getOPs().isNeedNetworkLedger())
    {
        // If we've never been in synch, there's nothing we can do
        // with a transaction
        JLOG(pJournal_.debug()) << "Ignoring incoming transaction: Need network ledger";
        return;
    }

    SerialIter sit(makeSlice(m->rawtransaction()));

    try
    {
        auto stx = std::make_shared<STTx const>(sit);
        uint256 const txID = stx->getTransactionID();

        // Charge strongly for attempting to relay a txn with tfInnerBatchTxn
        // LCOV_EXCL_START
        /*
           There is no need to check whether the featureBatch amendment is
           enabled.

           * If the `tfInnerBatchTxn` flag is set, and the amendment is
           enabled, then it's an invalid transaction because inner batch
           transactions should not be relayed.
           * If the `tfInnerBatchTxn` flag is set, and the amendment is *not*
           enabled, then the transaction is malformed because it's using an
           "unknown" flag. There's no need to waste the resources to send it
           to the transaction engine.

           We don't normally check transaction validity at this level, but
           since we _need_ to check it when the amendment is enabled, we may as
           well drop it if the flag is set regardless.
        */
        if (stx->isFlag(tfInnerBatchTxn))
        {
            JLOG(pJournal_.warn()) << "Ignoring Network relayed Tx containing "
                                      "tfInnerBatchTxn (handleTransaction).";
            fee_.update(Resource::kFeeModerateBurdenPeer, "inner batch txn");
            return;
        }
        // LCOV_EXCL_STOP

        HashRouterFlags flags = HashRouterFlags::UNDEFINED;
        static constexpr std::chrono::seconds kTxInterval = 10s;

        if (!app_.getHashRouter().shouldProcess(txID, id_, flags, kTxInterval))
        {
            // we have seen this transaction recently
            if (any(flags & HashRouterFlags::BAD))
            {
                fee_.update(Resource::kFeeUselessData, "known bad");
                JLOG(pJournal_.debug()) << "Ignoring known bad tx " << txID;
            }

            // Erase only if the server has seen this tx. If the server has not
            // seen this tx then the tx could not has been queued for this peer.
            else if (eraseTxQueue && txReduceRelayEnabled())
            {
                removeTxQueue(txID);
            }

            overlay_.reportInboundTraffic(
                TrafficCount::Category::TransactionDuplicate, Message::messageSize(*m));

            return;
        }

        JLOG(pJournal_.debug()) << "Got tx " << txID;

        bool checkSignature = true;
        if (cluster())
        {
            if (!m->has_deferred() || !m->deferred())
            {
                // Skip local checks if a server we trust
                // put the transaction in its open ledger
                flags |= HashRouterFlags::TRUSTED;
            }

            // for non-validator nodes only -- localPublicKey is set for
            // validators only
            if (!app_.getValidationPublicKey())
            {
                // For now, be paranoid and have each validator
                // check each transaction, regardless of source
                checkSignature = false;
            }
        }

        if (app_.getLedgerMaster().getValidatedLedgerAge() > 4min)
        {
            JLOG(pJournal_.trace()) << "No new transactions until synchronized";
        }
        else if (app_.getJobQueue().getJobCount(JtTransaction) > app_.config().MAX_TRANSACTIONS)
        {
            overlay_.incJqTransOverflow();
            JLOG(pJournal_.info()) << "Transaction queue is full";
        }
        else
        {
            app_.getJobQueue().addJob(
                JtTransaction,
                "RcvCheckTx",
                [weak = std::weak_ptr<PeerImp>(shared_from_this()),
                 flags,
                 checkSignature,
                 batch,
                 stx]() {
                    if (auto peer = weak.lock())
                        peer->checkTransaction(flags, checkSignature, stx, batch);
                });
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(pJournal_.warn()) << "Transaction invalid: " << strHex(m->rawtransaction())
                               << ". Exception: " << ex.what();
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::kFeeInvalidData, "get_ledger " + msg);
        JLOG(pJournal_.warn()) << "TMGetLedger: " << msg;
    };
    auto const itype{m->itype()};

    // Verify ledger info type
    if (itype < protocol::liBASE || itype > protocol::liTS_CANDIDATE)
    {
        badData("Invalid ledger info type");
        return;
    }

    auto const ltype = [&m]() -> std::optional<::protocol::TMLedgerType> {
        if (m->has_ltype())
            return m->ltype();
        return std::nullopt;
    }();

    if (itype == protocol::liTS_CANDIDATE)
    {
        if (!m->has_ledgerhash())
        {
            badData("Invalid TX candidate set, missing TX set hash");
            return;
        }
    }
    else if (
        !m->has_ledgerhash() && !m->has_ledgerseq() && (!ltype || *ltype != protocol::ltCLOSED))
    {
        badData("Invalid request");
        return;
    }

    // Verify ledger type
    if (ltype && (*ltype < protocol::ltACCEPTED || *ltype > protocol::ltCLOSED))
    {
        badData("Invalid ledger type");
        return;
    }

    // Verify ledger hash
    if (m->has_ledgerhash() && !stringIsUInt256Sized(m->ledgerhash()))
    {
        badData("Invalid ledger hash");
        return;
    }

    // Verify ledger sequence
    if (m->has_ledgerseq())
    {
        auto const ledgerSeq{m->ledgerseq()};

        // Check if within a reasonable range
        using namespace std::chrono_literals;
        if (app_.getLedgerMaster().getValidatedLedgerAge() <= 10s &&
            ledgerSeq > app_.getLedgerMaster().getValidLedgerIndex() + 10)
        {
            badData("Invalid ledger sequence " + std::to_string(ledgerSeq));
            return;
        }
    }

    // Verify ledger node IDs
    if (itype != protocol::liBASE)
    {
        if (m->nodeids_size() <= 0)
        {
            badData("Invalid ledger node IDs");
            return;
        }

        for (auto const& nodeId : m->nodeids())
        {
            if (deserializeSHAMapNodeID(nodeId) == std::nullopt)
            {
                badData("Invalid SHAMap node ID");
                return;
            }
        }
    }

    // Verify query type
    if (m->has_querytype() && m->querytype() != protocol::qtINDIRECT)
    {
        badData("Invalid query type");
        return;
    }

    // Verify query depth
    if (m->has_querydepth())
    {
        if (m->querydepth() > Tuning::kMaxQueryDepth || itype == protocol::liBASE)
        {
            badData("Invalid query depth");
            return;
        }
    }

    // Queue a job to process the request
    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtLedgerReq, "RcvGetLedger", [weak, m]() {
        if (auto peer = weak.lock())
            peer->processLedgerRequest(m);
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m)
{
    JLOG(pJournal_.trace()) << "onMessage, TMProofPathRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFeeMalformedRequest, "proof_path_request disabled");
        return;
    }

    fee_.update(Resource::kFeeModerateBurdenPeer, "received a proof path request");
    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtReplayReq, "RcvProofPReq", [weak, m]() {
        if (auto peer = weak.lock())
        {
            auto reply = peer->ledgerReplayMsgHandler_.processProofPathRequest(m);
            if (reply.has_error())
            {
                if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                {
                    peer->charge(Resource::kFeeMalformedRequest, "proof_path_request");
                }
                else
                {
                    peer->charge(Resource::kFeeRequestNoReply, "proof_path_request");
                }
            }
            else
            {
                peer->send(std::make_shared<Message>(reply, protocol::mtPROOF_PATH_RESPONSE));
            }
        }
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFeeMalformedRequest, "proof_path_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processProofPathResponse(m))
    {
        fee_.update(Resource::kFeeInvalidData, "proof_path_response");
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m)
{
    JLOG(pJournal_.trace()) << "onMessage, TMReplayDeltaRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFeeMalformedRequest, "replay_delta_request disabled");
        return;
    }

    fee_.fee = Resource::kFeeModerateBurdenPeer;
    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtReplayReq, "RcvReplDReq", [weak, m]() {
        if (auto peer = weak.lock())
        {
            auto reply = peer->ledgerReplayMsgHandler_.processReplayDeltaRequest(m);
            if (reply.has_error())
            {
                if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                {
                    peer->charge(Resource::kFeeMalformedRequest, "replay_delta_request");
                }
                else
                {
                    peer->charge(Resource::kFeeRequestNoReply, "replay_delta_request");
                }
            }
            else
            {
                peer->send(std::make_shared<Message>(reply, protocol::mtREPLAY_DELTA_RESPONSE));
            }
        }
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFeeMalformedRequest, "replay_delta_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processReplayDeltaResponse(m))
    {
        fee_.update(Resource::kFeeInvalidData, "replay_delta_response");
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMLedgerData> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::kFeeInvalidData, msg);
        JLOG(pJournal_.warn()) << "TMLedgerData: " << msg;
    };

    // Verify ledger hash
    if (!stringIsUInt256Sized(m->ledgerhash()))
    {
        badData("Invalid ledger hash");
        return;
    }

    // Verify ledger sequence
    {
        auto const ledgerSeq{m->ledgerseq()};
        if (m->type() == protocol::liTS_CANDIDATE)
        {
            if (ledgerSeq != 0)
            {
                badData("Invalid ledger sequence " + std::to_string(ledgerSeq));
                return;
            }
        }
        else
        {
            // Check if within a reasonable range
            using namespace std::chrono_literals;
            if (app_.getLedgerMaster().getValidatedLedgerAge() <= 10s &&
                ledgerSeq > app_.getLedgerMaster().getValidLedgerIndex() + 10)
            {
                badData("Invalid ledger sequence " + std::to_string(ledgerSeq));
                return;
            }
        }
    }

    // Verify ledger info type
    if (m->type() < protocol::liBASE || m->type() > protocol::liTS_CANDIDATE)
    {
        badData("Invalid ledger info type");
        return;
    }

    // Verify reply error
    if (m->has_error() &&
        (m->error() < protocol::reNO_LEDGER || m->error() > protocol::reBAD_REQUEST))
    {
        badData("Invalid reply error");
        return;
    }

    // Verify ledger nodes.
    if (m->nodes_size() <= 0 || m->nodes_size() > Tuning::kHardMaxReplyNodes)
    {
        badData("Invalid Ledger/TXset nodes " + std::to_string(m->nodes_size()));
        return;
    }

    // If there is a request cookie, attempt to relay the message
    if (m->has_requestcookie())
    {
        if (auto peer = overlay_.findPeerByShortID(m->requestcookie()))
        {
            m->clear_requestcookie();
            peer->send(std::make_shared<Message>(*m, protocol::mtLEDGER_DATA));
        }
        else
        {
            JLOG(pJournal_.info()) << "Unable to route TX/ledger data reply";
        }
        return;
    }

    uint256 const ledgerHash = uint256::fromRaw(m->ledgerhash());

    // Otherwise check if received data for a candidate transaction set
    if (m->type() == protocol::liTS_CANDIDATE)
    {
        std::weak_ptr<PeerImp> const weak{shared_from_this()};
        app_.getJobQueue().addJob(JtTxnData, "RcvPeerData", [weak, ledgerHash, m]() {
            if (auto peer = weak.lock())
            {
                peer->app_.getInboundTransactions().gotData(ledgerHash, peer, m);
            }
        });
        return;
    }

    // Consume the message
    app_.getInboundLedgers().gotLedgerData(ledgerHash, shared_from_this(), m);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProposeSet> const& m)
{
    protocol::TMProposeSet const& set = *m;

    auto const sig = makeSlice(set.signature());

    // Preliminary check for the validity of the signature: A DER encoded
    // signature can't be longer than 72 bytes.
    if ((std::clamp<std::size_t>(sig.size(), 64, 72) != sig.size()) ||
        (publicKeyType(makeSlice(set.nodepubkey())) != KeyType::Secp256k1))
    {
        JLOG(pJournal_.warn()) << "Proposal: malformed";
        fee_.update(Resource::kFeeInvalidSignature, " signature can't be longer than 72 bytes");
        return;
    }

    if (!stringIsUInt256Sized(set.currenttxhash()) || !stringIsUInt256Sized(set.previousledger()))
    {
        JLOG(pJournal_.warn()) << "Proposal: malformed";
        fee_.update(Resource::kFeeMalformedRequest, "bad hashes");
        return;
    }

    // RH TODO: when isTrusted = false we should probably also cache a key
    // suppression for 30 seconds to avoid doing a relatively expensive lookup
    // every time a spam packet is received
    PublicKey const publicKey{makeSlice(set.nodepubkey())};
    auto const isTrusted = app_.getValidators().trusted(publicKey);

    // If the operator has specified that untrusted proposals be dropped then
    // this happens here I.e. before further wasting CPU verifying the signature
    // of an untrusted key
    if (!isTrusted)
    {
        // report untrusted proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::Category::ProposalUntrusted, Message::messageSize(*m));

        if (app_.config().RELAY_UNTRUSTED_PROPOSALS == -1)
            return;
    }

    uint256 const proposeHash = uint256::fromRaw(set.currenttxhash());
    uint256 const prevLedger = uint256::fromRaw(set.previousledger());

    NetClock::time_point const closeTime{NetClock::duration{set.closetime()}};

    uint256 const suppression = proposalUniqueId(
        proposeHash, prevLedger, set.proposeseq(), closeTime, publicKey.slice(), sig);

    if (auto [added, relayed] = app_.getHashRouter().addSuppressionPeerWithStatus(suppression, id_);
        !added)
    {
        // Count unique messages (Slots has it's own 'HashRouter'), which a peer
        // receives within IDLED seconds since the message has been relayed.
        if (relayed && (stopwatch().now() - *relayed) < reduce_relay::kIdled)
            overlay_.updateSlotAndSquelch(suppression, publicKey, id_, protocol::mtPROPOSE_LEDGER);

        // report duplicate proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::Category::ProposalDuplicate, Message::messageSize(*m));

        JLOG(pJournal_.trace()) << "Proposal: duplicate";

        return;
    }

    if (!isTrusted)
    {
        if (tracking_.load() == Tracking::Diverged)
        {
            JLOG(pJournal_.debug()) << "Proposal: Dropping untrusted (peer divergence)";
            return;
        }

        if (!cluster() && app_.getFeeTrack().isLoadedLocal())
        {
            JLOG(pJournal_.debug()) << "Proposal: Dropping untrusted (load)";
            return;
        }
    }

    JLOG(pJournal_.trace()) << "Proposal: " << (isTrusted ? "trusted" : "untrusted");

    auto proposal = RCLCxPeerPos(
        publicKey,
        sig,
        suppression,
        RCLCxPeerPos::Proposal{
            prevLedger,
            set.proposeseq(),
            proposeHash,
            closeTime,
            app_.getTimeKeeper().closeTime(),
            calcNodeID(app_.getValidatorManifests().getMasterKey(publicKey))});

    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(
        isTrusted ? JtProposalT : JtProposalUt, "checkPropose", [weak, isTrusted, m, proposal]() {
            if (auto peer = weak.lock())
                peer->checkPropose(isTrusted, m, proposal);
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMStatusChange> const& m)
{
    JLOG(pJournal_.trace()) << "Status: Change";

    if (!m->has_networktime())
        m->set_networktime(app_.getTimeKeeper().now().time_since_epoch().count());

    {
        std::scoped_lock const sl(recentLock_);
        if (!lastStatus_.has_newstatus() || m->has_newstatus())
        {
            lastStatus_ = *m;
        }
        else
        {
            // preserve old status
            protocol::NodeStatus const status = lastStatus_.newstatus();
            lastStatus_ = *m;
            m->set_newstatus(status);
        }
    }

    if (m->newevent() == protocol::neLOST_SYNC)
    {
        bool outOfSync{false};
        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::scoped_lock const sl(recentLock_);
            if (!closedLedgerHash_.isZero())
            {
                outOfSync = true;
                closedLedgerHash_.zero();
            }
            previousLedgerHash_.zero();
        }
        if (outOfSync)
        {
            JLOG(pJournal_.debug()) << "Status: Out of sync";
        }
        return;
    }

    {
        uint256 closedLedgerHash{};
        bool const peerChangedLedgers{m->has_ledgerhash() && stringIsUInt256Sized(m->ledgerhash())};

        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::scoped_lock const sl(recentLock_);
            if (peerChangedLedgers)
            {
                closedLedgerHash_ = m->ledgerhash();
                closedLedgerHash = closedLedgerHash_;
                addLedger(closedLedgerHash, sl);
            }
            else
            {
                closedLedgerHash_.zero();
            }

            if (m->has_ledgerhashprevious() && stringIsUInt256Sized(m->ledgerhashprevious()))
            {
                previousLedgerHash_ = m->ledgerhashprevious();
                addLedger(previousLedgerHash_, sl);
            }
            else
            {
                previousLedgerHash_.zero();
            }
        }
        if (peerChangedLedgers)
        {
            JLOG(pJournal_.debug()) << "LCL is " << closedLedgerHash;
        }
        else
        {
            JLOG(pJournal_.debug()) << "Status: No ledger";
        }
    }

    if (m->has_firstseq() && m->has_lastseq())
    {
        std::scoped_lock const sl(recentLock_);

        minLedger_ = m->firstseq();
        maxLedger_ = m->lastseq();

        if ((maxLedger_ < minLedger_) || (minLedger_ == 0) || (maxLedger_ == 0))
            minLedger_ = maxLedger_ = 0;
    }

    if (m->has_ledgerseq() && app_.getLedgerMaster().getValidatedLedgerAge() < 2min)
    {
        checkTracking(m->ledgerseq(), app_.getLedgerMaster().getValidLedgerIndex());
    }

    app_.getOPs().pubPeerStatus([m, this]() -> json::Value {
        json::Value j = json::ValueType::Object;

        if (m->has_newstatus())
        {
            switch (m->newstatus())
            {
                case protocol::nsCONNECTING:
                    j[jss::status] = "CONNECTING";
                    break;
                case protocol::nsCONNECTED:
                    j[jss::status] = "CONNECTED";
                    break;
                case protocol::nsMONITORING:
                    j[jss::status] = "MONITORING";
                    break;
                case protocol::nsVALIDATING:
                    j[jss::status] = "VALIDATING";
                    break;
                case protocol::nsSHUTTING:
                    j[jss::status] = "SHUTTING";
                    break;
            }
        }

        if (m->has_newevent())
        {
            switch (m->newevent())
            {
                case protocol::neCLOSING_LEDGER:
                    j[jss::action] = "CLOSING_LEDGER";
                    break;
                case protocol::neACCEPTED_LEDGER:
                    j[jss::action] = "ACCEPTED_LEDGER";
                    break;
                case protocol::neSWITCHED_LEDGER:
                    j[jss::action] = "SWITCHED_LEDGER";
                    break;
                case protocol::neLOST_SYNC:
                    j[jss::action] = "LOST_SYNC";
                    break;
            }
        }

        if (m->has_ledgerseq())
        {
            j[jss::ledger_index] = m->ledgerseq();
        }

        if (m->has_ledgerhash())
        {
            uint256 closedLedgerHash{};
            {
                std::scoped_lock const sl(recentLock_);
                closedLedgerHash = closedLedgerHash_;
            }
            j[jss::ledger_hash] = to_string(closedLedgerHash);
        }

        if (m->has_networktime())
        {
            j[jss::date] = json::UInt(m->networktime());
        }

        if (m->has_firstseq() && m->has_lastseq())
        {
            j[jss::ledger_index_min] = json::UInt(m->firstseq());
            j[jss::ledger_index_max] = json::UInt(m->lastseq());
        }

        return j;
    });
}

void
PeerImp::checkTracking(std::uint32_t validationSeq)
{
    std::uint32_t serverSeq = 0;
    {
        // Extract the sequence number of the highest
        // ledger this peer has
        std::scoped_lock const sl(recentLock_);

        serverSeq = maxLedger_;
    }
    if (serverSeq != 0)
    {
        // Compare the peer's ledger sequence to the
        // sequence of a recently-validated ledger
        checkTracking(serverSeq, validationSeq);
    }
}

void
PeerImp::checkTracking(std::uint32_t seq1, std::uint32_t seq2)
{
    int const diff = std::max(seq1, seq2) - std::min(seq1, seq2);

    if (diff < Tuning::kConvergedLedgerLimit)
    {
        // The peer's ledger sequence is close to the validation's
        tracking_ = Tracking::Converged;
    }

    if ((diff > Tuning::kDivergedLedgerLimit) && (tracking_.load() != Tracking::Diverged))
    {
        // The peer's ledger sequence is way off the validation's
        std::scoped_lock const sl(recentLock_);

        tracking_ = Tracking::Diverged;
        trackingTime_ = clock_type::now();
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m)
{
    if (!stringIsUInt256Sized(m->hash()))
    {
        fee_.update(Resource::kFeeMalformedRequest, "bad hash");
        return;
    }

    uint256 const hash = uint256::fromRaw(m->hash());

    if (m->status() == protocol::tsHAVE)
    {
        std::scoped_lock const sl(recentLock_);

        if (std::ranges::find(recentTxSets_, hash) != recentTxSets_.end())
        {
            fee_.update(Resource::kFeeUselessData, "duplicate (tsHAVE)");
            return;
        }

        recentTxSets_.push_back(hash);
    }
}

void
PeerImp::onValidatorListMessage(
    std::string const& messageType,
    std::string const& manifest,
    std::uint32_t version,
    std::vector<ValidatorBlobInfo> const& blobs)
{
    // If there are no blobs, the message is malformed (possibly because of
    // ValidatorList class rules), so charge accordingly and skip processing.
    if (blobs.empty())
    {
        JLOG(pJournal_.warn()) << "Ignored malformed " << messageType;
        // This shouldn't ever happen with a well-behaved peer
        fee_.update(Resource::kFeeHeavyBurdenPeer, "no blobs");
        return;
    }

    auto const hash = sha512Half(manifest, blobs, version);

    JLOG(pJournal_.debug()) << "Received " << messageType;

    if (!app_.getHashRouter().addSuppressionPeer(hash, id_))
    {
        JLOG(pJournal_.debug()) << messageType << ": received duplicate " << messageType;
        // Charging this fee here won't hurt the peer in the normal
        // course of operation (ie. refresh every 5 minutes), but
        // will add up if the peer is misbehaving.
        fee_.update(Resource::kFeeUselessData, "duplicate");
        return;
    }

    auto const applyResult = app_.getValidators().applyListsAndBroadcast(
        manifest,
        version,
        blobs,
        remoteAddress_.toString(),
        hash,
        app_.getOverlay(),
        app_.getHashRouter(),
        app_.getOPs());

    JLOG(pJournal_.debug()) << "Processed " << messageType << " version " << version << " from "
                            << (applyResult.publisherKey ? strHex(*applyResult.publisherKey)
                                                         : "unknown or invalid publisher")
                            << " with best result " << to_string(applyResult.bestDisposition());

    // Act based on the best result
    switch (applyResult.bestDisposition())
    {
        // New list
        case ListDisposition::Accepted:
        // Newest list is expired, and that needs to be broadcast, too
        case ListDisposition::Expired:
        // Future list
        case ListDisposition::Pending: {
            std::scoped_lock<std::mutex> const sl(recentLock_);

            XRPL_ASSERT(
                applyResult.publisherKey,
                "xrpl::PeerImp::onValidatorListMessage : publisher key is "
                "set");
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access) assert above
            auto const& pubKey = *applyResult.publisherKey;
#ifndef NDEBUG
            if (auto const iter = publisherListSequences_.find(pubKey);
                iter != publisherListSequences_.end())
            {
                XRPL_ASSERT(
                    iter->second < applyResult.sequence,
                    "xrpl::PeerImp::onValidatorListMessage : lower sequence");
            }
#endif
            publisherListSequences_[pubKey] = applyResult.sequence;
        }
        break;
        case ListDisposition::SameSequence:
        case ListDisposition::KnownSequence:
#ifndef NDEBUG
        {
            std::scoped_lock<std::mutex> const sl(recentLock_);
            XRPL_ASSERT(
                applyResult.sequence && applyResult.publisherKey,
                "xrpl::PeerImp::onValidatorListMessage : nonzero sequence "
                "and set publisher key");
            XRPL_ASSERT(
                publisherListSequences_[*applyResult.publisherKey] <= applyResult.sequence,
                "xrpl::PeerImp::onValidatorListMessage : maximum sequence");
        }
#endif  // !NDEBUG

        break;
        case ListDisposition::Stale:
        case ListDisposition::Untrusted:
        case ListDisposition::Invalid:
        case ListDisposition::UnsupportedVersion:
            break;
        // LCOV_EXCL_START
        default:
            UNREACHABLE(
                "xrpl::PeerImp::onValidatorListMessage : invalid best list "
                "disposition");
            // LCOV_EXCL_STOP
    }

    // Charge based on the worst result
    switch (applyResult.worstDisposition())
    {
        case ListDisposition::Accepted:
        case ListDisposition::Expired:
        case ListDisposition::Pending:
            // No charges for good data
            break;
        case ListDisposition::SameSequence:
        case ListDisposition::KnownSequence:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_.update(Resource::kFeeUselessData, " duplicate (same_sequence or known_sequence)");
            break;
        case ListDisposition::Stale:
            // There are very few good reasons for a peer to send an
            // old list, particularly more than once.
            fee_.update(Resource::kFeeInvalidData, "expired");
            break;
        case ListDisposition::Untrusted:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_.update(Resource::kFeeUselessData, "untrusted");
            break;
        case ListDisposition::Invalid:
            // This shouldn't ever happen with a well-behaved peer
            fee_.update(Resource::kFeeInvalidSignature, "invalid list disposition");
            break;
        case ListDisposition::UnsupportedVersion:
            // During a version transition, this may be legitimate.
            // If it happens frequently, that's probably bad.
            fee_.update(Resource::kFeeInvalidData, "version");
            break;
        // LCOV_EXCL_START
        default:
            UNREACHABLE(
                "xrpl::PeerImp::onValidatorListMessage : invalid worst list "
                "disposition");
            // LCOV_EXCL_STOP
    }

    // Log based on all the results.
    for (auto const& [disp, count] : applyResult.dispositions)
    {
        switch (disp)
        {
            // New list
            case ListDisposition::Accepted:
                JLOG(pJournal_.debug()) << "Applied " << count << " new " << messageType;
                break;
            // Newest list is expired, and that needs to be broadcast, too
            case ListDisposition::Expired:
                JLOG(pJournal_.debug()) << "Applied " << count << " expired " << messageType;
                break;
            // Future list
            case ListDisposition::Pending:
                JLOG(pJournal_.debug()) << "Processed " << count << " future " << messageType;
                break;
            case ListDisposition::SameSequence:
                JLOG(pJournal_.warn())
                    << "Ignored " << count << " " << messageType << "(s) with current sequence";
                break;
            case ListDisposition::KnownSequence:
                JLOG(pJournal_.warn())
                    << "Ignored " << count << " " << messageType << "(s) with future sequence";
                break;
            case ListDisposition::Stale:
                JLOG(pJournal_.warn()) << "Ignored " << count << "stale " << messageType;
                break;
            case ListDisposition::Untrusted:
                JLOG(pJournal_.warn()) << "Ignored " << count << " untrusted " << messageType;
                break;
            case ListDisposition::UnsupportedVersion:
                JLOG(pJournal_.warn())
                    << "Ignored " << count << "unsupported version " << messageType;
                break;
            case ListDisposition::Invalid:
                JLOG(pJournal_.warn()) << "Ignored " << count << "invalid " << messageType;
                break;
            // LCOV_EXCL_START
            default:
                UNREACHABLE(
                    "xrpl::PeerImp::onValidatorListMessage : invalid list "
                    "disposition");
                // LCOV_EXCL_STOP
        }
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidatorList> const& m)
{
    try
    {
        if (!supportsFeature(ProtocolFeature::ValidatorListPropagation))
        {
            JLOG(pJournal_.debug()) << "ValidatorList: received validator list from peer using "
                                    << "protocol version " << to_string(protocol_)
                                    << " which shouldn't support this feature.";
            fee_.update(Resource::kFeeUselessData, "unsupported peer");
            return;
        }
        onValidatorListMessage(
            "ValidatorList", m->manifest(), m->version(), ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(pJournal_.warn()) << "ValidatorList: Exception, " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::kFeeInvalidData, e.what());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidatorListCollection> const& m)
{
    try
    {
        if (!supportsFeature(ProtocolFeature::ValidatorList2Propagation))
        {
            JLOG(pJournal_.debug()) << "ValidatorListCollection: received validator list from peer "
                                    << "using protocol version " << to_string(protocol_)
                                    << " which shouldn't support this feature.";
            fee_.update(Resource::kFeeUselessData, "unsupported peer");
            return;
        }
        if (m->version() < 2)
        {
            JLOG(pJournal_.debug())
                << "ValidatorListCollection: received invalid validator list "
                   "version "
                << m->version() << " from peer using protocol version " << to_string(protocol_);
            fee_.update(Resource::kFeeInvalidData, "wrong version");
            return;
        }
        onValidatorListMessage(
            "ValidatorListCollection", m->manifest(), m->version(), ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(pJournal_.warn()) << "ValidatorListCollection: Exception, " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::kFeeInvalidData, e.what());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidation> const& m)
{
    if (m->validation().size() < 50)
    {
        JLOG(pJournal_.warn()) << "Validation: Too small";
        fee_.update(Resource::kFeeMalformedRequest, "too small");
        return;
    }

    try
    {
        auto const closeTime = app_.getTimeKeeper().closeTime();

        std::shared_ptr<STValidation> val;
        {
            SerialIter sit(makeSlice(m->validation()));
            val = std::make_shared<STValidation>(
                std::ref(sit),
                [this](PublicKey const& pk) {
                    return calcNodeID(app_.getValidatorManifests().getMasterKey(pk));
                },
                false);
            val->setSeen(closeTime);
        }

        if (!isCurrent(
                app_.getValidations().parms(),
                app_.getTimeKeeper().closeTime(),
                val->getSignTime(),
                val->getSeenTime()))
        {
            JLOG(pJournal_.trace()) << "Validation: Not current";
            fee_.update(Resource::kFeeUselessData, "not current");
            return;
        }

        // RH TODO: when isTrusted = false we should probably also cache a key
        // suppression for 30 seconds to avoid doing a relatively expensive
        // lookup every time a spam packet is received
        auto const isTrusted = app_.getValidators().trusted(val->getSignerPublic());

        // If the operator has specified that untrusted validations be
        // dropped then this happens here I.e. before further wasting CPU
        // verifying the signature of an untrusted key
        if (!isTrusted)
        {
            // increase untrusted validations received
            overlay_.reportInboundTraffic(
                TrafficCount::Category::ValidationUntrusted, Message::messageSize(*m));

            if (app_.config().RELAY_UNTRUSTED_VALIDATIONS == -1)
                return;
        }

        auto key = sha512Half(makeSlice(m->validation()));

        auto [added, relayed] = app_.getHashRouter().addSuppressionPeerWithStatus(key, id_);

        if (!added)
        {
            // Count unique messages (Slots has it's own 'HashRouter'), which a
            // peer receives within IDLED seconds since the message has been
            // relayed.
            if (relayed && (stopwatch().now() - *relayed) < reduce_relay::kIdled)
            {
                overlay_.updateSlotAndSquelch(
                    key, val->getSignerPublic(), id_, protocol::mtVALIDATION);
            }

            // increase duplicate validations received
            overlay_.reportInboundTraffic(
                TrafficCount::Category::ValidationDuplicate, Message::messageSize(*m));

            JLOG(pJournal_.trace()) << "Validation: duplicate";
            return;
        }

        if (!isTrusted && (tracking_.load() == Tracking::Diverged))
        {
            JLOG(pJournal_.debug()) << "Dropping untrusted validation from diverged peer";
        }
        else if (isTrusted || !app_.getFeeTrack().isLoadedLocal())
        {
            std::string const name = isTrusted ? "ChkTrust" : "ChkUntrust";

            std::weak_ptr<PeerImp> const weak = shared_from_this();
            app_.getJobQueue().addJob(
                isTrusted ? JtValidationT : JtValidationUt, name, [weak, val, m, key]() {
                    if (auto peer = weak.lock())
                        peer->checkValidation(val, key, m);
                });
        }
        else
        {
            JLOG(pJournal_.debug()) << "Dropping untrusted validation for load";
        }
    }
    catch (std::exception const& e)
    {
        JLOG(pJournal_.warn()) << "Exception processing validation: " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::kFeeMalformedRequest, e.what());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash const& packet = *m;

    JLOG(pJournal_.trace()) << "received TMGetObjectByHash " << packet.type() << " "
                            << packet.objects_size();

    if (packet.query())
    {
        // this is a query
        if (sendQueue_.size() >= Tuning::kDropSendQueue)
        {
            JLOG(pJournal_.debug()) << "GetObject: Large send queue";
            return;
        }

        if (packet.type() == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            doFetchPack(m);
            return;
        }

        if (packet.type() == protocol::TMGetObjectByHash::otTRANSACTIONS)
        {
            if (!txReduceRelayEnabled())
            {
                JLOG(pJournal_.error()) << "TMGetObjectByHash: tx reduce-relay is disabled";
                fee_.update(Resource::kFeeMalformedRequest, "disabled");
                return;
            }

            std::weak_ptr<PeerImp> const weak = shared_from_this();
            app_.getJobQueue().addJob(JtRequestedTxn, "DoTxs", [weak, m]() {
                if (auto peer = weak.lock())
                    peer->doTransactions(m);
            });
            return;
        }

        protocol::TMGetObjectByHash reply;

        reply.set_query(false);

        reply.set_type(packet.type());

        if (packet.has_ledgerhash())
        {
            if (!stringIsUInt256Sized(packet.ledgerhash()))
            {
                fee_.update(Resource::kFeeMalformedRequest, "ledger hash");
                return;
            }

            reply.set_ledgerhash(packet.ledgerhash());
        }

        fee_.update(Resource::kFeeModerateBurdenPeer, " received a get object by hash request");

        // This is a very minimal implementation
        for (int i = 0; i < packet.objects_size(); ++i)
        {
            auto const& obj = packet.objects(i);
            if (obj.has_hash() && stringIsUInt256Sized(obj.hash()))
            {
                uint256 const hash = uint256::fromRaw(obj.hash());
                // VFALCO TODO Move this someplace more sensible so we dont
                //             need to inject the NodeStore interfaces.
                std::uint32_t const seq{obj.has_ledgerseq() ? obj.ledgerseq() : 0};
                auto nodeObject{app_.getNodeStore().fetchNodeObject(hash, seq)};
                if (nodeObject)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects();
                    newObj.set_hash(hash.begin(), hash.size());
                    newObj.set_data(&nodeObject->getData().front(), nodeObject->getData().size());

                    if (obj.has_nodeid())
                        newObj.set_index(obj.nodeid());
                    if (obj.has_ledgerseq())
                        newObj.set_ledgerseq(obj.ledgerseq());

                    // Check if by adding this object, reply has reached its
                    // limit
                    if (reply.objects_size() >= Tuning::kHardMaxReplyNodes)
                    {
                        fee_.update(
                            Resource::kFeeModerateBurdenPeer,
                            "Reply limit reached. Truncating reply.");
                        break;
                    }
                }
            }
        }

        JLOG(pJournal_.trace()) << "GetObj: " << reply.objects_size() << " of "
                                << packet.objects_size();
        send(std::make_shared<Message>(reply, protocol::mtGET_OBJECTS));
    }
    else
    {
        // this is a reply
        std::uint32_t pLSeq = 0;
        bool pLDo = true;
        bool progress = false;

        for (int i = 0; i < packet.objects_size(); ++i)
        {
            protocol::TMIndexedObject const& obj = packet.objects(i);

            if (obj.has_hash() && stringIsUInt256Sized(obj.hash()))
            {
                if (obj.has_ledgerseq())
                {
                    if (obj.ledgerseq() != pLSeq)
                    {
                        if (pLDo && (pLSeq != 0))
                        {
                            JLOG(pJournal_.debug()) << "GetObj: Full fetch pack for " << pLSeq;
                        }
                        pLSeq = obj.ledgerseq();
                        pLDo = !app_.getLedgerMaster().haveLedger(pLSeq);

                        if (!pLDo)
                        {
                            JLOG(pJournal_.debug()) << "GetObj: Late fetch pack for " << pLSeq;
                        }
                        else
                        {
                            progress = true;
                        }
                    }
                }

                if (pLDo)
                {
                    uint256 const hash = uint256::fromRaw(obj.hash());

                    app_.getLedgerMaster().addFetchPack(
                        hash, std::make_shared<Blob>(obj.data().begin(), obj.data().end()));
                }
            }
        }

        if (pLDo && (pLSeq != 0))
        {
            JLOG(pJournal_.debug()) << "GetObj: Partial fetch pack for " << pLSeq;
        }
        if (packet.type() == protocol::TMGetObjectByHash::otFETCH_PACK)
            app_.getLedgerMaster().gotFetchPack(progress, pLSeq);
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactions> const& m)
{
    if (!txReduceRelayEnabled())
    {
        JLOG(pJournal_.error()) << "TMHaveTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::kFeeMalformedRequest, "disabled");
        return;
    }

    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtMissingTxn, "HandleHaveTxs", [weak, m]() {
        if (auto peer = weak.lock())
            peer->handleHaveTransactions(m);
    });
}

void
PeerImp::handleHaveTransactions(std::shared_ptr<protocol::TMHaveTransactions> const& m)
{
    protocol::TMGetObjectByHash tmBH;
    tmBH.set_type(protocol::TMGetObjectByHash_ObjectType_otTRANSACTIONS);
    tmBH.set_query(true);

    JLOG(pJournal_.trace()) << "received TMHaveTransactions " << m->hashes_size();

    for (std::uint32_t i = 0; i < m->hashes_size(); i++)
    {
        if (!stringIsUInt256Sized(m->hashes(i)))
        {
            JLOG(pJournal_.error()) << "TMHaveTransactions with invalid hash size";
            fee_.update(Resource::kFeeMalformedRequest, "hash size");
            return;
        }

        uint256 hash = uint256::fromRaw(m->hashes(i));

        auto txn = app_.getMasterTransaction().fetchFromCache(hash);

        JLOG(pJournal_.trace()) << "checking transaction " << (bool)txn;

        if (!txn)
        {
            JLOG(pJournal_.debug()) << "adding transaction to request";

            auto obj = tmBH.add_objects();
            obj->set_hash(hash.data(), hash.size());
        }
        else
        {
            // Erase only if a peer has seen this tx. If the peer has not
            // seen this tx then the tx could not has been queued for this
            // peer.
            removeTxQueue(hash);
        }
    }

    JLOG(pJournal_.trace()) << "transaction request object is " << tmBH.objects_size();

    if (tmBH.objects_size() > 0)
        send(std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS));
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransactions> const& m)
{
    if (!txReduceRelayEnabled())
    {
        JLOG(pJournal_.error()) << "TMTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::kFeeMalformedRequest, "disabled");
        return;
    }

    JLOG(pJournal_.trace()) << "received TMTransactions " << m->transactions_size();

    overlay_.addTxMetrics(m->transactions_size());

    for (std::uint32_t i = 0; i < m->transactions_size(); ++i)
    {
        handleTransaction(
            std::shared_ptr<protocol::TMTransaction>(
                m->mutable_transactions(i), [](protocol::TMTransaction*) {}),
            false,
            true);
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMSquelch> const& m)
{
    using on_message_fn = void (PeerImp::*)(std::shared_ptr<protocol::TMSquelch> const&);
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind((on_message_fn)&PeerImp::onMessage, shared_from_this(), m));
        return;
    }

    if (!m->has_validatorpubkey())
    {
        fee_.update(Resource::kFeeInvalidData, "squelch no pubkey");
        return;
    }
    auto validator = m->validatorpubkey();
    auto const slice{makeSlice(validator)};
    if (!publicKeyType(slice))
    {
        fee_.update(Resource::kFeeInvalidData, "squelch bad pubkey");
        return;
    }
    PublicKey const key(slice);

    // Ignore the squelch for validator's own messages.
    if (key == app_.getValidationPublicKey())
    {
        JLOG(pJournal_.debug()) << "onMessage: TMSquelch discarding validator's squelch " << slice;
        return;
    }

    std::uint32_t const duration = m->has_squelchduration() ? m->squelchduration() : 0;
    if (!m->squelch())
    {
        squelch_.removeSquelch(key);
    }
    else if (!squelch_.addSquelch(key, std::chrono::seconds{duration}))
    {
        fee_.update(Resource::kFeeInvalidData, "squelch duration");
    }

    JLOG(pJournal_.debug()) << "onMessage: TMSquelch " << slice << " " << id() << " " << duration;
}

//--------------------------------------------------------------------------

void
PeerImp::addLedger(uint256 const& hash, std::scoped_lock<std::mutex> const& lockedRecentLock)
{
    // lockedRecentLock is passed as a reminder that recentLock_ must be
    // locked by the caller.
    (void)lockedRecentLock;

    if (std::ranges::find(recentLedgers_, hash) != recentLedgers_.end())
        return;

    recentLedgers_.push_back(hash);
}

void
PeerImp::doFetchPack(std::shared_ptr<protocol::TMGetObjectByHash> const& packet)
{
    // VFALCO TODO Invert this dependency using an observer and shared state
    // object. Don't queue fetch pack jobs if we're under load or we already
    // have some queued.
    if (app_.getFeeTrack().isLoadedLocal() ||
        (app_.getLedgerMaster().getValidatedLedgerAge() > 40s) ||
        (app_.getJobQueue().getJobCount(JtPack) > 10))
    {
        JLOG(pJournal_.info()) << "Too busy to make fetch pack";
        return;
    }

    if (!stringIsUInt256Sized(packet->ledgerhash()))
    {
        JLOG(pJournal_.warn()) << "FetchPack hash size malformed";
        fee_.update(Resource::kFeeMalformedRequest, "hash size");
        return;
    }

    fee_.fee = Resource::kFeeHeavyBurdenPeer;

    uint256 const hash = uint256::fromRaw(packet->ledgerhash());

    std::weak_ptr<PeerImp> const weak = shared_from_this();
    auto elapsed = UptimeClock::now();
    auto const pap = &app_;
    app_.getJobQueue().addJob(JtPack, "MakeFetchPack", [pap, weak, packet, hash, elapsed]() {
        pap->getLedgerMaster().makeFetchPack(weak, packet, hash, elapsed);
    });
}

void
PeerImp::doTransactions(std::shared_ptr<protocol::TMGetObjectByHash> const& packet)
{
    protocol::TMTransactions reply;

    JLOG(pJournal_.trace()) << "received TMGetObjectByHash requesting tx "
                            << packet->objects_size();

    if (packet->objects_size() > reduce_relay::kMaxTxQueueSize)
    {
        JLOG(pJournal_.error()) << "doTransactions, invalid number of hashes";
        fee_.update(Resource::kFeeMalformedRequest, "too big");
        return;
    }

    for (std::uint32_t i = 0; i < packet->objects_size(); ++i)
    {
        auto const& obj = packet->objects(i);

        if (!stringIsUInt256Sized(obj.hash()))
        {
            fee_.update(Resource::kFeeMalformedRequest, "hash size");
            return;
        }

        uint256 hash = uint256::fromRaw(obj.hash());

        auto txn = app_.getMasterTransaction().fetchFromCache(hash);

        if (!txn)
        {
            JLOG(pJournal_.error())
                << "doTransactions, transaction not found " << Slice(hash.data(), hash.size());
            fee_.update(Resource::kFeeMalformedRequest, "tx not found");
            return;
        }

        Serializer s;
        auto tx = reply.add_transactions();
        auto sttx = txn->getSTransaction();
        sttx->add(s);
        tx->set_rawtransaction(s.data(), s.size());
        tx->set_status(
            txn->getStatus() == TransStatus::INCLUDED ? protocol::tsCURRENT : protocol::tsNEW);
        tx->set_receivetimestamp(app_.getTimeKeeper().now().time_since_epoch().count());
        tx->set_deferred(txn->getSubmitResult().queued);
    }

    if (reply.transactions_size() > 0)
        send(std::make_shared<Message>(reply, protocol::mtTRANSACTIONS));
}

void
PeerImp::checkTransaction(
    HashRouterFlags flags,
    bool checkSignature,
    std::shared_ptr<STTx const> const& stx,
    bool batch)
{
    // VFALCO TODO Rewrite to not use exceptions
    try
    {
        // charge strongly for relaying batch txns
        // LCOV_EXCL_START
        /*
           There is no need to check whether the featureBatch amendment is
           enabled.

           * If the `tfInnerBatchTxn` flag is set, and the amendment is
           enabled, then it's an invalid transaction because inner batch
           transactions should not be relayed.
           * If the `tfInnerBatchTxn` flag is set, and the amendment is *not*
           enabled, then the transaction is malformed because it's using an
           "unknown" flag. There's no need to waste the resources to send it
           to the transaction engine.

           We don't normally check transaction validity at this level, but
           since we _need_ to check it when the amendment is enabled, we may as
           well drop it if the flag is set regardless.
        */
        if (stx->isFlag(tfInnerBatchTxn))
        {
            JLOG(pJournal_.warn()) << "Ignoring Network relayed Tx containing "
                                      "tfInnerBatchTxn (checkSignature).";
            charge(Resource::kFeeModerateBurdenPeer, "inner batch txn");
            return;
        }
        // LCOV_EXCL_STOP

        // Expired?
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32(sfLastLedgerSequence) < app_.getLedgerMaster().getValidLedgerIndex()))
        {
            JLOG(pJournal_.info()) << "Marking transaction " << stx->getTransactionID()
                                   << "as BAD because it's expired";
            app_.getHashRouter().setFlags(stx->getTransactionID(), HashRouterFlags::BAD);
            charge(Resource::kFeeUselessData, "expired tx");
            return;
        }

        if (isPseudoTx(*stx))
        {
            // Don't do anything with pseudo transactions except put them in the
            // TransactionMaster cache
            std::string reason;
            auto tx = std::make_shared<Transaction>(stx, reason, app_);
            XRPL_ASSERT(
                tx->getStatus() == TransStatus::NEW,
                "xrpl::PeerImp::checkTransaction Transaction created "
                "correctly");
            if (tx->getStatus() == TransStatus::NEW)
            {
                JLOG(pJournal_.debug()) << "Processing " << (batch ? "batch" : "unsolicited")
                                        << " pseudo-transaction tx " << tx->getID();

                app_.getMasterTransaction().canonicalize(&tx);
                // Tell the overlay about it, but don't relay it.
                auto const toSkip = app_.getHashRouter().shouldRelay(tx->getID());
                if (toSkip)
                {
                    JLOG(pJournal_.debug())
                        << "Passing skipped pseudo pseudo-transaction tx " << tx->getID();
                    app_.getOverlay().relay(tx->getID(), {}, *toSkip);
                }
                if (!batch)
                {
                    JLOG(pJournal_.debug()) << "Charging for pseudo-transaction tx " << tx->getID();
                    charge(Resource::kFeeUselessData, "pseudo tx");
                }

                return;
            }
        }

        if (checkSignature)
        {
            // Check the signature before handing off to the job queue.
            if (auto [valid, validReason] = checkValidity(
                    app_.getHashRouter(), *stx, app_.getLedgerMaster().getValidatedRules());
                valid != Validity::Valid)
            {
                if (!validReason.empty())
                {
                    JLOG(pJournal_.debug()) << "Exception checking transaction: " << validReason;
                }

                // Probably not necessary to set HashRouterFlags::BAD, but
                // doesn't hurt.
                app_.getHashRouter().setFlags(stx->getTransactionID(), HashRouterFlags::BAD);
                charge(Resource::kFeeInvalidSignature, "check transaction signature failure");
                return;
            }
        }
        else
        {
            forceValidity(app_.getHashRouter(), stx->getTransactionID(), Validity::Valid);
        }

        std::string reason;
        auto tx = std::make_shared<Transaction>(stx, reason, app_);

        if (tx->getStatus() == TransStatus::INVALID)
        {
            if (!reason.empty())
            {
                JLOG(pJournal_.debug()) << "Exception checking transaction: " << reason;
            }
            app_.getHashRouter().setFlags(stx->getTransactionID(), HashRouterFlags::BAD);
            charge(Resource::kFeeInvalidSignature, "tx (impossible)");
            return;
        }

        bool const trusted = any(flags & HashRouterFlags::TRUSTED);
        app_.getOPs().processTransaction(tx, trusted, false, NetworkOPs::FailHard::No);
    }
    catch (std::exception const& ex)
    {
        JLOG(pJournal_.warn()) << "Exception in " << __func__ << ": " << ex.what();
        app_.getHashRouter().setFlags(stx->getTransactionID(), HashRouterFlags::BAD);
        using namespace std::string_literals;
        charge(Resource::kFeeInvalidData, "tx "s + ex.what());
    }
}

// Called from our JobQueue
void
PeerImp::checkPropose(
    bool isTrusted,
    std::shared_ptr<protocol::TMProposeSet> const& packet,
    RCLCxPeerPos peerPos)
{
    JLOG(pJournal_.trace()) << "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    XRPL_ASSERT(packet, "xrpl::PeerImp::checkPropose : non-null packet");

    if (!cluster() && !peerPos.checkSign())
    {
        std::string const desc{"Proposal fails sig check"};
        JLOG(pJournal_.warn()) << desc;
        charge(Resource::kFeeInvalidSignature, desc);
        return;
    }

    bool relay = false;

    if (isTrusted)
    {
        relay = app_.getOPs().processTrustedProposal(peerPos);
    }
    else
    {
        relay = app_.config().RELAY_UNTRUSTED_PROPOSALS == 1 || cluster();
    }

    if (relay)
    {
        // haveMessage contains peers, which are suppressed; i.e. the peers
        // are the source of the message, consequently the message should
        // not be relayed to these peers. But the message must be counted
        // as part of the squelch logic.
        auto haveMessage =
            app_.getOverlay().relay(*packet, peerPos.suppressionID(), peerPos.publicKey());
        if (!haveMessage.empty())
        {
            overlay_.updateSlotAndSquelch(
                peerPos.suppressionID(),
                peerPos.publicKey(),
                std::move(haveMessage),
                protocol::mtPROPOSE_LEDGER);
        }
    }
}

void
PeerImp::checkValidation(
    std::shared_ptr<STValidation> const& val,
    uint256 const& key,
    std::shared_ptr<protocol::TMValidation> const& packet)
{
    if (!val->isValid())
    {
        std::string const desc{"Validation forwarded by peer is invalid"};
        JLOG(pJournal_.debug()) << desc;
        charge(Resource::kFeeInvalidSignature, desc);
        return;
    }

    // FIXME it should be safe to remove this try/catch. Investigate codepaths.
    try
    {
        if (app_.getOPs().recvValidation(val, std::to_string(id())) || cluster())
        {
            // haveMessage contains peers, which are suppressed; i.e. the peers
            // are the source of the message, consequently the message should
            // not be relayed to these peers. But the message must be counted
            // as part of the squelch logic.
            auto haveMessage = overlay_.relay(*packet, key, val->getSignerPublic());
            if (!haveMessage.empty())
            {
                overlay_.updateSlotAndSquelch(
                    key, val->getSignerPublic(), std::move(haveMessage), protocol::mtVALIDATION);
            }
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(pJournal_.trace()) << "Exception processing validation: " << ex.what();
        using namespace std::string_literals;
        charge(Resource::kFeeMalformedRequest, "validation "s + ex.what());
    }
}

// Returns the set of peers that can help us get
// the TX tree with the specified root hash.
//
static std::shared_ptr<PeerImp>
getPeerWithTree(OverlayImpl& ov, uint256 const& rootHash, PeerImp const* skip)
{
    std::shared_ptr<PeerImp> ret;
    int retScore = 0;

    ov.forEach([&](std::shared_ptr<PeerImp>&& p) {
        if (p->hasTxSet(rootHash) && p.get() != skip)
        {
            auto score = p->getScore(true);
            if (!ret || (score > retScore))
            {
                ret = std::move(p);
                retScore = score;
            }
        }
    });

    return ret;
}

// Returns a random peer weighted by how likely to
// have the ledger and how responsive it is.
//
static std::shared_ptr<PeerImp>
getPeerWithLedger(
    OverlayImpl& ov,
    uint256 const& ledgerHash,
    LedgerIndex ledger,
    PeerImp const* skip)
{
    std::shared_ptr<PeerImp> ret;
    int retScore = 0;

    ov.forEach([&](std::shared_ptr<PeerImp>&& p) {
        if (p->hasLedger(ledgerHash, ledger) && p.get() != skip)
        {
            auto score = p->getScore(true);
            if (!ret || (score > retScore))
            {
                ret = std::move(p);
                retScore = score;
            }
        }
    });

    return ret;
}

void
PeerImp::sendLedgerBase(
    std::shared_ptr<Ledger const> const& ledger,
    protocol::TMLedgerData& ledgerData)
{
    JLOG(pJournal_.trace()) << "sendLedgerBase: Base data";

    Serializer s(sizeof(LedgerHeader));
    addRaw(ledger->header(), s);
    ledgerData.add_nodes()->set_nodedata(s.getDataPtr(), s.getLength());

    auto const& stateMap{ledger->stateMap()};
    if (stateMap.getHash() != beast::kZero)
    {
        // Return account state root node if possible
        Serializer root(768);

        stateMap.serializeRoot(root);
        ledgerData.add_nodes()->set_nodedata(root.getDataPtr(), root.getLength());

        if (ledger->header().txHash != beast::kZero)
        {
            auto const& txMap{ledger->txMap()};
            if (txMap.getHash() != beast::kZero)
            {
                // Return TX root node if possible
                root.erase();
                txMap.serializeRoot(root);
                ledgerData.add_nodes()->set_nodedata(root.getDataPtr(), root.getLength());
            }
        }
    }

    auto message{std::make_shared<Message>(ledgerData, protocol::mtLEDGER_DATA)};
    send(message);
}

std::shared_ptr<Ledger const>
PeerImp::getLedger(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    JLOG(pJournal_.trace()) << "getLedger: Ledger";

    std::shared_ptr<Ledger const> ledger;

    if (m->has_ledgerhash())
    {
        // Attempt to find ledger by hash
        uint256 const ledgerHash = uint256::fromRaw(m->ledgerhash());
        ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
        if (!ledger)
        {
            JLOG(pJournal_.trace()) << "getLedger: Don't have ledger with hash " << ledgerHash;

            if (m->has_querytype() && !m->has_requestcookie())
            {
                // Attempt to relay the request to a peer
                if (auto const peer = getPeerWithLedger(
                        overlay_, ledgerHash, m->has_ledgerseq() ? m->ledgerseq() : 0, this))
                {
                    m->set_requestcookie(id());
                    peer->send(std::make_shared<Message>(*m, protocol::mtGET_LEDGER));
                    JLOG(pJournal_.debug()) << "getLedger: Request relayed to peer";
                    return ledger;
                }

                JLOG(pJournal_.trace()) << "getLedger: Failed to find peer to relay request";
            }
        }
    }
    else if (m->has_ledgerseq())
    {
        // Attempt to find ledger by sequence
        if (m->ledgerseq() < app_.getLedgerMaster().getEarliestFetch())
        {
            JLOG(pJournal_.debug()) << "getLedger: Early ledger sequence request";
        }
        else
        {
            ledger = app_.getLedgerMaster().getLedgerBySeq(m->ledgerseq());
            if (!ledger)
            {
                JLOG(pJournal_.debug())
                    << "getLedger: Don't have ledger with sequence " << m->ledgerseq();
            }
        }
    }
    else if (m->has_ltype() && m->ltype() == protocol::ltCLOSED)
    {
        ledger = app_.getLedgerMaster().getClosedLedger();
    }

    if (ledger)
    {
        // Validate retrieved ledger sequence
        auto const ledgerSeq{ledger->header().seq};
        if (m->has_ledgerseq())
        {
            if (ledgerSeq != m->ledgerseq())
            {
                // Do not resource charge a peer responding to a relay
                if (!m->has_requestcookie())
                    charge(Resource::kFeeMalformedRequest, "get_ledger ledgerSeq");

                ledger.reset();
                JLOG(pJournal_.warn()) << "getLedger: Invalid ledger sequence " << ledgerSeq;
            }
        }
        else if (ledgerSeq < app_.getLedgerMaster().getEarliestFetch())
        {
            ledger.reset();
            JLOG(pJournal_.debug()) << "getLedger: Early ledger sequence request " << ledgerSeq;
        }
    }
    else
    {
        JLOG(pJournal_.debug()) << "getLedger: Unable to find ledger";
    }

    return ledger;
}

std::shared_ptr<SHAMap const>
PeerImp::getTxSet(std::shared_ptr<protocol::TMGetLedger> const& m) const
{
    JLOG(pJournal_.trace()) << "getTxSet: TX set";

    uint256 const txSetHash = uint256::fromRaw(m->ledgerhash());
    std::shared_ptr<SHAMap> shaMap{app_.getInboundTransactions().getSet(txSetHash, false)};
    if (!shaMap)
    {
        if (m->has_querytype() && !m->has_requestcookie())
        {
            // Attempt to relay the request to a peer
            if (auto const peer = getPeerWithTree(overlay_, txSetHash, this))
            {
                m->set_requestcookie(id());
                peer->send(std::make_shared<Message>(*m, protocol::mtGET_LEDGER));
                JLOG(pJournal_.debug()) << "getTxSet: Request relayed";
            }
            else
            {
                JLOG(pJournal_.debug()) << "getTxSet: Failed to find relay peer";
            }
        }
        else
        {
            JLOG(pJournal_.debug()) << "getTxSet: Failed to find TX set";
        }
    }

    return shaMap;
}

void
PeerImp::processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    // Do not resource charge a peer responding to a relay
    if (!m->has_requestcookie())
        charge(Resource::kFeeModerateBurdenPeer, "received a get ledger request");

    std::shared_ptr<Ledger const> ledger;
    std::shared_ptr<SHAMap const> sharedMap;
    SHAMap const* map{nullptr};
    protocol::TMLedgerData ledgerData;
    bool fatLeaves{true};
    auto const itype{m->itype()};

    if (itype == protocol::liTS_CANDIDATE)
    {
        if (sharedMap = getTxSet(m); !sharedMap)
            return;
        map = sharedMap.get();

        // Fill out the reply
        ledgerData.set_ledgerseq(0);
        ledgerData.set_ledgerhash(m->ledgerhash());
        ledgerData.set_type(protocol::liTS_CANDIDATE);
        if (m->has_requestcookie())
            ledgerData.set_requestcookie(m->requestcookie());

        // We'll already have most transactions
        fatLeaves = false;
    }
    else
    {
        if (sendQueue_.size() >= Tuning::kDropSendQueue)
        {
            JLOG(pJournal_.debug()) << "processLedgerRequest: Large send queue";
            return;
        }
        if (app_.getFeeTrack().isLoadedLocal() && !cluster())
        {
            JLOG(pJournal_.debug()) << "processLedgerRequest: Too busy";
            return;
        }

        if (ledger = getLedger(m); !ledger)
            return;

        // Fill out the reply
        auto const ledgerHash{ledger->header().hash};
        ledgerData.set_ledgerhash(ledgerHash.begin(), ledgerHash.size());
        ledgerData.set_ledgerseq(ledger->header().seq);
        ledgerData.set_type(itype);
        if (m->has_requestcookie())
            ledgerData.set_requestcookie(m->requestcookie());

        switch (itype)
        {
            case protocol::liBASE:
                sendLedgerBase(ledger, ledgerData);
                return;

            case protocol::liTX_NODE:
                map = &ledger->txMap();
                JLOG(pJournal_.trace())
                    << "processLedgerRequest: TX map hash " << to_string(map->getHash());
                break;

            case protocol::liAS_NODE:
                map = &ledger->stateMap();
                JLOG(pJournal_.trace())
                    << "processLedgerRequest: Account state map hash " << to_string(map->getHash());
                break;

            default:
                // This case should not be possible here
                JLOG(pJournal_.error()) << "processLedgerRequest: Invalid ledger info type";
                return;
        }
    }

    if (map == nullptr)
    {
        JLOG(pJournal_.warn()) << "processLedgerRequest: Unable to find map";
        return;
    }

    // Add requested node data to reply
    if (m->nodeids_size() > 0)
    {
        std::uint32_t const defaultDepth = isHighLatency() ? 2 : 1;
        auto const queryDepth{m->has_querydepth() ? m->querydepth() : defaultDepth};

        std::vector<std::pair<SHAMapNodeID, Blob>> data;

        for (int i = 0;
             i < m->nodeids_size() && ledgerData.nodes_size() < Tuning::kSoftMaxReplyNodes;
             ++i)
        {
            auto const shaMapNodeId{deserializeSHAMapNodeID(m->nodeids(i))};

            data.clear();
            data.reserve(Tuning::kSoftMaxReplyNodes);

            try
            {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access) nodeids checked in onGetLedger
                if (map->getNodeFat(*shaMapNodeId, data, fatLeaves, queryDepth))
                {
                    JLOG(pJournal_.trace())
                        << "processLedgerRequest: getNodeFat got " << data.size() << " nodes";

                    for (auto const& d : data)
                    {
                        if (ledgerData.nodes_size() >= Tuning::kHardMaxReplyNodes)
                            break;
                        protocol::TMLedgerNode* node{ledgerData.add_nodes()};
                        node->set_nodeid(d.first.getRawString());
                        node->set_nodedata(d.second.data(), d.second.size());
                    }
                }
                else
                {
                    JLOG(pJournal_.warn()) << "processLedgerRequest: getNodeFat returns false";
                }
            }
            catch (std::exception const& e)
            {
                std::string info;
                switch (itype)
                {
                    case protocol::liBASE:
                        // This case should not be possible here
                        info = "Ledger base";
                        break;

                    case protocol::liTX_NODE:
                        info = "TX node";
                        break;

                    case protocol::liAS_NODE:
                        info = "AS node";
                        break;

                    case protocol::liTS_CANDIDATE:
                        info = "TS candidate";
                        break;

                    default:
                        info = "Invalid";
                        break;
                }

                if (!m->has_ledgerhash())
                    info += ", no hash specified";

                JLOG(pJournal_.warn())
                    << "processLedgerRequest: getNodeFat with nodeId " << *shaMapNodeId
                    << " and ledger info type " << info << " throws exception: " << e.what();
            }
        }

        JLOG(pJournal_.info()) << "processLedgerRequest: Got request for " << m->nodeids_size()
                               << " nodes at depth " << queryDepth << ", return "
                               << ledgerData.nodes_size() << " nodes";
    }

    if (ledgerData.nodes_size() == 0)
        return;

    send(std::make_shared<Message>(ledgerData, protocol::mtLEDGER_DATA));
}

int
PeerImp::getScore(bool haveItem) const
{
    // Random component of score, used to break ties and avoid
    // overloading the "best" peer
    static int const kSpRandomMax = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static int const kSpHaveItem = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static int const kSpLatency = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static int const kSpNoLatency = 8000;

    int score = randInt(kSpRandomMax);

    if (haveItem)
        score += kSpHaveItem;

    std::optional<std::chrono::milliseconds> latency;
    {
        std::scoped_lock const sl(recentLock_);
        latency = latency_;
    }

    if (latency)
    {
        score -= latency->count() * kSpLatency;
    }
    else
    {
        score -= kSpNoLatency;
    }

    return score;
}

bool
PeerImp::isHighLatency() const
{
    std::scoped_lock const sl(recentLock_);
    return latency_ >= kPeerHighLatency;
}

void
PeerImp::Metrics::addMessage(std::uint64_t bytes)
{
    using namespace std::chrono_literals;
    std::unique_lock const lock{mutex_};

    totalBytes_ += bytes;
    accumBytes_ += bytes;
    auto const timeElapsed = clock_type::now() - intervalStart_;
    auto const timeElapsedInSecs = std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avgBytes = accumBytes_ / timeElapsedInSecs.count();
        rollingAvg_.push_back(avgBytes);

        auto const totalBytes = std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0ull);
        rollingAvgBytes_ = totalBytes / rollingAvg_.size();

        intervalStart_ = clock_type::now();
        accumBytes_ = 0;
    }
}

std::uint64_t
PeerImp::Metrics::averageBytes() const
{
    std::shared_lock const lock{mutex_};
    return rollingAvgBytes_;
}

std::uint64_t
PeerImp::Metrics::totalBytes() const
{
    std::shared_lock const lock{mutex_};
    return totalBytes_;
}

}  // namespace xrpl
