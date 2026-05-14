/** @file
 *  Implements `PeerImp`, the concrete per-connection object for the XRPL P2P
 *  overlay.
 *
 *  Each `PeerImp` instance owns exactly one live TLS-over-TCP session. It
 *  drives the full connection lifecycle — TLS handshake acceptance, binary
 *  wire-protocol framing, message dispatch, resource accounting, and
 *  graceful shutdown — serialising all mutable state through a single
 *  `boost::asio::strand`.
 *
 *  The three major concerns handled here are:
 *  - **Async I/O**: read/write loops, timer management, shutdown state machine.
 *  - **Protocol dispatch**: `invokeProtocolMessage` fans out to typed
 *    `onMessage()` overloads; `onMessageBegin`/`onMessageEnd` bracket each
 *    dispatch with resource-charge bookkeeping.
 *  - **Overlay accounting**: peer tracking (converged/diverged/unknown),
 *    squelch enforcement, TX reduce-relay queue, and PeerFinder slot
 *    management.
 */
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
std::chrono::milliseconds constexpr kPEER_HIGH_LATENCY{300};

/** How often we PING the peer to check for latency and sendq probe */
std::chrono::seconds constexpr kPEER_TIMER_INTERVAL{60};

/** The timeout for a shutdown timer */
std::chrono::seconds constexpr kSHUTDOWN_TIMER_INTERVAL{5};

}  // namespace

// TODO: Remove this exclusion once unit tests are added after the hotfix
// release.

/** Construct an inbound `PeerImp` from an already-completed TLS handshake.
 *
 *  Called by `OverlayImpl::onHandoff` after the HTTP upgrade succeeds.  The
 *  caller has already validated the `publicKey` and negotiated `protocol`; the
 *  constructor records the negotiated capabilities (LZ4 compression,
 *  TX reduce-relay, ledger replay) by parsing the `X-Protocol-Ctl` header via
 *  `peerFeatureEnabled`.  A structured `fingerprint_` (remote IP + public key
 *  + numeric ID) is computed and prepended to every log line emitted by this
 *  peer's journals.
 *
 *  @param app        Application context owning shared subsystems.
 *  @param id         Monotonically assigned numeric peer ID.
 *  @param slot       PeerFinder slot already allocated for this connection.
 *  @param request    HTTP upgrade request (headers are retained for feature
 *      negotiation and diagnostic JSON).
 *  @param publicKey  Verified ephemeral node public key from handshake.
 *  @param protocol   Negotiated XRPL wire-protocol version.
 *  @param consumer   Pre-created resource consumer for rate-limiting.
 *  @param streamPtr  Ownership of the established TLS stream.
 *  @param overlay    Owning overlay manager.
 */
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
    , fee_{.fee = Resource::kFEE_TRIVIAL_PEER, .context = ""}
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_COMPR, "lz4", app_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
    , txReduceRelayEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_TXRR, app_.config().TX_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_LEDGER_REPLAY, app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.info()) << "compression enabled " << (compressionEnabled_ == Compressed::On)
                          << " vp reduce-relay base squelch enabled "
                          << peerFeatureEnabled(
                                 headers_,
                                 kFEATURE_VPRR,
                                 app_.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
                          << " tx reduce-relay enabled " << txReduceRelayEnabled_;
}

/** Release overlay registrations and PeerFinder slot.
 *
 *  Runs the mandatory teardown chain: `deletePeer` → `onPeerDeactivate` →
 *  `peerFinder().onClosed` → `overlay_.remove`.  Cluster membership is
 *  snapshotted before the first call because `deletePeer` may remove the peer
 *  from the cluster registry before we can log.
 *
 *  @note The destructor does NOT close the socket; `close()` must have been
 *      called before this point via the normal shutdown state machine.
 */
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

/** Return true if `pBuffStr` has exactly 32 bytes (the size of a `uint256`).
 *
 *  Protobuf represents hash fields as `bytes` (mapped to `std::string` in
 *  C++).  Callers must validate the field width before casting to `uint256`
 *  to prevent silent truncation or out-of-bounds reads.  Every raw-byte
 *  protobuf field that holds a hash is guarded by this check before the
 *  `uint256{field}` conversion.
 *
 *  @param pBuffStr  Raw bytes from a protobuf `bytes` field.
 *  @return True if the byte length exactly matches `uint256::size()` (32).
 */
static bool
stringIsUInt256Sized(std::string const& pBuffStr)
{
    return pBuffStr.size() == uint256::size();
}

/** Begin the post-handshake protocol session on the strand.
 *
 *  Parses the optional `Closed-Ledger` and `Previous-Ledger` HTTP headers
 *  (hex or base64-encoded `uint256` values) to seed `closedLedgerHash_` and
 *  `previousLedgerHash_` before protocol messages arrive.  Malformed header
 *  values trigger `fail()` immediately.  After storing the hashes, delegates
 *  to `doAccept()` (inbound) or `doProtocolStart()` (outbound).
 *
 *  @note Always self-posts to the strand; safe to call from any thread.
 */
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

/** Initiate graceful shutdown of this peer connection.
 *
 *  Idempotent: if the socket is already closed the call is a no-op.
 *  Always self-posts to the strand so it is safe to call from any thread.
 */
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

/** Enqueue a protocol message for delivery to the remote peer.
 *
 *  Before enqueuing, checks the squelch table: if the message carries a
 *  validator key that is currently squelched, the bytes are counted under
 *  `SquelchSuppressed` and the message is dropped silently.  If a shutdown
 *  is in progress the method instead nudges `tryAsyncShutdown()`.
 *
 *  Outbound traffic (by category and total) is reported to the overlay
 *  counters.  The send queue is bounded by `kTARGET_SEND_QUEUE`; exceeding
 *  it increments `largeSendq_` which the periodic timer uses to disconnect
 *  stalled peers.  Only the first enqueue while the queue is empty starts a
 *  new `async_write`; subsequent calls simply push to the queue and return,
 *  relying on `onWriteMessage` to chain the next write.
 *
 *  @param m  Message to deliver; must not be null.
 *  @note Always self-posts to the strand; safe to call from any thread.
 */
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

    if (sendqSize < Tuning::kTARGET_SEND_QUEUE)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        largeSendq_ = 0;
    }
    else if (auto sink = journal_.debug(); sink && (sendqSize % Tuning::kSEND_QUEUE_LOG_FREQ) == 0)
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

/** Flush the pending TX-hash queue as a single `TMHaveTransactions` message.
 *
 *  Used by the TX reduce-relay path: hashes accumulated via `addTxQueue` are
 *  batched here and sent in one protobuf message, then the queue is cleared.
 *  A no-op when `txQueue_` is empty.
 *
 *  @note Always self-posts to the strand; safe to call from any thread.
 */
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

/** Add a transaction hash to the outbound TX-hash queue.
 *
 *  Part of the TX reduce-relay path: instead of sending the full transaction
 *  to every peer, the overlay sends hash announcements.  If the queue has
 *  reached `reduce_relay::kMAX_TX_QUEUE_SIZE`, it is flushed immediately
 *  before inserting the new hash to stay within the 64 MiB wire limit.
 *
 *  @param hash  Transaction ID to announce to this peer.
 *  @note Always self-posts to the strand; safe to call from any thread.
 */
void
PeerImp::addTxQueue(uint256 const& hash)
{
    if (!strand_.running_in_this_thread())
    {
        post(strand_, std::bind(&PeerImp::addTxQueue, shared_from_this(), hash));
        return;
    }

    if (txQueue_.size() == reduce_relay::kMAX_TX_QUEUE_SIZE)
    {
        JLOG(pJournal_.warn()) << "addTxQueue exceeds the cap";
        sendTxQueue();
    }

    txQueue_.insert(hash);
    JLOG(pJournal_.trace()) << "addTxQueue " << txQueue_.size();
}

/** Remove a transaction hash from the outbound TX-hash queue if present.
 *
 *  Called when the server learns the peer has already seen a transaction
 *  (e.g., the peer sent us the full tx, or we observed a duplicate).
 *  Prevents sending a redundant hash announcement.
 *
 *  @param hash  Transaction ID to remove.
 *  @note Always self-posts to the strand; safe to call from any thread.
 */
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

/** Apply a resource charge to this peer and disconnect if the balance is exceeded.
 *
 *  Charges are accumulated in the `Resource::Consumer` balance.  When the
 *  balance crosses the drop threshold, `usage_.disconnect()` logs the reason
 *  and this method severs the connection via `fail()`.  The method must be
 *  called on the strand because `fail()` requires strand context.
 *
 *  @param fee      Charge level to apply (e.g. `kFEE_INVALID_SIGNATURE`).
 *  @param context  Human-readable label used in disconnect log messages.
 */
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

/** Return true if this peer consented to being listed in crawl results.
 *
 *  The peer signals consent by including `Crawl: public` in the HTTP upgrade
 *  request or response headers.  The comparison is case-insensitive.
 *
 *  @return True when the `Crawl` header value equals "public".
 */
bool
PeerImp::crawl() const
{
    auto const iter = headers_.find("Crawl");
    if (iter == headers_.end())
        return false;
    return boost::iequals(iter->value(), "public");
}

/** Return true if this peer's public key appears in the cluster registry.
 *
 *  @return True when the peer belongs to the operator's trusted cluster.
 */
bool
PeerImp::cluster() const
{
    return static_cast<bool>(app_.getCluster().member(publicKey_));
}

/** Return the remote peer's software version string.
 *
 *  For inbound connections the version is taken from the `User-Agent` request
 *  header; for outbound connections from the `Server` response header.
 *
 *  @return Version string, or an empty string if the header is absent.
 */
std::string
PeerImp::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"];
    return headers_["Server"];
}

/** Serialize peer diagnostics to a JSON object.
 *
 *  Collects identity (public key, address, cluster name, domain), connection
 *  metadata (protocol version, network ID, uptime, inbound flag), resource
 *  load balance, current latency estimate, completed-ledger range, tracking
 *  state, last status change, and rolling I/O byte metrics.  Called by the
 *  `peers` RPC method and the `/crawl` HTTP endpoint.
 *
 *  @note Acquires `recentLock_` briefly to snapshot mutable fields.
 *  @return JSON object containing all peer diagnostic fields.
 */
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

    if (closedLedgerHash != beast::kZERO)
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

/** Return true if this peer supports the requested protocol feature.
 *
 *  `ValidatorListPropagation` requires protocol ≥ 2.1;
 *  `ValidatorList2Propagation` requires ≥ 2.2;
 *  `LedgerReplay` is controlled by the `ledgerReplayEnabled_` flag negotiated
 *  at construction from the `X-Protocol-Ctl` header.
 *
 *  @param f  The feature to query.
 *  @return True if the negotiated session supports `f`.
 */
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

/** Return true if the peer is believed to hold a specific ledger.
 *
 *  Two evidence paths: (1) the requested sequence falls within the peer's
 *  advertised `[minLedger_, maxLedger_]` range **and** the peer is
 *  `Converged`; or (2) the hash appears in `recentLedgers_` (short history
 *  populated by `TMStatusChange` and `addLedger`).  Using sequence alone is
 *  insufficient because a diverged peer's range is unreliable.
 *
 *  @param hash  Ledger hash to check.
 *  @param seq   Ledger sequence (0 to skip sequence-range check).
 *  @return True if either evidence path confirms ledger availability.
 *  @note Acquires `recentLock_`.
 */
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

/** Copy the peer's advertised completed-ledger sequence range.
 *
 *  @param minSeq  Out-parameter set to `minLedger_`.
 *  @param maxSeq  Out-parameter set to `maxLedger_`.
 *  @note Acquires `recentLock_`.
 */
void
PeerImp::ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const
{
    std::scoped_lock const sl(recentLock_);

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

/** Return true if the peer has announced possession of a transaction set.
 *
 *  @param hash  SHAMap root hash of the candidate transaction set.
 *  @return True if `hash` appears in `recentTxSets_`.
 *  @note Acquires `recentLock_`.
 */
bool
PeerImp::hasTxSet(uint256 const& hash) const
{
    std::scoped_lock const sl(recentLock_);
    return std::ranges::find(recentTxSets_, hash) != recentTxSets_.end();
}

/** Advance ledger-hash state when the locally validated ledger changes.
 *
 *  Called by the overlay when the local node closes a ledger.  Rotates the
 *  current `closedLedgerHash_` into `previousLedgerHash_` and zeros the
 *  current hash so the peer's next `TMStatusChange` can fill it in fresh.
 *
 *  @note Acquires `recentLock_`.
 */
void
PeerImp::cycleStatus()
{
    // Operations on closedLedgerHash_ and previousLedgerHash_ must be
    // guarded by recentLock_.
    std::scoped_lock const sl(recentLock_);
    previousLedgerHash_ = closedLedgerHash_;
    closedLedgerHash_.zero();
}

/** Return true if the peer's advertised ledger range fully covers [uMin, uMax].
 *
 *  Deliberately returns false for diverged peers even when the sequence range
 *  matches, because a diverged peer's advertised ledgers may be on a different
 *  fork.
 *
 *  @param uMin  Lower bound of the requested sequence range (inclusive).
 *  @param uMax  Upper bound of the requested sequence range (inclusive).
 *  @return True if the peer is not diverged and holds the full range.
 *  @note Acquires `recentLock_`.
 */
bool
PeerImp::hasRange(std::uint32_t uMin, std::uint32_t uMax)
{
    std::scoped_lock const sl(recentLock_);
    return (tracking_ != Tracking::Diverged) && (uMin >= minLedger_) && (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

/** Log an I/O error and initiate shutdown.
 *
 *  @param name  Label used as a prefix in the warning log line.
 *  @param ec    Error code whose message is appended to the log line.
 *  @note Must be called on the strand.
 */
void
PeerImp::fail(std::string const& name, error_code ec)
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::PeerImp::fail : strand in this thread");

    if (!socket_.is_open())
        return;

    JLOG(journal_.warn()) << name << ": " << ec.message();

    shutdown();
}

/** Log a protocol error reason string and initiate shutdown.
 *
 *  Avoids calling `name()` (which locks `nameMutex_`) unless the warning
 *  severity is actually active.  Always self-posts to the strand; safe to
 *  call from any thread.
 *
 *  @param reason  Human-readable description of the failure (logged at Warning).
 */
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

/** Start the SSL graceful-shutdown handshake if no async I/O is in flight.
 *
 *  The SSL `async_shutdown` must NOT be called while `readPending_` or
 *  `writePending_` are set — doing so would invoke async operations on the
 *  same stream concurrently, which is undefined behaviour.  This method is
 *  idempotent (guarded by `shutdownStarted_`) and arms the 5-second safety
 *  timer before launching the SSL handshake.
 *
 *  @note Must be called on the strand.
 */
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

    setTimer(kSHUTDOWN_TIMER_INTERVAL);

    // gracefully shutdown the SSL socket, performing a shutdown handshake
    stream_.async_shutdown(bind_executor(
        strand_, std::bind(&PeerImp::onShutdown, shared_from_this(), std::placeholders::_1)));
}

/** Set the shutdown flag, cancel pending I/O, and attempt SSL close.
 *
 *  Idempotent: subsequent calls while `shutdown_` is already set are no-ops.
 *  Cancels all pending async operations on the lowest TLS layer so that
 *  `onReadMessage` and `onWriteMessage` receive `operation_aborted` and clear
 *  their pending flags, allowing `tryAsyncShutdown` to proceed.
 *
 *  @note Must be called on the strand.
 */
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

/** Handle completion of the SSL `async_shutdown` call.
 *
 *  Several error codes are benign and suppressed:
 *  - `eof` — the remote peer closed cleanly.
 *  - `operation_aborted` — the 5-second safety timer expired.
 *  - "application data after close notify" — the peer sent data after
 *    initiating their own TLS close; harmless.
 *  - `broken_pipe`/`stream_truncated` — the TCP connection dropped before the
 *    TLS handshake completed; the peer is already gone.
 *
 *  After handling the error the raw socket is unconditionally closed via
 *  `close()`.
 *
 *  @param ec  Completion error code from `stream_.async_shutdown`.
 */
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

/** Close the underlying TCP socket and record the disconnect.
 *
 *  Final step of the shutdown state machine.  Errors from `socket_.close()`
 *  are intentionally ignored — the socket is already being torn down.
 *  Inbound disconnects are logged at Debug; outbound at Info (outbound
 *  connections are under our control, so disconnects are more significant).
 *
 *  @note Must be called on the strand.
 */
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

/** Arm the async timer to fire after `interval`.
 *
 *  Shared by the periodic ping timer (`kPEER_TIMER_INTERVAL`) and the
 *  shutdown-safety timer (`kSHUTDOWN_TIMER_INTERVAL`).  Any exception from
 *  `expires_after` (rare; typically EINVAL on an already-closed socket)
 *  triggers `shutdown()`.
 *
 *  @param interval  Duration after which `onTimer` will be invoked on the strand.
 */
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

/** Format a log-line prefix from a peer fingerprint.
 *
 *  Wraps the fingerprint in square brackets and appends a space so log lines
 *  from different peers are visually distinguished in aggregated logs.
 *
 *  @param fingerprint  Short identifier string (remote IP + pubkey + ID).
 *  @return Prefix string of the form `"[fingerprint] "`.
 */
std::string
PeerImp::makePrefix(std::string const& fingerprint)
{
    std::stringstream ss;
    ss << "[" << fingerprint << "] ";
    return ss.str();
}

/** Handle the shared async timer expiry.
 *
 *  Serves two roles depending on context:
 *  1. **Shutdown safety**: if `shutdown_` is set, the SSL teardown has stalled
 *     past `kSHUTDOWN_TIMER_INTERVAL`; force `close()`.
 *  2. **Periodic health**: if not shutting down, the timer fires every
 *     `kPEER_TIMER_INTERVAL` to (a) disconnect peers with chronically large
 *     send queues, (b) disconnect outbound peers that have stayed non-converged
 *     beyond `MAX_DIVERGED_TIME` or `MAX_UNKNOWN_TIME`, and (c) send a
 *     `TMPing` with a random cookie and re-arm the timer.  An unanswered ping
 *     cookie from the previous interval causes `fail("Ping Timeout")`.
 *
 *  @param ec  Completion error code; `operation_aborted` means the timer was
 *      cancelled and is silently ignored.
 *  @note Must be called on the strand.
 */
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

    if (largeSendq_++ >= Tuning::kSENDQ_INTERVALS)
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

    setTimer(kPEER_TIMER_INTERVAL);
}

/** Cancel the async timer, swallowing any exception.
 *
 *  `noexcept` wrapper around `timer_.cancel()`.  Called during both normal
 *  shutdown and the SSL-shutdown completion to ensure the timer handler does
 *  not fire after the socket is closed.
 */
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
/** Complete the inbound-peer handshake and write the HTTP 101 response.
 *
 *  Re-derives the TLS shared value from the already-established stream (the
 *  value was previously computed in `OverlayImpl::onHandoff`; a second
 *  derivation here is the authoritative check).  On success activates this
 *  peer in the overlay (`overlay_.activate`), writes the HTTP upgrade
 *  response, then calls `doProtocolStart` on completion.  An in-progress
 *  shutdown detected before writing causes an early `tryAsyncShutdown`.
 *
 *  @note Must be called on the strand.
 */
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

/** Return the cluster-assigned name for this peer, or an empty string.
 *
 *  Protected by `nameMutex_` (shared_mutex) so readers do not block each
 *  other; writers hold a unique lock when updating from `TMCluster` gossip.
 *
 *  @return Cluster name if the peer is a known cluster member; otherwise `""`.
 */
std::string
PeerImp::name() const
{
    std::shared_lock const readLock{nameMutex_};
    return name_;
}

/** Return the server domain advertised by the peer in its handshake headers.
 *
 *  @return Value of the `Server-Domain` HTTP header, or `""` if absent.
 */
std::string
PeerImp::domain() const
{
    return headers_["Server-Domain"];
}

//------------------------------------------------------------------------------

// Protocol logic

/** Start the XRPL binary wire-protocol session.
 *
 *  Calls `onReadMessage` once to kick off the continuous read loop, then
 *  pushes pending state to the peer: for inbound connections that support
 *  `ValidatorListPropagation`, all currently-known validator lists are sent
 *  (and immediately suppressed in the hash router so they are not re-sent on
 *  the next refresh); finally the manifests message is sent and the periodic
 *  ping timer is armed.
 *
 *  @note A shutdown detected before starting is handled by `tryAsyncShutdown`.
 */
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

    setTimer(kPEER_TIMER_INTERVAL);
}

/** Async-read completion handler and message dispatch loop.
 *
 *  Runs on the strand.  On each invocation, new bytes are committed to
 *  `readBuffer_` and `invokeProtocolMessage` is called in a loop until the
 *  buffer is exhausted or a partial header is encountered (`bytesConsumed==0`).
 *  Each `invokeProtocolMessage` call may itself invoke multiple `onMessage`
 *  overloads.  After draining the buffer, another `async_read_some` is posted
 *  and `readPending_` is set to prevent concurrent reads.
 *
 *  Shutdown is checked both after the I/O completion (early abort) and after
 *  the dispatch loop (deferred abort): if `shutdown_` is set, dispatch is
 *  skipped and `tryAsyncShutdown` is called instead of re-posting a read.
 *
 *  @param ec                Error code from `async_read_some`; `eof` triggers
 *      graceful shutdown, `operation_aborted` defers to `tryAsyncShutdown`.
 *  @param bytesTransferred  Number of bytes placed in `readBuffer_`.
 *  @note Must be called on the strand.
 */
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

    auto hint = Tuning::kREAD_BUFFER_BYTES;

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
        readBuffer_.prepare(std::max(Tuning::kREAD_BUFFER_BYTES, hint)),
        bind_executor(
            strand_,
            std::bind(
                &PeerImp::onReadMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

/** Async-write completion handler; chains the next write if the queue is non-empty.
 *
 *  Runs on the strand.  Pops the just-sent message from `sendQueue_` and, if
 *  more messages are waiting, immediately posts another `async_write`.  If
 *  `shutdown_` is set after clearing `writePending_`, delegates to
 *  `tryAsyncShutdown` (which may now be able to start SSL teardown).
 *
 *  @param ec                Error code from `async_write`.
 *  @param bytesTransferred  Bytes confirmed sent; passed to `metrics_.sent`.
 *  @note Must be called on the strand.
 */
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

/** Handle an unrecognised protobuf message type.
 *
 *  Currently a no-op; future versions may charge the peer for sending
 *  unknown message types or log at trace.
 *
 *  @param type  Numeric protobuf message type identifier.
 */
void
PeerImp::onMessageUnknown(std::uint16_t type)
{
    // TODO
}

/** Set up per-message resource accounting before dispatching to `onMessage`.
 *
 *  Creates a load-event for the job queue profiler, resets `fee_` to the
 *  baseline trivial charge, records inbound byte counts (both total and
 *  per-category), and, for transaction-related messages when TX metrics are
 *  enabled, forwards byte counts to the overlay's `TxMetrics` aggregator.
 *
 *  @param type              Numeric protobuf message type.
 *  @param m                 Decoded protobuf message object.
 *  @param size              Wire byte size (may be compressed).
 *  @param uncompressedSize  Uncompressed byte size.
 *  @param isCompressed      Whether the message arrived LZ4-compressed.
 */
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
    fee_ = {.fee = Resource::kFEE_TRIVIAL_PEER, .context = name};

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

/** Apply the accumulated resource charge after message dispatch completes.
 *
 *  Releases the job-queue load event and applies `fee_` (possibly escalated
 *  by the handler via `fee_.update()`) through `charge()`.  If the charge
 *  exceeds the drop threshold, `charge()` calls `fail()` and the connection
 *  is severed.
 */
void
PeerImp::onMessageEnd(std::uint16_t, std::shared_ptr<::google::protobuf::Message> const&)
{
    loadEvent_.reset();
    charge(fee_.fee, fee_.context);
}

/** Handle an incoming `TMManifests` message.
 *
 *  Empty lists are immediately rejected as useless data.  Oversized lists
 *  (>100 entries) are still processed but incur a moderate-burden charge.
 *  Processing is deferred to a `JtManifest` job so signature verification
 *  does not block the network strand.
 *
 *  @param m  Parsed `TMManifests` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMManifests> const& m)
{
    auto const s = m->list_size();

    if (s == 0)
    {
        fee_.update(Resource::kFEE_USELESS_DATA, "empty");
        return;
    }

    if (s > 100)
        fee_.update(Resource::kFEE_MODERATE_BURDEN_PEER, "oversize");

    app_.getJobQueue().addJob(JtManifest, "RcvManifests", [this, that = shared_from_this(), m]() {
        overlay_.onManifests(m, that);
    });
}

/** Handle an incoming `TMPing` message (ping request or pong reply).
 *
 *  **Ping request (`ptPING`)**: Immediately echoes the message back as a pong.
 *  Charges moderate-burden to limit ping flooding.
 *
 *  **Pong reply (`ptPONG`)**: Validates the `seq` cookie against `lastPingSeq_`
 *  — only matching cookies clear the pending-ping state, so peers that spoof
 *  pong cookies will eventually time out.  On a valid pong, RTT is measured and
 *  blended into `latency_` with an 8-factor EWMA:
 *  `latency = (latency * 7 + rtt) / 8`.
 *
 *  @param m  Parsed `TMPing` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMPing> const& m)
{
    if (m->type() == protocol::TMPing::ptPING)
    {
        // We have received a ping request, reply with a pong
        fee_.update(Resource::kFEE_MODERATE_BURDEN_PEER, "ping request");
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

/** Handle an incoming `TMCluster` gossip message.
 *
 *  Only accepted from peers that are themselves cluster members; non-cluster
 *  senders incur a useless-data charge.  Valid messages update the cluster
 *  registry with load and status data for each node in the list, import
 *  resource gossip into the resource manager, then compute the median cluster
 *  load fee (from nodes active within the past 90 seconds) and push it to
 *  `FeeTrack`.
 *
 *  @param m  Parsed `TMCluster` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMCluster> const& m)
{
    // VFALCO NOTE I think we should drop the peer immediately
    if (!cluster())
    {
        fee_.update(Resource::kFEE_USELESS_DATA, "unknown cluster");
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

/** Handle an incoming `TMEndpoints` peer-address gossip message.
 *
 *  Only processed from converged peers using protocol version 2.  Messages
 *  with ≥ 1024 entries are rejected as useless data.  Each entry is parsed
 *  via `IP::Endpoint::fromStringChecked`; malformed entries accumulate a
 *  per-entry `kFEE_INVALID_DATA` charge but do not abort processing of the
 *  remaining valid entries.  For zero-hop entries, the socket's remote address
 *  overrides the advertised IP so peers do not need to know their own public
 *  IP.  Valid entries are forwarded to `PeerFinder::onEndpoints`.
 *
 *  @param m  Parsed `TMEndpoints` protobuf message.
 */
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
        fee_.update(Resource::kFEE_USELESS_DATA, "endpoints too large");
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
            Resource::kFEE_INVALID_DATA * malformed,
            std::to_string(malformed) + " malformed endpoints");
    }

    if (!endpoints.empty())
        overlay_.peerFinder().onEndpoints(slot_, endpoints);
}

/** Handle an incoming `TMTransaction` (unsolicited full transaction).
 *
 *  Delegates to `handleTransaction` with `eraseTxQueue=true` (the peer sent a
 *  full tx so any matching pending hash in our TX queue can be removed) and
 *  `batch=false` (single transaction, not a batch reply).
 *
 *  @param m  Parsed `TMTransaction` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransaction> const& m)
{
    handleTransaction(m, true, false);
}

/** Core transaction receive handler shared by `onMessage(TMTransaction)` and
 *  `onMessage(TMTransactions)`.
 *
 *  Guards: diverged-peer early-return, not-yet-synced check.  After parsing
 *  the raw transaction, checks `HashRouter::shouldProcess` to suppress
 *  duplicate processing.  Transactions with `tfInnerBatchTxn` are rejected
 *  regardless of amendment state (they must not be relayed).  Cluster peers'
 *  transactions skip local signature validation when the `deferred` flag is
 *  clear (i.e. the cluster peer already validated locally).  CPU-heavy
 *  signature verification and submission are posted to the `JtTransaction`
 *  job queue via a `weak_ptr` capture so the job silently no-ops if the peer
 *  is torn down before the job runs.
 *
 *  @param m             The raw `TMTransaction` message.
 *  @param eraseTxQueue  If true, remove `txID` from the outbound TX-hash queue
 *      when a duplicate is detected (the peer already has the tx).
 *  @param batch         True when called from `onMessage(TMTransactions)`;
 *      suppresses the pseudo-tx charge that applies to unsolicited singles.
 *  @note `eraseTxQueue` and `batch` must not both be true.
 */
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
            fee_.update(Resource::kFEE_MODERATE_BURDEN_PEER, "inner batch txn");
            return;
        }
        // LCOV_EXCL_STOP

        HashRouterFlags flags = HashRouterFlags::UNDEFINED;
        constexpr std::chrono::seconds kTX_INTERVAL = 10s;

        if (!app_.getHashRouter().shouldProcess(txID, id_, flags, kTX_INTERVAL))
        {
            // we have seen this transaction recently
            if (any(flags & HashRouterFlags::BAD))
            {
                fee_.update(Resource::kFEE_USELESS_DATA, "known bad");
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

/** Handle an incoming `TMGetLedger` ledger-data request.
 *
 *  Validates all request fields before touching any application state:
 *  ledger info type, ledger type, ledger hash and sequence (bounds-checked
 *  against the validated ledger), node IDs (deserialized as `SHAMapNodeID`),
 *  query type, and query depth.  Any validation failure charges `kFEE_INVALID_DATA`
 *  and returns.  Accepted requests are dispatched to a `JtLedgerReq` job
 *  calling `processLedgerRequest`.
 *
 *  @param m  Parsed `TMGetLedger` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::kFEE_INVALID_DATA, "get_ledger " + msg);
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
        if (m->querydepth() > Tuning::kMAX_QUERY_DEPTH || itype == protocol::liBASE)
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

/** Handle an incoming `TMProofPathRequest` (ledger-replay proof-path query).
 *
 *  Rejected with `kFEE_MALFORMED_REQUEST` if `ledgerReplayEnabled_` is false
 *  for this session.  Accepted requests are dispatched to a `JtReplayReq` job;
 *  the job charges `reBAD_REQUEST` or `reNO_REPLY` on error, or sends a
 *  `TMProofPathResponse` on success.
 *
 *  @param m  Parsed `TMProofPathRequest` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m)
{
    JLOG(pJournal_.trace()) << "onMessage, TMProofPathRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "proof_path_request disabled");
        return;
    }

    fee_.update(Resource::kFEE_MODERATE_BURDEN_PEER, "received a proof path request");
    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtReplayReq, "RcvProofPReq", [weak, m]() {
        if (auto peer = weak.lock())
        {
            auto reply = peer->ledgerReplayMsgHandler_.processProofPathRequest(m);
            if (reply.has_error())
            {
                if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                {
                    peer->charge(Resource::kFEE_MALFORMED_REQUEST, "proof_path_request");
                }
                else
                {
                    peer->charge(Resource::kFEE_REQUEST_NO_REPLY, "proof_path_request");
                }
            }
            else
            {
                peer->send(std::make_shared<Message>(reply, protocol::mtPROOF_PATH_RESPONSE));
            }
        }
    });
}

/** Handle an incoming `TMProofPathResponse` (ledger-replay proof-path reply).
 *
 *  Rejected with `kFEE_MALFORMED_REQUEST` if `ledgerReplayEnabled_` is false.
 *  Otherwise forwarded to `ledgerReplayMsgHandler_`; an invalid response
 *  charges `kFEE_INVALID_DATA`.
 *
 *  @param m  Parsed `TMProofPathResponse` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "proof_path_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processProofPathResponse(m))
    {
        fee_.update(Resource::kFEE_INVALID_DATA, "proof_path_response");
    }
}

/** Handle an incoming `TMReplayDeltaRequest` (ledger-replay delta query).
 *
 *  Rejected with `kFEE_MALFORMED_REQUEST` if `ledgerReplayEnabled_` is false.
 *  Accepted requests are dispatched to a `JtReplayReq` job; the reply is sent
 *  as a `TMReplayDeltaResponse`.
 *
 *  @param m  Parsed `TMReplayDeltaRequest` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m)
{
    JLOG(pJournal_.trace()) << "onMessage, TMReplayDeltaRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "replay_delta_request disabled");
        return;
    }

    fee_.fee = Resource::kFEE_MODERATE_BURDEN_PEER;
    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtReplayReq, "RcvReplDReq", [weak, m]() {
        if (auto peer = weak.lock())
        {
            auto reply = peer->ledgerReplayMsgHandler_.processReplayDeltaRequest(m);
            if (reply.has_error())
            {
                if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                {
                    peer->charge(Resource::kFEE_MALFORMED_REQUEST, "replay_delta_request");
                }
                else
                {
                    peer->charge(Resource::kFEE_REQUEST_NO_REPLY, "replay_delta_request");
                }
            }
            else
            {
                peer->send(std::make_shared<Message>(reply, protocol::mtREPLAY_DELTA_RESPONSE));
            }
        }
    });
}

/** Handle an incoming `TMReplayDeltaResponse` (ledger-replay delta reply).
 *
 *  Rejected with `kFEE_MALFORMED_REQUEST` if `ledgerReplayEnabled_` is false.
 *  Otherwise forwarded to `ledgerReplayMsgHandler_`; an invalid response
 *  charges `kFEE_INVALID_DATA`.
 *
 *  @param m  Parsed `TMReplayDeltaResponse` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "replay_delta_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processReplayDeltaResponse(m))
    {
        fee_.update(Resource::kFEE_INVALID_DATA, "replay_delta_response");
    }
}

/** Handle an incoming `TMLedgerData` ledger-data reply.
 *
 *  Validates: ledger hash size, ledger sequence bounds (against current
 *  validated index), ledger info type, reply error code range, and node
 *  count (must be in `(0, kHARD_MAX_REPLY_NODES]`).  Messages with a
 *  `requestcookie` are forwarded to the originating peer and not processed
 *  locally.  TX-candidate sets (`liTS_CANDIDATE`) are dispatched to
 *  `InboundTransactions::gotData`; ordinary ledger data goes to
 *  `InboundLedgers::gotLedgerData`.
 *
 *  @param m  Parsed `TMLedgerData` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMLedgerData> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::kFEE_INVALID_DATA, msg);
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
    if (m->nodes_size() <= 0 || m->nodes_size() > Tuning::kHARD_MAX_REPLY_NODES)
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

/** Handle an incoming consensus `TMProposeSet` proposal.
 *
 *  Performs a rapid sanity-check on the DER signature size (64–72 bytes) and
 *  public-key type before the more expensive trusted/suppression checks.
 *  Duplicate proposals (detected via `addSuppressionPeerWithStatus`) update
 *  the squelch slot counters and are dropped.  Untrusted proposals are dropped
 *  early when `RELAY_UNTRUSTED_PROPOSALS == -1` or when the peer is diverged.
 *  Valid proposals are dispatched to a `JtProposalT`/`JtProposalUt` job
 *  calling `checkPropose` for signature verification and relay.
 *
 *  @param m  Parsed `TMProposeSet` protobuf message.
 */
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
        fee_.update(Resource::kFEE_INVALID_SIGNATURE, " signature can't be longer than 72 bytes");
        return;
    }

    if (!stringIsUInt256Sized(set.currenttxhash()) || !stringIsUInt256Sized(set.previousledger()))
    {
        JLOG(pJournal_.warn()) << "Proposal: malformed";
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "bad hashes");
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
        if (relayed && (stopwatch().now() - *relayed) < reduce_relay::kIDLED)
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

/** Handle an incoming `TMStatusChange` peer-state advertisement.
 *
 *  Updates `lastStatus_`, `closedLedgerHash_`, `previousLedgerHash_`,
 *  `minLedger_`, `maxLedger_`, and triggers `checkTracking` against the
 *  locally validated ledger sequence.  A `neLOST_SYNC` event zeroes the
 *  ledger hashes.  All ledger-field updates are guarded by `recentLock_`.
 *  Finally publishes the status change as a JSON event via
 *  `NetworkOPs::pubPeerStatus`.
 *
 *  @param m  Parsed `TMStatusChange` protobuf message.
 */
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

/** Compare the peer's highest ledger sequence against the validated index.
 *
 *  Reads `maxLedger_` (under `recentLock_`) and delegates to the two-argument
 *  overload.  A zero `maxLedger_` is skipped — the peer has not yet
 *  advertised any ledger range.
 *
 *  @param validationSeq  Sequence of the locally validated ledger.
 */
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

/** Update the peer's `Tracking` state by comparing two ledger sequences.
 *
 *  Uses a two-threshold design for hysteresis:
 *  - `|seq1 - seq2| < kCONVERGED_LEDGER_LIMIT (24)` → `Converged`.
 *  - `|seq1 - seq2| > kDIVERGED_LEDGER_LIMIT (128)` → `Diverged`; records
 *    `trackingTime_` so the timer can disconnect persistently diverged peers.
 *  Transitions from `Diverged` back to `Converged` are allowed but not from
 *  `Converged` to `Diverged` without passing through the gap first (the two
 *  thresholds are far apart).
 *
 *  @param seq1  One sequence number (order does not matter; absolute diff is used).
 *  @param seq2  The other sequence number.
 */
void
PeerImp::checkTracking(std::uint32_t seq1, std::uint32_t seq2)
{
    int const diff = std::max(seq1, seq2) - std::min(seq1, seq2);

    if (diff < Tuning::kCONVERGED_LEDGER_LIMIT)
    {
        // The peer's ledger sequence is close to the validation's
        tracking_ = Tracking::Converged;
    }

    if ((diff > Tuning::kDIVERGED_LEDGER_LIMIT) && (tracking_.load() != Tracking::Diverged))
    {
        // The peer's ledger sequence is way off the validation's
        std::scoped_lock const sl(recentLock_);

        tracking_ = Tracking::Diverged;
        trackingTime_ = clock_type::now();
    }
}

/** Handle an incoming `TMHaveTransactionSet` announcement.
 *
 *  When the peer reports `tsHAVE`, the TX-set hash is added to
 *  `recentTxSets_` (guarded by `recentLock_`) so future `hasTxSet` queries
 *  can route fetch requests to this peer.  Duplicate announcements for a hash
 *  already in the cache are charged as useless data.
 *
 *  @param m  Parsed `TMHaveTransactionSet` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m)
{
    if (!stringIsUInt256Sized(m->hash()))
    {
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "bad hash");
        return;
    }

    uint256 const hash = uint256::fromRaw(m->hash());

    if (m->status() == protocol::tsHAVE)
    {
        std::scoped_lock const sl(recentLock_);

        if (std::ranges::find(recentTxSets_, hash) != recentTxSets_.end())
        {
            fee_.update(Resource::kFEE_USELESS_DATA, "duplicate (tsHAVE)");
            return;
        }

        recentTxSets_.push_back(hash);
    }
}

/** Shared handler for `TMValidatorList` and `TMValidatorListCollection`.
 *
 *  Deduplicates by `sha512Half(manifest, blobs, version)` using the hash
 *  router.  Forwards to `ValidatorList::applyListsAndBroadcast`; charges and
 *  logs are driven by the best (most favourable) and worst (most harmful)
 *  `ListDisposition` across all blobs:
 *  - Accepted/Expired/Pending → no charge.
 *  - SameSequence/KnownSequence → useless-data charge.
 *  - Stale → invalid-data charge.
 *  - Untrusted → useless-data charge.
 *  - Invalid → invalid-signature charge.
 *  - UnsupportedVersion → invalid-data charge.
 *  Debug-mode assertions verify that cached sequence numbers are monotonically
 *  increasing for accepted/expired/pending lists.
 *
 *  @param messageType  Human-readable type label for logging ("ValidatorList"
 *      or "ValidatorListCollection").
 *  @param manifest     Publisher manifest bytes.
 *  @param version      UNL list format version.
 *  @param blobs        Encoded validator list blob payloads.
 */
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
        fee_.update(Resource::kFEE_HEAVY_BURDEN_PEER, "no blobs");
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
        fee_.update(Resource::kFEE_USELESS_DATA, "duplicate");
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
            fee_.update(
                Resource::kFEE_USELESS_DATA, " duplicate (same_sequence or known_sequence)");
            break;
        case ListDisposition::Stale:
            // There are very few good reasons for a peer to send an
            // old list, particularly more than once.
            fee_.update(Resource::kFEE_INVALID_DATA, "expired");
            break;
        case ListDisposition::Untrusted:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_.update(Resource::kFEE_USELESS_DATA, "untrusted");
            break;
        case ListDisposition::Invalid:
            // This shouldn't ever happen with a well-behaved peer
            fee_.update(Resource::kFEE_INVALID_SIGNATURE, "invalid list disposition");
            break;
        case ListDisposition::UnsupportedVersion:
            // During a version transition, this may be legitimate.
            // If it happens frequently, that's probably bad.
            fee_.update(Resource::kFEE_INVALID_DATA, "version");
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

/** Handle an incoming `TMValidatorList` (UNL version 1).
 *
 *  Rejected if the peer's protocol version predates `ValidatorListPropagation`
 *  (≥ 2.1).  Otherwise delegates to `onValidatorListMessage`.  Parse
 *  exceptions (malformed blobs) charge `kFEE_INVALID_DATA`.
 *
 *  @param m  Parsed `TMValidatorList` protobuf message.
 */
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
            fee_.update(Resource::kFEE_USELESS_DATA, "unsupported peer");
            return;
        }
        onValidatorListMessage(
            "ValidatorList", m->manifest(), m->version(), ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(pJournal_.warn()) << "ValidatorList: Exception, " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::kFEE_INVALID_DATA, e.what());
    }
}

/** Handle an incoming `TMValidatorListCollection` (UNL version ≥ 2).
 *
 *  Requires protocol ≥ 2.2 (`ValidatorList2Propagation`) and message version
 *  ≥ 2.  Rejects older message versions with `kFEE_INVALID_DATA`.  Otherwise
 *  delegates to `onValidatorListMessage`.
 *
 *  @param m  Parsed `TMValidatorListCollection` protobuf message.
 */
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
            fee_.update(Resource::kFEE_USELESS_DATA, "unsupported peer");
            return;
        }
        if (m->version() < 2)
        {
            JLOG(pJournal_.debug())
                << "ValidatorListCollection: received invalid validator list "
                   "version "
                << m->version() << " from peer using protocol version " << to_string(protocol_);
            fee_.update(Resource::kFEE_INVALID_DATA, "wrong version");
            return;
        }
        onValidatorListMessage(
            "ValidatorListCollection", m->manifest(), m->version(), ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(pJournal_.warn()) << "ValidatorListCollection: Exception, " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::kFEE_INVALID_DATA, e.what());
    }
}

/** Handle an incoming `TMValidation` ledger-validation message.
 *
 *  Sanity-checks size (≥ 50 bytes), deserialises the `STValidation`, and
 *  verifies currency (via `isCurrent` against the validation parameters).
 *  Duplicate validations update squelch counters and are dropped.  Untrusted
 *  validations are dropped when `RELAY_UNTRUSTED_VALIDATIONS == -1` or when
 *  local load is high.  Signature verification and acceptance are posted to a
 *  `JtValidationT`/`JtValidationUt` job calling `checkValidation`.
 *
 *  @param m  Parsed `TMValidation` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidation> const& m)
{
    if (m->validation().size() < 50)
    {
        JLOG(pJournal_.warn()) << "Validation: Too small";
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "too small");
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
            fee_.update(Resource::kFEE_USELESS_DATA, "not current");
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
            if (relayed && (stopwatch().now() - *relayed) < reduce_relay::kIDLED)
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
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, e.what());
    }
}

/** Handle an incoming `TMGetObjectByHash` query or reply.
 *
 *  **Query path** (`packet.query() == true`):
 *  - `otFETCH_PACK`: Delegates to `doFetchPack`.
 *  - `otTRANSACTIONS`: Dispatches to `doTransactions` via `JtRequestedTxn`
 *    (TX reduce-relay — only accepted when the feature is negotiated).
 *  - Other types: Iterates `packet.objects()`, fetches each hash from the
 *    node store, and packs results into a reply, capped at
 *    `kHARD_MAX_REPLY_NODES`.  Skips fetch if send queue is overloaded
 *    (`kDROP_SEND_QUEUE`).
 *
 *  **Reply path** (`packet.query() == false`):
 *  Iterates `packet.objects()` grouped by ledger sequence.  For each ledger
 *  not already held locally, nodes are added to the fetch-pack via
 *  `LedgerMaster::addFetchPack`; `gotFetchPack` is called at the end.
 *
 *  @param m  Parsed `TMGetObjectByHash` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash const& packet = *m;

    JLOG(pJournal_.trace()) << "received TMGetObjectByHash " << packet.type() << " "
                            << packet.objects_size();

    if (packet.query())
    {
        // this is a query
        if (sendQueue_.size() >= Tuning::kDROP_SEND_QUEUE)
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
                fee_.update(Resource::kFEE_MALFORMED_REQUEST, "disabled");
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
                fee_.update(Resource::kFEE_MALFORMED_REQUEST, "ledger hash");
                return;
            }

            reply.set_ledgerhash(packet.ledgerhash());
        }

        fee_.update(Resource::kFEE_MODERATE_BURDEN_PEER, " received a get object by hash request");

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
                    if (reply.objects_size() >= Tuning::kHARD_MAX_REPLY_NODES)
                    {
                        fee_.update(
                            Resource::kFEE_MODERATE_BURDEN_PEER,
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

/** Handle an incoming `TMHaveTransactions` hash-announcement batch.
 *
 *  Part of the TX reduce-relay path.  Only accepted when the feature is
 *  negotiated (`txReduceRelayEnabled`).  Dispatches to `handleHaveTransactions`
 *  via a `JtMissingTxn` job so cache lookups do not block the network strand.
 *
 *  @param m  Parsed `TMHaveTransactions` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactions> const& m)
{
    if (!txReduceRelayEnabled())
    {
        JLOG(pJournal_.error()) << "TMHaveTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "disabled");
        return;
    }

    std::weak_ptr<PeerImp> const weak = shared_from_this();
    app_.getJobQueue().addJob(JtMissingTxn, "HandleHaveTxs", [weak, m]() {
        if (auto peer = weak.lock())
            peer->handleHaveTransactions(m);
    });
}

/** Process a `TMHaveTransactions` batch and request any missing transactions.
 *
 *  For each hash in the batch:
 *  - Validates hash width (32 bytes) — charges `kFEE_MALFORMED_REQUEST` and
 *    returns immediately on the first invalid entry.
 *  - If the transaction is not in the local `MasterTransaction` cache, adds it
 *    to a `TMGetObjectByHash` (type `otTRANSACTIONS`) request.
 *  - If it is cached, removes the hash from our outbound TX-hash queue
 *    (`removeTxQueue`) since the peer clearly already has the tx.
 *  The request (if non-empty) is sent in a single `mtGET_OBJECTS` message.
 *
 *  @param m  Parsed `TMHaveTransactions` protobuf message.
 */
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
            fee_.update(Resource::kFEE_MALFORMED_REQUEST, "hash size");
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

/** Handle an incoming `TMTransactions` batch-transaction reply.
 *
 *  Only accepted when TX reduce-relay is negotiated.  Each transaction in the
 *  batch is processed via `handleTransaction` with `eraseTxQueue=false` and
 *  `batch=true`.  The batch size is also forwarded to the overlay's
 *  `TxMetrics` aggregator.
 *
 *  @param m  Parsed `TMTransactions` protobuf message.
 */
void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransactions> const& m)
{
    if (!txReduceRelayEnabled())
    {
        JLOG(pJournal_.error()) << "TMTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "disabled");
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

/** Handle an incoming `TMSquelch` reduce-relay suppression command.
 *
 *  A peer asks us to stop forwarding validations for a particular validator
 *  key for `squelchduration` seconds.  Guards:
 *  - The message must include a valid `validatorpubkey` (parseable public key).
 *  - Self-squelch attempts — where the target key matches our own validation
 *    key — are silently discarded; we never trust a remote peer to suppress
 *    our own outputs.
 *  - `squelch=false` removes an existing squelch entry.
 *  - Out-of-range durations passed to `addSquelch` result in
 *    `kFEE_INVALID_DATA`.
 *
 *  @note This handler self-posts to the strand (unlike most `onMessage`
 *      overloads) because it is called from `invokeProtocolMessage` which may
 *      run on any thread when dispatched via the job queue.
 *  @param m  Parsed `TMSquelch` protobuf message.
 */
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
        fee_.update(Resource::kFEE_INVALID_DATA, "squelch no pubkey");
        return;
    }
    auto validator = m->validatorpubkey();
    auto const slice{makeSlice(validator)};
    if (!publicKeyType(slice))
    {
        fee_.update(Resource::kFEE_INVALID_DATA, "squelch bad pubkey");
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
        fee_.update(Resource::kFEE_INVALID_DATA, "squelch duration");
    }

    JLOG(pJournal_.debug()) << "onMessage: TMSquelch " << slice << " " << id() << " " << duration;
}

//--------------------------------------------------------------------------

/** Append a ledger hash to the peer's recent-ledgers list if not already present.
 *
 *  The `lockedRecentLock` parameter is a zero-cost proof-of-lock token:
 *  callers must already hold `recentLock_` and pass their `scoped_lock` to
 *  make the lock requirement visible at the call site.
 *
 *  @param hash              Ledger hash to record.
 *  @param lockedRecentLock  Proof that the caller holds `recentLock_`.
 */
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

/** Build and send a fetch-pack response to this peer.
 *
 *  Guards against building fetch packs when the local node is loaded, the
 *  validated ledger is stale (>40s), or there are already >10 pack jobs
 *  queued.  The actual pack construction is deferred to a `JtPack` job via
 *  `LedgerMaster::makeFetchPack`.  `UptimeClock::now()` is snapshotted before
 *  posting so the job can measure elapsed time.
 *
 *  @param packet  The `TMGetObjectByHash` request containing the target
 *      ledger hash.
 */
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
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "hash size");
        return;
    }

    fee_.fee = Resource::kFEE_HEAVY_BURDEN_PEER;

    uint256 const hash = uint256::fromRaw(packet->ledgerhash());

    std::weak_ptr<PeerImp> const weak = shared_from_this();
    auto elapsed = UptimeClock::now();
    auto const pap = &app_;
    app_.getJobQueue().addJob(JtPack, "MakeFetchPack", [pap, weak, packet, hash, elapsed]() {
        pap->getLedgerMaster().makeFetchPack(weak, packet, hash, elapsed);
    });
}

/** Respond to a TX reduce-relay transaction-data request.
 *
 *  Validates that the request contains at most `kMAX_TX_QUEUE_SIZE` hashes.
 *  For each hash, looks up the transaction in the `MasterTransaction` cache;
 *  if not found (e.g. was evicted), charges malformed-request and returns
 *  early.  Found transactions are serialised into a `TMTransactions` reply
 *  which is sent as a single `mtTRANSACTIONS` message.
 *
 *  @param packet  The `TMGetObjectByHash` request listing the desired
 *      transaction hashes.
 */
void
PeerImp::doTransactions(std::shared_ptr<protocol::TMGetObjectByHash> const& packet)
{
    protocol::TMTransactions reply;

    JLOG(pJournal_.trace()) << "received TMGetObjectByHash requesting tx "
                            << packet->objects_size();

    if (packet->objects_size() > reduce_relay::kMAX_TX_QUEUE_SIZE)
    {
        JLOG(pJournal_.error()) << "doTransactions, invalid number of hashes";
        fee_.update(Resource::kFEE_MALFORMED_REQUEST, "too big");
        return;
    }

    for (std::uint32_t i = 0; i < packet->objects_size(); ++i)
    {
        auto const& obj = packet->objects(i);

        if (!stringIsUInt256Sized(obj.hash()))
        {
            fee_.update(Resource::kFEE_MALFORMED_REQUEST, "hash size");
            return;
        }

        uint256 hash = uint256::fromRaw(obj.hash());

        auto txn = app_.getMasterTransaction().fetchFromCache(hash);

        if (!txn)
        {
            JLOG(pJournal_.error())
                << "doTransactions, transaction not found " << Slice(hash.data(), hash.size());
            fee_.update(Resource::kFEE_MALFORMED_REQUEST, "tx not found");
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

/** Validate and submit a received transaction (runs on the job queue).
 *
 *  Steps: (1) Reject `tfInnerBatchTxn` regardless of amendment state.
 *  (2) Reject expired transactions (`sfLastLedgerSequence` < validated index)
 *  and mark them `BAD` in the hash router.  (3) Pseudo-transactions are
 *  canonicalized and relayed but not submitted to the transaction engine.
 *  (4) If `checkSignature`, calls `checkValidity`; failure marks the tx `BAD`
 *  and charges `kFEE_INVALID_SIGNATURE`.  (5) Otherwise calls `forceValidity`
 *  (cluster-trusted path).  (6) Constructs a `Transaction` object and submits
 *  via `NetworkOPs::processTransaction`.
 *
 *  @param flags          Hash-router flags populated by `shouldProcess`.
 *  @param checkSignature True if signature verification should be performed.
 *  @param stx            Deserialised transaction object.
 *  @param batch          True when the transaction arrived in a `TMTransactions`
 *      batch; suppresses the pseudo-tx useless-data charge.
 */
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
            charge(Resource::kFEE_MODERATE_BURDEN_PEER, "inner batch txn");
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
            charge(Resource::kFEE_USELESS_DATA, "expired tx");
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
                    charge(Resource::kFEE_USELESS_DATA, "pseudo tx");
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
                charge(Resource::kFEE_INVALID_SIGNATURE, "check transaction signature failure");
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
            charge(Resource::kFEE_INVALID_SIGNATURE, "tx (impossible)");
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
        charge(Resource::kFEE_INVALID_DATA, "tx "s + ex.what());
    }
}

/** Verify a consensus proposal signature and relay it (runs on the job queue).
 *
 *  Cluster peers bypass signature verification (`checkSign`).  For trusted
 *  proposals, `processTrustedProposal` determines whether to relay.  For
 *  untrusted proposals, relay is controlled by `RELAY_UNTRUSTED_PROPOSALS`
 *  or cluster membership.  After relaying, `updateSlotAndSquelch` is called
 *  with the set of peers that already had the message (returned by `relay`)
 *  to keep squelch slot counters accurate.
 *
 *  @param isTrusted  True if the proposer's key is in the trusted validator set.
 *  @param packet     Original wire message (used for relay).
 *  @param peerPos    Reconstructed proposal object carrying the suppression ID.
 */
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
        charge(Resource::kFEE_INVALID_SIGNATURE, desc);
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

/** Verify a validation object's signature and relay it (runs on the job queue).
 *
 *  Calls `STValidation::isValid()` (cryptographic check); invalid validations
 *  charge `kFEE_INVALID_SIGNATURE`.  Valid validations are passed to
 *  `NetworkOPs::recvValidation`; accepted validations (or those from cluster
 *  members) are relayed via `overlay_.relay`, and `updateSlotAndSquelch` is
 *  called with the suppression set to maintain reduce-relay slot counters.
 *
 *  @param val     Deserialised and time-checked `STValidation` object.
 *  @param key     `sha512Half` of the raw validation bytes (suppression key).
 *  @param packet  Original wire message (passed to `overlay_.relay`).
 */
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
        charge(Resource::kFEE_INVALID_SIGNATURE, desc);
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
        charge(Resource::kFEE_MALFORMED_REQUEST, "validation "s + ex.what());
    }
}

/** Select the highest-scoring peer that holds a TX tree with `rootHash`.
 *
 *  Iterates all active peers, filters by `hasTxSet(rootHash)`, excludes
 *  `skip` (the calling peer — we already know it doesn't have it), and
 *  returns the peer with the highest `getScore(true)`.
 *
 *  @param ov        Overlay manager to iterate peers over.
 *  @param rootHash  SHAMap root hash of the desired candidate transaction set.
 *  @param skip      Peer to exclude from selection (typically `this`).
 *  @return Best candidate peer, or null if none qualify.
 */
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

/** Select the highest-scoring peer that is believed to hold a specific ledger.
 *
 *  Iterates all active peers, filters by `hasLedger(ledgerHash, ledger)`,
 *  excludes `skip`, and returns the peer with the highest `getScore(true)`.
 *  Scoring incorporates latency and randomness so load is distributed across
 *  equally-capable peers.
 *
 *  @param ov          Overlay manager to iterate peers over.
 *  @param ledgerHash  Hash of the desired ledger.
 *  @param ledger      Sequence number of the desired ledger (0 to skip seq check).
 *  @param skip        Peer to exclude from selection (typically `this`).
 *  @return Best candidate peer, or null if none qualify.
 */
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

/** Serialise and send the base ledger nodes (header + root hashes).
 *
 *  For a `liBASE` request, packs the ledger header and — when available — the
 *  state-map root node and the transaction-map root node into `ledgerData`,
 *  then transmits the reply.
 *
 *  @param ledger      The ledger whose base data is to be sent.
 *  @param ledgerData  Pre-populated `TMLedgerData` (hash, seq, type fields
 *      already set by the caller); nodes are appended here.
 */
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
    if (stateMap.getHash() != beast::kZERO)
    {
        // Return account state root node if possible
        Serializer root(768);

        stateMap.serializeRoot(root);
        ledgerData.add_nodes()->set_nodedata(root.getDataPtr(), root.getLength());

        if (ledger->header().txHash != beast::kZERO)
        {
            auto const& txMap{ledger->txMap()};
            if (txMap.getHash() != beast::kZERO)
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

/** Resolve the ledger requested by a `TMGetLedger` message.
 *
 *  Lookup priority: hash → sequence → `ltCLOSED`.  When a hash-based lookup
 *  fails and the request has a `querytype` but no `requestcookie`, relays the
 *  request to the best peer that has the ledger via `getPeerWithLedger` (sets
 *  `requestcookie` to our own ID so the reply is routed back through us).
 *  After finding a ledger, validates that the returned sequence matches the
 *  requested sequence and is not below `getEarliestFetch`.
 *
 *  @param m  The `TMGetLedger` request.
 *  @return   The resolved ledger, or null if not found or relayed.
 */
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
                    charge(Resource::kFEE_MALFORMED_REQUEST, "get_ledger ledgerSeq");

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

/** Resolve the candidate transaction set requested by a `TMGetLedger` message.
 *
 *  Looks up the SHAMap by hash in `InboundTransactions`.  On miss, if the
 *  request has `querytype` and no `requestcookie`, relays to the best peer
 *  with the TX tree (sets `requestcookie` so the reply routes back through us).
 *
 *  @param m  The `TMGetLedger` request; `ledgerhash` must be the TX-set root.
 *  @return   The SHAMap if available locally; null if not found or relayed.
 */
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

/** Fulfil a `TMGetLedger` request (runs on the `JtLedgerReq` job queue).
 *
 *  Applies send-queue and local-load backpressure guards for non-TX-candidate
 *  requests.  Resolves the ledger or TX-set via `getLedger`/`getTxSet`, fills
 *  the `TMLedgerData` reply with the appropriate SHAMap nodes (up to
 *  `kSOFT_MAX_REPLY_NODES` per batch, hard-capped at `kHARD_MAX_REPLY_NODES`),
 *  and transmits.  Query depth defaults to 2 for high-latency peers, 1
 *  otherwise, unless the request specifies an explicit depth.  For `liBASE`
 *  requests, delegates to `sendLedgerBase` and returns immediately.
 *
 *  Relay responses (identified by a `requestcookie`) skip the resource charge
 *  applied to direct requests.
 *
 *  @param m  The `TMGetLedger` request, already validated by `onMessage`.
 */
void
PeerImp::processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    // Do not resource charge a peer responding to a relay
    if (!m->has_requestcookie())
        charge(Resource::kFEE_MODERATE_BURDEN_PEER, "received a get ledger request");

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
        if (sendQueue_.size() >= Tuning::kDROP_SEND_QUEUE)
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
             i < m->nodeids_size() && ledgerData.nodes_size() < Tuning::kSOFT_MAX_REPLY_NODES;
             ++i)
        {
            auto const shaMapNodeId{deserializeSHAMapNodeID(m->nodeids(i))};

            data.clear();
            data.reserve(Tuning::kSOFT_MAX_REPLY_NODES);

            try
            {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access) nodeids checked in onGetLedger
                if (map->getNodeFat(*shaMapNodeId, data, fatLeaves, queryDepth))
                {
                    JLOG(pJournal_.trace())
                        << "processLedgerRequest: getNodeFat got " << data.size() << " nodes";

                    for (auto const& d : data)
                    {
                        if (ledgerData.nodes_size() >= Tuning::kHARD_MAX_REPLY_NODES)
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

/** Compute a composite peer score for data-fetch candidate selection.
 *
 *  The score combines four components:
 *  - A random baseline `[0, kSP_RANDOM_MAX=9999]` to break ties and distribute
 *    load across equally-capable peers.
 *  - `+kSP_HAVE_ITEM=10000` if `haveItem` is true (peer is known to have the
 *    requested data).
 *  - `-latency_ms * kSP_LATENCY=30` per millisecond of measured RTT.
 *  - `-kSP_NO_LATENCY=8000` penalty if no latency sample is available yet.
 *
 *  The caller selects the peer with the highest score.
 *
 *  @param haveItem  True when the peer is known to hold the requested item.
 *  @return A signed integer score; higher is better.
 *  @note Acquires `recentLock_` briefly to read `latency_`.
 */
int
PeerImp::getScore(bool haveItem) const
{
    // Random component of score, used to break ties and avoid
    // overloading the "best" peer
    static int const kSP_RANDOM_MAX = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static int const kSP_HAVE_ITEM = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static int const kSP_LATENCY = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static int const kSP_NO_LATENCY = 8000;

    int score = randInt(kSP_RANDOM_MAX);

    if (haveItem)
        score += kSP_HAVE_ITEM;

    std::optional<std::chrono::milliseconds> latency;
    {
        std::scoped_lock const sl(recentLock_);
        latency = latency_;
    }

    if (latency)
    {
        score -= latency->count() * kSP_LATENCY;
    }
    else
    {
        score -= kSP_NO_LATENCY;
    }

    return score;
}

/** Return true if this peer's smoothed RTT exceeds `kPEER_HIGH_LATENCY` (300ms).
 *
 *  Used by `processLedgerRequest` to select a shallower default query depth
 *  (1 instead of 2) for low-bandwidth peers so replies stay within the size
 *  cap.
 *
 *  @return True when `latency_ >= kPEER_HIGH_LATENCY`.
 *  @note Acquires `recentLock_`.
 */
bool
PeerImp::isHighLatency() const
{
    std::scoped_lock const sl(recentLock_);
    return latency_ >= kPEER_HIGH_LATENCY;
}

/** Record bytes transferred for a single message and update rolling averages.
 *
 *  Accumulates bytes into a per-second bucket.  At the end of each second,
 *  the bucket average is appended to `rollingAvg_` (a circular buffer) and
 *  a new overall average is recomputed.  This gives a smoothed per-second
 *  throughput figure independent of message arrival rate.
 *
 *  @param bytes  Number of bytes in the completed message transfer.
 */
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

/** Return the smoothed rolling-average throughput in bytes per second.
 *
 *  @return Average bytes/second over the rolling window, protected by a
 *      shared lock so concurrent RPC readers do not block writers.
 */
std::uint64_t
PeerImp::Metrics::averageBytes() const
{
    std::shared_lock const lock{mutex_};
    return rollingAvgBytes_;
}

/** Return the total number of bytes transferred since connection start.
 *
 *  @return Cumulative byte count, protected by a shared lock.
 */
std::uint64_t
PeerImp::Metrics::totalBytes() const
{
    std::shared_lock const lock{mutex_};
    return totalBytes_;
}

}  // namespace xrpl
