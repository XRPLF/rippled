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

#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/Tuning.h>
#include <xrpld/perflog/PerfLog.h>

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/ostream.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>

using namespace std::chrono_literals;

namespace ripple {

namespace {
/** The threshold above which we treat a peer connection as high latency */
std::chrono::milliseconds constexpr peerHighLatency{300};

/** How often we PING the peer to check for latency and sendq probe */
std::chrono::seconds constexpr peerTimerInterval{60};
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
    std::unique_ptr<stream_type>&& stream_ptr,
    OverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(app_.journal("Peer"), makePrefix(id))
    , p_sink_(app_.journal("Protocol"), makePrefix(id))
    , journal_(sink_)
    , p_journal_(p_sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , timer_(waitable_timer{socket_.get_executor()})
    , remote_address_(slot->remote_endpoint())
    , overlay_(overlay)
    , inbound_(true)
    , protocol_(protocol)
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.journal("Squelch"))
    , usage_(consumer)
    , fee_{Resource::feeTrivialPeer, ""}
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              app_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
    , txReduceRelayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_TXRR,
          app_.config().TX_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_LEDGER_REPLAY,
          app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.info())
        << "compression enabled " << (compressionEnabled_ == Compressed::On)
        << " vp reduce-relay base squelch enabled "
        << peerFeatureEnabled(
               headers_,
               FEATURE_VPRR,
               app_.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
        << " tx reduce-relay enabled " << txReduceRelayEnabled_ << " on "
        << remote_address_ << " " << id_;
}

PeerImp::~PeerImp()
{
    bool const inCluster{cluster()};

    overlay_.deletePeer(id_);
    overlay_.onPeerDeactivate(id_);
    overlay_.peerFinder().on_closed(slot_);
    overlay_.remove(slot_);

    if (inCluster)
    {
        JLOG(journal_.warn()) << name() << " left cluster";
    }
}

// Helper function to check for valid uint256 values in protobuf buffers
static bool
stringIsUint256Sized(std::string const& pBuffStr)
{
    return pBuffStr.size() == uint256::size();
}

void
PeerImp::run()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&PeerImp::run, shared_from_this()));

    auto parseLedgerHash =
        [](std::string_view value) -> std::optional<uint256> {
        if (uint256 ret; ret.parseHex(value))
            return ret;

        if (auto const s = base64_decode(value); s.size() == uint256::size())
            return uint256{s};

        return std::nullopt;
    };

    std::optional<uint256> closed;
    std::optional<uint256> previous;

    if (auto const iter = headers_.find("Closed-Ledger");
        iter != headers_.end())
    {
        closed = parseLedgerHash(iter->value());

        if (!closed)
            fail("Malformed handshake data (1)");
    }

    if (auto const iter = headers_.find("Previous-Ledger");
        iter != headers_.end())
    {
        previous = parseLedgerHash(iter->value());

        if (!previous)
            fail("Malformed handshake data (2)");
    }

    if (previous && !closed)
        fail("Malformed handshake data (3)");

    {
        std::lock_guard<std::mutex> sl(recentLock_);
        if (closed)
            closedLedgerHash_ = *closed;
        if (previous)
            previousLedgerHash_ = *previous;
    }

    if (inbound_)
        doAccept();
    else
        doProtocolStart();

    // Anything else that needs to be done with the connection should be
    // done in doProtocolStart
}

void
PeerImp::stop()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&PeerImp::stop, shared_from_this()));
    if (socket_.is_open())
    {
        // The rationale for using different severity levels is that
        // outbound connections are under our control and may be logged
        // at a higher level, but inbound connections are more numerous and
        // uncontrolled so to prevent log flooding the severity is reduced.
        //
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Stop";
        }
        else
        {
            JLOG(journal_.info()) << "Stop";
        }
    }
    close();
}

//------------------------------------------------------------------------------

void
PeerImp::send(std::shared_ptr<Message> const& m)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&PeerImp::send, shared_from_this(), m));
    if (gracefulClose_)
        return;
    if (detaching_)
        return;

    auto validator = m->getValidatorKey();
    if (validator && !squelch_.expireSquelch(*validator))
    {
        overlay_.reportOutboundTraffic(
            TrafficCount::category::squelch_suppressed,
            static_cast<int>(m->getBuffer(compressionEnabled_).size()));
        return;
    }

    // report categorized outgoing traffic
    overlay_.reportOutboundTraffic(
        safe_cast<TrafficCount::category>(m->getCategory()),
        static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    // report total outgoing traffic
    overlay_.reportOutboundTraffic(
        TrafficCount::category::total,
        static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    auto sendq_size = send_queue_.size();

    if (sendq_size < Tuning::targetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        large_sendq_ = 0;
    }
    else if (auto sink = journal_.debug();
             sink && (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const n = name();
        sink << (n.empty() ? remote_address_.to_string() : n)
             << " sendq: " << sendq_size;
    }

    send_queue_.push(m);

    if (sendq_size != 0)
        return;

    boost::asio::async_write(
        stream_,
        boost::asio::buffer(
            send_queue_.front()->getBuffer(compressionEnabled_)),
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
        return post(
            strand_, std::bind(&PeerImp::sendTxQueue, shared_from_this()));

    if (!txQueue_.empty())
    {
        protocol::TMHaveTransactions ht;
        std::for_each(txQueue_.begin(), txQueue_.end(), [&](auto const& hash) {
            ht.add_hashes(hash.data(), hash.size());
        });
        JLOG(p_journal_.trace()) << "sendTxQueue " << txQueue_.size();
        txQueue_.clear();
        send(std::make_shared<Message>(ht, protocol::mtHAVE_TRANSACTIONS));
    }
}

void
PeerImp::addTxQueue(uint256 const& hash)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&PeerImp::addTxQueue, shared_from_this(), hash));

    if (txQueue_.size() == reduce_relay::MAX_TX_QUEUE_SIZE)
    {
        JLOG(p_journal_.warn()) << "addTxQueue exceeds the cap";
        sendTxQueue();
    }

    txQueue_.insert(hash);
    JLOG(p_journal_.trace()) << "addTxQueue " << txQueue_.size();
}

void
PeerImp::removeTxQueue(uint256 const& hash)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(&PeerImp::removeTxQueue, shared_from_this(), hash));

    auto removed = txQueue_.erase(hash);
    JLOG(p_journal_.trace()) << "removeTxQueue " << removed;
}

void
PeerImp::charge(Resource::Charge const& fee, std::string const& context)
{
    if ((usage_.charge(fee, context) == Resource::drop) &&
        usage_.disconnect(p_journal_) && strand_.running_in_this_thread())
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
    return static_cast<bool>(app_.cluster().member(publicKey_));
}

std::string
PeerImp::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"];
    return headers_["Server"];
}

Json::Value
PeerImp::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::public_key] = toBase58(TokenType::NodePublic, publicKey_);
    ret[jss::address] = remote_address_.to_string();

    if (inbound_)
        ret[jss::inbound] = true;

    if (cluster())
    {
        ret[jss::cluster] = true;

        if (auto const n = name(); !n.empty())
            // Could move here if Json::Value supported moving from a string
            ret[jss::name] = n;
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
        std::lock_guard sl(recentLock_);
        if (latency_)
            ret[jss::latency] = static_cast<Json::UInt>(latency_->count());
    }

    ret[jss::uptime] = static_cast<Json::UInt>(
        std::chrono::duration_cast<std::chrono::seconds>(uptime()).count());

    std::uint32_t minSeq, maxSeq;
    ledgerRange(minSeq, maxSeq);

    if ((minSeq != 0) || (maxSeq != 0))
        ret[jss::complete_ledgers] =
            std::to_string(minSeq) + " - " + std::to_string(maxSeq);

    switch (tracking_.load())
    {
        case Tracking::diverged:
            ret[jss::track] = "diverged";
            break;

        case Tracking::unknown:
            ret[jss::track] = "unknown";
            break;

        case Tracking::converged:
            // Nothing to do here
            break;
    }

    uint256 closedLedgerHash;
    protocol::TMStatusChange last_status;
    {
        std::lock_guard sl(recentLock_);
        closedLedgerHash = closedLedgerHash_;
        last_status = last_status_;
    }

    if (closedLedgerHash != beast::zero)
        ret[jss::ledger] = to_string(closedLedgerHash);

    if (last_status.has_newstatus())
    {
        switch (last_status.newstatus())
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
                JLOG(p_journal_.warn())
                    << "Unknown status: " << last_status.newstatus();
        }
    }

    ret[jss::metrics] = Json::Value(Json::objectValue);
    ret[jss::metrics][jss::total_bytes_recv] =
        std::to_string(metrics_.recv.total_bytes());
    ret[jss::metrics][jss::total_bytes_sent] =
        std::to_string(metrics_.sent.total_bytes());
    ret[jss::metrics][jss::avg_bps_recv] =
        std::to_string(metrics_.recv.average_bytes());
    ret[jss::metrics][jss::avg_bps_sent] =
        std::to_string(metrics_.sent.average_bytes());

    return ret;
}

bool
PeerImp::supportsFeature(ProtocolFeature f) const
{
    switch (f)
    {
        case ProtocolFeature::ValidatorListPropagation:
            return protocol_ >= make_protocol(2, 1);
        case ProtocolFeature::ValidatorList2Propagation:
            return protocol_ >= make_protocol(2, 2);
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
        std::lock_guard sl(recentLock_);
        if ((seq != 0) && (seq >= minLedger_) && (seq <= maxLedger_) &&
            (tracking_.load() == Tracking::converged))
            return true;
        if (std::find(recentLedgers_.begin(), recentLedgers_.end(), hash) !=
            recentLedgers_.end())
            return true;
    }
    return false;
}

void
PeerImp::ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const
{
    std::lock_guard sl(recentLock_);

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

bool
PeerImp::hasTxSet(uint256 const& hash) const
{
    std::lock_guard sl(recentLock_);
    return std::find(recentTxSets_.begin(), recentTxSets_.end(), hash) !=
        recentTxSets_.end();
}

void
PeerImp::cycleStatus()
{
    // Operations on closedLedgerHash_ and previousLedgerHash_ must be
    // guarded by recentLock_.
    std::lock_guard sl(recentLock_);
    previousLedgerHash_ = closedLedgerHash_;
    closedLedgerHash_.zero();
}

bool
PeerImp::hasRange(std::uint32_t uMin, std::uint32_t uMax)
{
    std::lock_guard sl(recentLock_);
    return (tracking_ != Tracking::diverged) && (uMin >= minLedger_) &&
        (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

void
PeerImp::close()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::PeerImp::close : strand in this thread");
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        timer_.cancel(ec);
        socket_.close(ec);
        overlay_.incPeerDisconnect();
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Closed";
        }
        else
        {
            JLOG(journal_.info()) << "Closed";
        }
    }
}

void
PeerImp::fail(std::string const& reason)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void(Peer::*)(std::string const&)) & PeerImp::fail,
                shared_from_this(),
                reason));
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        std::string const n = name();
        JLOG(journal_.warn()) << (n.empty() ? remote_address_.to_string() : n)
                              << " failed: " << reason;
    }
    close();
}

void
PeerImp::fail(std::string const& name, error_code ec)
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::PeerImp::fail : strand in this thread");
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

void
PeerImp::gracefulClose()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::PeerImp::gracefulClose : strand in this thread");
    XRPL_ASSERT(
        socket_.is_open(), "ripple::PeerImp::gracefulClose : socket is open");
    XRPL_ASSERT(
        !gracefulClose_,
        "ripple::PeerImp::gracefulClose : socket is not closing");
    gracefulClose_ = true;
    if (send_queue_.size() > 0)
        return;
    setTimer();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &PeerImp::onShutdown, shared_from_this(), std::placeholders::_1)));
}

void
PeerImp::setTimer()
{
    error_code ec;
    timer_.expires_from_now(peerTimerInterval, ec);

    if (ec)
    {
        JLOG(journal_.error()) << "setTimer: " << ec.message();
        return;
    }
    timer_.async_wait(bind_executor(
        strand_,
        std::bind(
            &PeerImp::onTimer, shared_from_this(), std::placeholders::_1)));
}

// convenience for ignoring the error code
void
PeerImp::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

//------------------------------------------------------------------------------

std::string
PeerImp::makePrefix(id_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

void
PeerImp::onTimer(error_code const& ec)
{
    if (!socket_.is_open())
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        return close();
    }

    if (large_sendq_++ >= Tuning::sendqIntervals)
    {
        fail("Large send queue");
        return;
    }

    if (auto const t = tracking_.load(); !inbound_ && t != Tracking::converged)
    {
        clock_type::duration duration;

        {
            std::lock_guard sl(recentLock_);
            duration = clock_type::now() - trackingTime_;
        }

        if ((t == Tracking::diverged &&
             (duration > app_.config().MAX_DIVERGED_TIME)) ||
            (t == Tracking::unknown &&
             (duration > app_.config().MAX_UNKNOWN_TIME)))
        {
            overlay_.peerFinder().on_failure(slot_);
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
    lastPingSeq_ = rand_int<std::uint32_t>();

    protocol::TMPing message;
    message.set_type(protocol::TMPing::ptPING);
    message.set_seq(*lastPingSeq_);

    send(std::make_shared<Message>(message, protocol::mtPING));

    setTimer();
}

void
PeerImp::onShutdown(error_code ec)
{
    cancelTimer();
    // If we don't get eof then something went wrong
    if (!ec)
    {
        JLOG(journal_.error()) << "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//------------------------------------------------------------------------------
void
PeerImp::doAccept()
{
    XRPL_ASSERT(
        read_buffer_.size() == 0,
        "ripple::PeerImp::doAccept : empty read buffer");

    JLOG(journal_.debug()) << "doAccept: " << remote_address_;

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    if (auto member = app_.cluster().member(publicKey_))
    {
        {
            std::unique_lock lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    overlay_.activate(shared_from_this());

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*write_buffer) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().public_ip,
        remote_address_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocol_,
        app_);

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        write_buffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, write_buffer, self = shared_from_this()](
                error_code ec, std::size_t bytes_transferred) {
                if (!socket_.is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("onWriteResponse", ec);
                if (write_buffer->size() == bytes_transferred)
                    return doProtocolStart();
                return fail("Failed to write header");
            }));
}

std::string
PeerImp::name() const
{
    std::shared_lock read_lock{nameMutex_};
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
    onReadMessage(error_code(), 0);

    // Send all the validator lists that have been loaded
    if (inbound_ && supportsFeature(ProtocolFeature::ValidatorListPropagation))
    {
        app_.validators().for_each_available(
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
                    p_journal_);

                // Don't send it next time.
                app_.getHashRouter().addSuppressionPeer(hash, id_);
            });
    }

    if (auto m = overlay_.getManifestsMessage())
        send(m);

    setTimer();
}

// Called repeatedly with protocol message data
void
PeerImp::onReadMessage(error_code ec, std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec == boost::asio::error::eof)
    {
        JLOG(journal_.info()) << "EOF";
        return gracefulClose();
    }
    if (ec)
        return fail("onReadMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onReadMessage: " << bytes_transferred << " bytes";
        else
            stream << "onReadMessage";
    }

    metrics_.recv.add_message(bytes_transferred);

    read_buffer_.commit(bytes_transferred);

    auto hint = Tuning::readBufferBytes;

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;

        using namespace std::chrono_literals;
        std::tie(bytes_consumed, ec) = perf::measureDurationAndLog(
            [&]() {
                return invokeProtocolMessage(read_buffer_.data(), *this, hint);
            },
            "invokeProtocolMessage",
            350ms,
            journal_);

        if (ec)
            return fail("onReadMessage", ec);
        if (!socket_.is_open())
            return;
        if (gracefulClose_)
            return;
        if (bytes_consumed == 0)
            break;
        read_buffer_.consume(bytes_consumed);
    }

    // Timeout on writes only
    stream_.async_read_some(
        read_buffer_.prepare(std::max(Tuning::readBufferBytes, hint)),
        bind_executor(
            strand_,
            std::bind(
                &PeerImp::onReadMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

void
PeerImp::onWriteMessage(error_code ec, std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWriteMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onWriteMessage: " << bytes_transferred << " bytes";
        else
            stream << "onWriteMessage";
    }

    metrics_.sent.add_message(bytes_transferred);

    XRPL_ASSERT(
        !send_queue_.empty(),
        "ripple::PeerImp::onWriteMessage : non-empty send buffer");
    send_queue_.pop();
    if (!send_queue_.empty())
    {
        // Timeout on writes only
        return boost::asio::async_write(
            stream_,
            boost::asio::buffer(
                send_queue_.front()->getBuffer(compressionEnabled_)),
            bind_executor(
                strand_,
                std::bind(
                    &PeerImp::onWriteMessage,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

    if (gracefulClose_)
    {
        return stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(
                &PeerImp::onShutdown,
                shared_from_this(),
                std::placeholders::_1)));
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
    std::size_t uncompressed_size,
    bool isCompressed)
{
    auto const name = protocolMessageName(type);
    load_event_ = app_.getJobQueue().makeLoadEvent(jtPEER, name);
    fee_ = {Resource::feeTrivialPeer, name};

    auto const category = TrafficCount::categorize(
        *m, static_cast<protocol::MessageType>(type), true);

    // report total incoming traffic
    overlay_.reportInboundTraffic(
        TrafficCount::category::total, static_cast<int>(size));

    // increase the traffic received for a specific category
    overlay_.reportInboundTraffic(category, static_cast<int>(size));

    using namespace protocol;
    if ((type == MessageType::mtTRANSACTION ||
         type == MessageType::mtHAVE_TRANSACTIONS ||
         type == MessageType::mtTRANSACTIONS ||
         // GET_OBJECTS
         category == TrafficCount::category::get_transactions ||
         // GET_LEDGER
         category == TrafficCount::category::ld_tsc_get ||
         category == TrafficCount::category::ld_tsc_share ||
         // LEDGER_DATA
         category == TrafficCount::category::gl_tsc_share ||
         category == TrafficCount::category::gl_tsc_get) &&
        (txReduceRelayEnabled() || app_.config().TX_REDUCE_RELAY_METRICS))
    {
        overlay_.addTxMetrics(
            static_cast<MessageType>(type), static_cast<std::uint64_t>(size));
    }
    JLOG(journal_.trace()) << "onMessageBegin: " << type << " " << size << " "
                           << uncompressed_size << " " << isCompressed;
}

void
PeerImp::onMessageEnd(
    std::uint16_t,
    std::shared_ptr<::google::protobuf::Message> const&)
{
    load_event_.reset();
    charge(fee_.fee, fee_.context);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMManifests> const& m)
{
    auto const s = m->list_size();

    if (s == 0)
    {
        fee_.update(Resource::feeUselessData, "empty");
        return;
    }

    if (s > 100)
        fee_.update(Resource::feeModerateBurdenPeer, "oversize");

    app_.getJobQueue().addJob(
        jtMANIFEST, "receiveManifests", [this, that = shared_from_this(), m]() {
            overlay_.onManifests(m, that);
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPing> const& m)
{
    if (m->type() == protocol::TMPing::ptPING)
    {
        // We have received a ping request, reply with a pong
        fee_.update(Resource::feeModerateBurdenPeer, "ping request");
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
            auto const rtt = std::chrono::round<std::chrono::milliseconds>(
                clock_type::now() - lastPingTime_);

            std::lock_guard sl(recentLock_);

            if (latency_)
                latency_ = (*latency_ * 7 + rtt) / 8;
            else
                latency_ = rtt;
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
        fee_.update(Resource::feeUselessData, "unknown cluster");
        return;
    }

    for (int i = 0; i < m->clusternodes().size(); ++i)
    {
        protocol::TMClusterNode const& node = m->clusternodes(i);

        std::string name;
        if (node.has_nodename())
            name = node.nodename();

        auto const publicKey =
            parseBase58<PublicKey>(TokenType::NodePublic, node.publickey());

        // NIKB NOTE We should drop the peer immediately if
        // they send us a public key we can't parse
        if (publicKey)
        {
            auto const reportTime =
                NetClock::time_point{NetClock::duration{node.reporttime()}};

            app_.cluster().update(
                *publicKey, name, node.nodeload(), reportTime);
        }
    }

    int loadSources = m->loadsources().size();
    if (loadSources != 0)
    {
        Resource::Gossip gossip;
        gossip.items.reserve(loadSources);
        for (int i = 0; i < m->loadsources().size(); ++i)
        {
            protocol::TMLoadSource const& node = m->loadsources(i);
            Resource::Gossip::Item item;
            item.address = beast::IP::Endpoint::from_string(node.name());
            item.balance = node.cost();
            if (item.address != beast::IP::Endpoint())
                gossip.items.push_back(item);
        }
        overlay_.resourceManager().importConsumers(name(), gossip);
    }

    // Calculate the cluster fee:
    auto const thresh = app_.timeKeeper().now() - 90s;
    std::uint32_t clusterFee = 0;

    std::vector<std::uint32_t> fees;
    fees.reserve(app_.cluster().size());

    app_.cluster().for_each([&fees, thresh](ClusterNode const& status) {
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
    if (tracking_.load() != Tracking::converged || m->version() != 2)
        return;

    // The number is arbitrary and doesn't have any real significance or
    // implication for the protocol.
    if (m->endpoints_v2().size() >= 1024)
    {
        fee_.update(Resource::feeUselessData, "endpoints too large");
        return;
    }

    std::vector<PeerFinder::Endpoint> endpoints;
    endpoints.reserve(m->endpoints_v2().size());

    auto malformed = 0;
    for (auto const& tm : m->endpoints_v2())
    {
        auto result = beast::IP::Endpoint::from_string_checked(tm.endpoint());

        if (!result)
        {
            JLOG(p_journal_.error()) << "failed to parse incoming endpoint: {"
                                     << tm.endpoint() << "}";
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
            result = remote_address_.at_port(result->port());

        endpoints.emplace_back(*result, tm.hops());
    }

    // Charge the peer for each malformed endpoint. As there still may be
    // multiple valid endpoints we don't return early.
    if (malformed > 0)
    {
        fee_.update(
            Resource::feeInvalidData * malformed,
            std::to_string(malformed) + " malformed endpoints");
    }

    if (!endpoints.empty())
        overlay_.peerFinder().on_endpoints(slot_, endpoints);
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
    XRPL_ASSERT(
        eraseTxQueue != batch,
        ("ripple::PeerImp::handleTransaction : valid inputs"));
    if (tracking_.load() == Tracking::diverged)
        return;

    if (app_.getOPs().isNeedNetworkLedger())
    {
        // If we've never been in synch, there's nothing we can do
        // with a transaction
        JLOG(p_journal_.debug()) << "Ignoring incoming transaction: "
                                 << "Need network ledger";
        return;
    }

    SerialIter sit(makeSlice(m->rawtransaction()));

    try
    {
        auto stx = std::make_shared<STTx const>(sit);
        uint256 txID = stx->getTransactionID();

        // Charge strongly for attempting to relay a txn with tfInnerBatchTxn
        // LCOV_EXCL_START
        if (stx->isFlag(tfInnerBatchTxn) &&
            getCurrentTransactionRules()->enabled(featureBatch))
        {
            JLOG(p_journal_.warn()) << "Ignoring Network relayed Tx containing "
                                       "tfInnerBatchTxn (handleTransaction).";
            fee_.update(Resource::feeModerateBurdenPeer, "inner batch txn");
            return;
        }
        // LCOV_EXCL_STOP

        HashRouterFlags flags;
        constexpr std::chrono::seconds tx_interval = 10s;

        if (!app_.getHashRouter().shouldProcess(txID, id_, flags, tx_interval))
        {
            // we have seen this transaction recently
            if (any(flags & HashRouterFlags::BAD))
            {
                fee_.update(Resource::feeUselessData, "known bad");
                JLOG(p_journal_.debug()) << "Ignoring known bad tx " << txID;
            }

            // Erase only if the server has seen this tx. If the server has not
            // seen this tx then the tx could not has been queued for this peer.
            else if (eraseTxQueue && txReduceRelayEnabled())
                removeTxQueue(txID);

            overlay_.reportInboundTraffic(
                TrafficCount::category::transaction_duplicate,
                Message::messageSize(*m));

            return;
        }

        JLOG(p_journal_.debug()) << "Got tx " << txID;

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
            JLOG(p_journal_.trace())
                << "No new transactions until synchronized";
        }
        else if (
            app_.getJobQueue().getJobCount(jtTRANSACTION) >
            app_.config().MAX_TRANSACTIONS)
        {
            overlay_.incJqTransOverflow();
            JLOG(p_journal_.info()) << "Transaction queue is full";
        }
        else
        {
            app_.getJobQueue().addJob(
                jtTRANSACTION,
                "recvTransaction->checkTransaction",
                [weak = std::weak_ptr<PeerImp>(shared_from_this()),
                 flags,
                 checkSignature,
                 batch,
                 stx]() {
                    if (auto peer = weak.lock())
                        peer->checkTransaction(
                            flags, checkSignature, stx, batch);
                });
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(p_journal_.warn())
            << "Transaction invalid: " << strHex(m->rawtransaction())
            << ". Exception: " << ex.what();
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::feeInvalidData, "get_ledger " + msg);
        JLOG(p_journal_.warn()) << "TMGetLedger: " << msg;
    };
    auto const itype{m->itype()};

    // Verify ledger info type
    if (itype < protocol::liBASE || itype > protocol::liTS_CANDIDATE)
        return badData("Invalid ledger info type");

    auto const ltype = [&m]() -> std::optional<::protocol::TMLedgerType> {
        if (m->has_ltype())
            return m->ltype();
        return std::nullopt;
    }();

    if (itype == protocol::liTS_CANDIDATE)
    {
        if (!m->has_ledgerhash())
            return badData("Invalid TX candidate set, missing TX set hash");
    }
    else if (
        !m->has_ledgerhash() && !m->has_ledgerseq() &&
        !(ltype && *ltype == protocol::ltCLOSED))
    {
        return badData("Invalid request");
    }

    // Verify ledger type
    if (ltype && (*ltype < protocol::ltACCEPTED || *ltype > protocol::ltCLOSED))
        return badData("Invalid ledger type");

    // Verify ledger hash
    if (m->has_ledgerhash() && !stringIsUint256Sized(m->ledgerhash()))
        return badData("Invalid ledger hash");

    // Verify ledger sequence
    if (m->has_ledgerseq())
    {
        auto const ledgerSeq{m->ledgerseq()};

        // Check if within a reasonable range
        using namespace std::chrono_literals;
        if (app_.getLedgerMaster().getValidatedLedgerAge() <= 10s &&
            ledgerSeq > app_.getLedgerMaster().getValidLedgerIndex() + 10)
        {
            return badData(
                "Invalid ledger sequence " + std::to_string(ledgerSeq));
        }
    }

    // Verify ledger node IDs
    if (itype != protocol::liBASE)
    {
        if (m->nodeids_size() <= 0)
            return badData("Invalid ledger node IDs");

        for (auto const& nodeId : m->nodeids())
        {
            if (deserializeSHAMapNodeID(nodeId) == std::nullopt)
                return badData("Invalid SHAMap node ID");
        }
    }

    // Verify query type
    if (m->has_querytype() && m->querytype() != protocol::qtINDIRECT)
        return badData("Invalid query type");

    // Verify query depth
    if (m->has_querydepth())
    {
        if (m->querydepth() > Tuning::maxQueryDepth ||
            itype == protocol::liBASE)
        {
            return badData("Invalid query depth");
        }
    }

    // Queue a job to process the request
    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(jtLEDGER_REQ, "recvGetLedger", [weak, m]() {
        if (auto peer = weak.lock())
            peer->processLedgerRequest(m);
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m)
{
    JLOG(p_journal_.trace()) << "onMessage, TMProofPathRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(
            Resource::feeMalformedRequest, "proof_path_request disabled");
        return;
    }

    fee_.update(
        Resource::feeModerateBurdenPeer, "received a proof path request");
    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(
        jtREPLAY_REQ, "recvProofPathRequest", [weak, m]() {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processProofPathRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(
                            Resource::feeMalformedRequest,
                            "proof_path_request");
                    else
                        peer->charge(
                            Resource::feeRequestNoReply, "proof_path_request");
                }
                else
                {
                    peer->send(std::make_shared<Message>(
                        reply, protocol::mtPROOF_PATH_RESPONSE));
                }
            }
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(
            Resource::feeMalformedRequest, "proof_path_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processProofPathResponse(m))
    {
        fee_.update(Resource::feeInvalidData, "proof_path_response");
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m)
{
    JLOG(p_journal_.trace()) << "onMessage, TMReplayDeltaRequest";
    if (!ledgerReplayEnabled_)
    {
        fee_.update(
            Resource::feeMalformedRequest, "replay_delta_request disabled");
        return;
    }

    fee_.fee = Resource::feeModerateBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(
        jtREPLAY_REQ, "recvReplayDeltaRequest", [weak, m]() {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processReplayDeltaRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(
                            Resource::feeMalformedRequest,
                            "replay_delta_request");
                    else
                        peer->charge(
                            Resource::feeRequestNoReply,
                            "replay_delta_request");
                }
                else
                {
                    peer->send(std::make_shared<Message>(
                        reply, protocol::mtREPLAY_DELTA_RESPONSE));
                }
            }
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        fee_.update(
            Resource::feeMalformedRequest, "replay_delta_response disabled");
        return;
    }

    if (!ledgerReplayMsgHandler_.processReplayDeltaResponse(m))
    {
        fee_.update(Resource::feeInvalidData, "replay_delta_response");
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMLedgerData> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_.update(Resource::feeInvalidData, msg);
        JLOG(p_journal_.warn()) << "TMLedgerData: " << msg;
    };

    // Verify ledger hash
    if (!stringIsUint256Sized(m->ledgerhash()))
        return badData("Invalid ledger hash");

    // Verify ledger sequence
    {
        auto const ledgerSeq{m->ledgerseq()};
        if (m->type() == protocol::liTS_CANDIDATE)
        {
            if (ledgerSeq != 0)
            {
                return badData(
                    "Invalid ledger sequence " + std::to_string(ledgerSeq));
            }
        }
        else
        {
            // Check if within a reasonable range
            using namespace std::chrono_literals;
            if (app_.getLedgerMaster().getValidatedLedgerAge() <= 10s &&
                ledgerSeq > app_.getLedgerMaster().getValidLedgerIndex() + 10)
            {
                return badData(
                    "Invalid ledger sequence " + std::to_string(ledgerSeq));
            }
        }
    }

    // Verify ledger info type
    if (m->type() < protocol::liBASE || m->type() > protocol::liTS_CANDIDATE)
        return badData("Invalid ledger info type");

    // Verify reply error
    if (m->has_error() &&
        (m->error() < protocol::reNO_LEDGER ||
         m->error() > protocol::reBAD_REQUEST))
    {
        return badData("Invalid reply error");
    }

    // Verify ledger nodes.
    if (m->nodes_size() <= 0 || m->nodes_size() > Tuning::hardMaxReplyNodes)
    {
        return badData(
            "Invalid Ledger/TXset nodes " + std::to_string(m->nodes_size()));
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
            JLOG(p_journal_.info()) << "Unable to route TX/ledger data reply";
        }
        return;
    }

    uint256 const ledgerHash{m->ledgerhash()};

    // Otherwise check if received data for a candidate transaction set
    if (m->type() == protocol::liTS_CANDIDATE)
    {
        std::weak_ptr<PeerImp> weak{shared_from_this()};
        app_.getJobQueue().addJob(
            jtTXN_DATA, "recvPeerData", [weak, ledgerHash, m]() {
                if (auto peer = weak.lock())
                {
                    peer->app_.getInboundTransactions().gotData(
                        ledgerHash, peer, m);
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
    protocol::TMProposeSet& set = *m;

    auto const sig = makeSlice(set.signature());

    // Preliminary check for the validity of the signature: A DER encoded
    // signature can't be longer than 72 bytes.
    if ((std::clamp<std::size_t>(sig.size(), 64, 72) != sig.size()) ||
        (publicKeyType(makeSlice(set.nodepubkey())) != KeyType::secp256k1))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_.update(
            Resource::feeInvalidSignature,
            " signature can't be longer than 72 bytes");
        return;
    }

    if (!stringIsUint256Sized(set.currenttxhash()) ||
        !stringIsUint256Sized(set.previousledger()))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_.update(Resource::feeMalformedRequest, "bad hashes");
        return;
    }

    // RH TODO: when isTrusted = false we should probably also cache a key
    // suppression for 30 seconds to avoid doing a relatively expensive lookup
    // every time a spam packet is received
    PublicKey const publicKey{makeSlice(set.nodepubkey())};
    auto const isTrusted = app_.validators().trusted(publicKey);

    // If the operator has specified that untrusted proposals be dropped then
    // this happens here I.e. before further wasting CPU verifying the signature
    // of an untrusted key
    if (!isTrusted)
    {
        // report untrusted proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::category::proposal_untrusted,
            Message::messageSize(*m));

        if (app_.config().RELAY_UNTRUSTED_PROPOSALS == -1)
            return;
    }

    uint256 const proposeHash{set.currenttxhash()};
    uint256 const prevLedger{set.previousledger()};

    NetClock::time_point const closeTime{NetClock::duration{set.closetime()}};

    uint256 const suppression = proposalUniqueId(
        proposeHash,
        prevLedger,
        set.proposeseq(),
        closeTime,
        publicKey.slice(),
        sig);

    if (auto [added, relayed] =
            app_.getHashRouter().addSuppressionPeerWithStatus(suppression, id_);
        !added)
    {
        // Count unique messages (Slots has it's own 'HashRouter'), which a peer
        // receives within IDLED seconds since the message has been relayed.
        if (relayed && (stopwatch().now() - *relayed) < reduce_relay::IDLED)
            overlay_.updateSlotAndSquelch(
                suppression, publicKey, id_, protocol::mtPROPOSE_LEDGER);

        // report duplicate proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::category::proposal_duplicate,
            Message::messageSize(*m));

        JLOG(p_journal_.trace()) << "Proposal: duplicate";

        return;
    }

    if (!isTrusted)
    {
        if (tracking_.load() == Tracking::diverged)
        {
            JLOG(p_journal_.debug())
                << "Proposal: Dropping untrusted (peer divergence)";
            return;
        }

        if (!cluster() && app_.getFeeTrack().isLoadedLocal())
        {
            JLOG(p_journal_.debug()) << "Proposal: Dropping untrusted (load)";
            return;
        }
    }

    JLOG(p_journal_.trace())
        << "Proposal: " << (isTrusted ? "trusted" : "untrusted");

    auto proposal = RCLCxPeerPos(
        publicKey,
        sig,
        suppression,
        RCLCxPeerPos::Proposal{
            prevLedger,
            set.proposeseq(),
            proposeHash,
            closeTime,
            app_.timeKeeper().closeTime(),
            calcNodeID(app_.validatorManifests().getMasterKey(publicKey))});

    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(
        isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
        "recvPropose->checkPropose",
        [weak, isTrusted, m, proposal]() {
            if (auto peer = weak.lock())
                peer->checkPropose(isTrusted, m, proposal);
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMStatusChange> const& m)
{
    JLOG(p_journal_.trace()) << "Status: Change";

    if (!m->has_networktime())
        m->set_networktime(app_.timeKeeper().now().time_since_epoch().count());

    {
        std::lock_guard sl(recentLock_);
        if (!last_status_.has_newstatus() || m->has_newstatus())
            last_status_ = *m;
        else
        {
            // preserve old status
            protocol::NodeStatus status = last_status_.newstatus();
            last_status_ = *m;
            m->set_newstatus(status);
        }
    }

    if (m->newevent() == protocol::neLOST_SYNC)
    {
        bool outOfSync{false};
        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::lock_guard sl(recentLock_);
            if (!closedLedgerHash_.isZero())
            {
                outOfSync = true;
                closedLedgerHash_.zero();
            }
            previousLedgerHash_.zero();
        }
        if (outOfSync)
        {
            JLOG(p_journal_.debug()) << "Status: Out of sync";
        }
        return;
    }

    {
        uint256 closedLedgerHash{};
        bool const peerChangedLedgers{
            m->has_ledgerhash() && stringIsUint256Sized(m->ledgerhash())};

        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::lock_guard sl(recentLock_);
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

            if (m->has_ledgerhashprevious() &&
                stringIsUint256Sized(m->ledgerhashprevious()))
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
            JLOG(p_journal_.debug()) << "LCL is " << closedLedgerHash;
        }
        else
        {
            JLOG(p_journal_.debug()) << "Status: No ledger";
        }
    }

    if (m->has_firstseq() && m->has_lastseq())
    {
        std::lock_guard sl(recentLock_);

        minLedger_ = m->firstseq();
        maxLedger_ = m->lastseq();

        if ((maxLedger_ < minLedger_) || (minLedger_ == 0) || (maxLedger_ == 0))
            minLedger_ = maxLedger_ = 0;
    }

    if (m->has_ledgerseq() &&
        app_.getLedgerMaster().getValidatedLedgerAge() < 2min)
    {
        checkTracking(
            m->ledgerseq(), app_.getLedgerMaster().getValidLedgerIndex());
    }

    app_.getOPs().pubPeerStatus([=, this]() -> Json::Value {
        Json::Value j = Json::objectValue;

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
                std::lock_guard sl(recentLock_);
                closedLedgerHash = closedLedgerHash_;
            }
            j[jss::ledger_hash] = to_string(closedLedgerHash);
        }

        if (m->has_networktime())
        {
            j[jss::date] = Json::UInt(m->networktime());
        }

        if (m->has_firstseq() && m->has_lastseq())
        {
            j[jss::ledger_index_min] = Json::UInt(m->firstseq());
            j[jss::ledger_index_max] = Json::UInt(m->lastseq());
        }

        return j;
    });
}

void
PeerImp::checkTracking(std::uint32_t validationSeq)
{
    std::uint32_t serverSeq;
    {
        // Extract the sequence number of the highest
        // ledger this peer has
        std::lock_guard sl(recentLock_);

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
    int diff = std::max(seq1, seq2) - std::min(seq1, seq2);

    if (diff < Tuning::convergedLedgerLimit)
    {
        // The peer's ledger sequence is close to the validation's
        tracking_ = Tracking::converged;
    }

    if ((diff > Tuning::divergedLedgerLimit) &&
        (tracking_.load() != Tracking::diverged))
    {
        // The peer's ledger sequence is way off the validation's
        std::lock_guard sl(recentLock_);

        tracking_ = Tracking::diverged;
        trackingTime_ = clock_type::now();
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m)
{
    if (!stringIsUint256Sized(m->hash()))
    {
        fee_.update(Resource::feeMalformedRequest, "bad hash");
        return;
    }

    uint256 const hash{m->hash()};

    if (m->status() == protocol::tsHAVE)
    {
        std::lock_guard sl(recentLock_);

        if (std::find(recentTxSets_.begin(), recentTxSets_.end(), hash) !=
            recentTxSets_.end())
        {
            fee_.update(Resource::feeUselessData, "duplicate (tsHAVE)");
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
        JLOG(p_journal_.warn()) << "Ignored malformed " << messageType
                                << " from peer " << remote_address_;
        // This shouldn't ever happen with a well-behaved peer
        fee_.update(Resource::feeHeavyBurdenPeer, "no blobs");
        return;
    }

    auto const hash = sha512Half(manifest, blobs, version);

    JLOG(p_journal_.debug())
        << "Received " << messageType << " from " << remote_address_.to_string()
        << " (" << id_ << ")";

    if (!app_.getHashRouter().addSuppressionPeer(hash, id_))
    {
        JLOG(p_journal_.debug())
            << messageType << ": received duplicate " << messageType;
        // Charging this fee here won't hurt the peer in the normal
        // course of operation (ie. refresh every 5 minutes), but
        // will add up if the peer is misbehaving.
        fee_.update(Resource::feeUselessData, "duplicate");
        return;
    }

    auto const applyResult = app_.validators().applyListsAndBroadcast(
        manifest,
        version,
        blobs,
        remote_address_.to_string(),
        hash,
        app_.overlay(),
        app_.getHashRouter(),
        app_.getOPs());

    JLOG(p_journal_.debug())
        << "Processed " << messageType << " version " << version << " from "
        << (applyResult.publisherKey ? strHex(*applyResult.publisherKey)
                                     : "unknown or invalid publisher")
        << " from " << remote_address_.to_string() << " (" << id_
        << ") with best result " << to_string(applyResult.bestDisposition());

    // Act based on the best result
    switch (applyResult.bestDisposition())
    {
        // New list
        case ListDisposition::accepted:
        // Newest list is expired, and that needs to be broadcast, too
        case ListDisposition::expired:
        // Future list
        case ListDisposition::pending: {
            std::lock_guard<std::mutex> sl(recentLock_);

            XRPL_ASSERT(
                applyResult.publisherKey,
                "ripple::PeerImp::onValidatorListMessage : publisher key is "
                "set");
            auto const& pubKey = *applyResult.publisherKey;
#ifndef NDEBUG
            if (auto const iter = publisherListSequences_.find(pubKey);
                iter != publisherListSequences_.end())
            {
                XRPL_ASSERT(
                    iter->second < applyResult.sequence,
                    "ripple::PeerImp::onValidatorListMessage : lower sequence");
            }
#endif
            publisherListSequences_[pubKey] = applyResult.sequence;
        }
        break;
        case ListDisposition::same_sequence:
        case ListDisposition::known_sequence:
#ifndef NDEBUG
        {
            std::lock_guard<std::mutex> sl(recentLock_);
            XRPL_ASSERT(
                applyResult.sequence && applyResult.publisherKey,
                "ripple::PeerImp::onValidatorListMessage : nonzero sequence "
                "and set publisher key");
            XRPL_ASSERT(
                publisherListSequences_[*applyResult.publisherKey] <=
                    applyResult.sequence,
                "ripple::PeerImp::onValidatorListMessage : maximum sequence");
        }
#endif  // !NDEBUG

        break;
        case ListDisposition::stale:
        case ListDisposition::untrusted:
        case ListDisposition::invalid:
        case ListDisposition::unsupported_version:
            break;
        default:
            UNREACHABLE(
                "ripple::PeerImp::onValidatorListMessage : invalid best list "
                "disposition");
    }

    // Charge based on the worst result
    switch (applyResult.worstDisposition())
    {
        case ListDisposition::accepted:
        case ListDisposition::expired:
        case ListDisposition::pending:
            // No charges for good data
            break;
        case ListDisposition::same_sequence:
        case ListDisposition::known_sequence:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_.update(
                Resource::feeUselessData,
                " duplicate (same_sequence or known_sequence)");
            break;
        case ListDisposition::stale:
            // There are very few good reasons for a peer to send an
            // old list, particularly more than once.
            fee_.update(Resource::feeInvalidData, "expired");
            break;
        case ListDisposition::untrusted:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_.update(Resource::feeUselessData, "untrusted");
            break;
        case ListDisposition::invalid:
            // This shouldn't ever happen with a well-behaved peer
            fee_.update(
                Resource::feeInvalidSignature, "invalid list disposition");
            break;
        case ListDisposition::unsupported_version:
            // During a version transition, this may be legitimate.
            // If it happens frequently, that's probably bad.
            fee_.update(Resource::feeInvalidData, "version");
            break;
        default:
            UNREACHABLE(
                "ripple::PeerImp::onValidatorListMessage : invalid worst list "
                "disposition");
    }

    // Log based on all the results.
    for (auto const& [disp, count] : applyResult.dispositions)
    {
        switch (disp)
        {
            // New list
            case ListDisposition::accepted:
                JLOG(p_journal_.debug())
                    << "Applied " << count << " new " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            // Newest list is expired, and that needs to be broadcast, too
            case ListDisposition::expired:
                JLOG(p_journal_.debug())
                    << "Applied " << count << " expired " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            // Future list
            case ListDisposition::pending:
                JLOG(p_journal_.debug())
                    << "Processed " << count << " future " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            case ListDisposition::same_sequence:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " " << messageType
                    << "(s) with current sequence from peer "
                    << remote_address_;
                break;
            case ListDisposition::known_sequence:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " " << messageType
                    << "(s) with future sequence from peer " << remote_address_;
                break;
            case ListDisposition::stale:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "stale " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            case ListDisposition::untrusted:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " untrusted " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            case ListDisposition::unsupported_version:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "unsupported version "
                    << messageType << "(s) from peer " << remote_address_;
                break;
            case ListDisposition::invalid:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "invalid " << messageType
                    << "(s) from peer " << remote_address_;
                break;
            default:
                UNREACHABLE(
                    "ripple::PeerImp::onValidatorListMessage : invalid list "
                    "disposition");
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
            JLOG(p_journal_.debug())
                << "ValidatorList: received validator list from peer using "
                << "protocol version " << to_string(protocol_)
                << " which shouldn't support this feature.";
            fee_.update(Resource::feeUselessData, "unsupported peer");
            return;
        }
        onValidatorListMessage(
            "ValidatorList",
            m->manifest(),
            m->version(),
            ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn()) << "ValidatorList: Exception, " << e.what()
                                << " from peer " << remote_address_;
        using namespace std::string_literals;
        fee_.update(Resource::feeInvalidData, e.what());
    }
}

void
PeerImp::onMessage(
    std::shared_ptr<protocol::TMValidatorListCollection> const& m)
{
    try
    {
        if (!supportsFeature(ProtocolFeature::ValidatorList2Propagation))
        {
            JLOG(p_journal_.debug())
                << "ValidatorListCollection: received validator list from peer "
                << "using protocol version " << to_string(protocol_)
                << " which shouldn't support this feature.";
            fee_.update(Resource::feeUselessData, "unsupported peer");
            return;
        }
        else if (m->version() < 2)
        {
            JLOG(p_journal_.debug())
                << "ValidatorListCollection: received invalid validator list "
                   "version "
                << m->version() << " from peer using protocol version "
                << to_string(protocol_);
            fee_.update(Resource::feeInvalidData, "wrong version");
            return;
        }
        onValidatorListMessage(
            "ValidatorListCollection",
            m->manifest(),
            m->version(),
            ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn()) << "ValidatorListCollection: Exception, "
                                << e.what() << " from peer " << remote_address_;
        using namespace std::string_literals;
        fee_.update(Resource::feeInvalidData, e.what());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidation> const& m)
{
    if (m->validation().size() < 50)
    {
        JLOG(p_journal_.warn()) << "Validation: Too small";
        fee_.update(Resource::feeMalformedRequest, "too small");
        return;
    }

    try
    {
        auto const closeTime = app_.timeKeeper().closeTime();

        std::shared_ptr<STValidation> val;
        {
            SerialIter sit(makeSlice(m->validation()));
            val = std::make_shared<STValidation>(
                std::ref(sit),
                [this](PublicKey const& pk) {
                    return calcNodeID(
                        app_.validatorManifests().getMasterKey(pk));
                },
                false);
            val->setSeen(closeTime);
        }

        if (!isCurrent(
                app_.getValidations().parms(),
                app_.timeKeeper().closeTime(),
                val->getSignTime(),
                val->getSeenTime()))
        {
            JLOG(p_journal_.trace()) << "Validation: Not current";
            fee_.update(Resource::feeUselessData, "not current");
            return;
        }

        // RH TODO: when isTrusted = false we should probably also cache a key
        // suppression for 30 seconds to avoid doing a relatively expensive
        // lookup every time a spam packet is received
        auto const isTrusted =
            app_.validators().trusted(val->getSignerPublic());

        // If the operator has specified that untrusted validations be
        // dropped then this happens here I.e. before further wasting CPU
        // verifying the signature of an untrusted key
        if (!isTrusted)
        {
            // increase untrusted validations received
            overlay_.reportInboundTraffic(
                TrafficCount::category::validation_untrusted,
                Message::messageSize(*m));

            if (app_.config().RELAY_UNTRUSTED_VALIDATIONS == -1)
                return;
        }

        auto key = sha512Half(makeSlice(m->validation()));

        auto [added, relayed] =
            app_.getHashRouter().addSuppressionPeerWithStatus(key, id_);

        if (!added)
        {
            // Count unique messages (Slots has it's own 'HashRouter'), which a
            // peer receives within IDLED seconds since the message has been
            // relayed.
            if (relayed && (stopwatch().now() - *relayed) < reduce_relay::IDLED)
                overlay_.updateSlotAndSquelch(
                    key, val->getSignerPublic(), id_, protocol::mtVALIDATION);

            // increase duplicate validations received
            overlay_.reportInboundTraffic(
                TrafficCount::category::validation_duplicate,
                Message::messageSize(*m));

            JLOG(p_journal_.trace()) << "Validation: duplicate";
            return;
        }

        if (!isTrusted && (tracking_.load() == Tracking::diverged))
        {
            JLOG(p_journal_.debug())
                << "Dropping untrusted validation from diverged peer";
        }
        else if (isTrusted || !app_.getFeeTrack().isLoadedLocal())
        {
            std::string const name = [isTrusted, val]() {
                std::string ret =
                    isTrusted ? "Trusted validation" : "Untrusted validation";

#ifdef DEBUG
                ret += " " +
                    std::to_string(val->getFieldU32(sfLedgerSequence)) + ": " +
                    to_string(val->getNodeID());
#endif

                return ret;
            }();

            std::weak_ptr<PeerImp> weak = shared_from_this();
            app_.getJobQueue().addJob(
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                name,
                [weak, val, m, key]() {
                    if (auto peer = weak.lock())
                        peer->checkValidation(val, key, m);
                });
        }
        else
        {
            JLOG(p_journal_.debug())
                << "Dropping untrusted validation for load";
        }
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn())
            << "Exception processing validation: " << e.what();
        using namespace std::string_literals;
        fee_.update(Resource::feeMalformedRequest, e.what());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash& packet = *m;

    JLOG(p_journal_.trace()) << "received TMGetObjectByHash " << packet.type()
                             << " " << packet.objects_size();

    if (packet.query())
    {
        // this is a query
        if (send_queue_.size() >= Tuning::dropSendQueue)
        {
            JLOG(p_journal_.debug()) << "GetObject: Large send queue";
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
                JLOG(p_journal_.error())
                    << "TMGetObjectByHash: tx reduce-relay is disabled";
                fee_.update(Resource::feeMalformedRequest, "disabled");
                return;
            }

            std::weak_ptr<PeerImp> weak = shared_from_this();
            app_.getJobQueue().addJob(
                jtREQUESTED_TXN, "doTransactions", [weak, m]() {
                    if (auto peer = weak.lock())
                        peer->doTransactions(m);
                });
            return;
        }

        protocol::TMGetObjectByHash reply;

        reply.set_query(false);

        if (packet.has_seq())
            reply.set_seq(packet.seq());

        reply.set_type(packet.type());

        if (packet.has_ledgerhash())
        {
            if (!stringIsUint256Sized(packet.ledgerhash()))
            {
                fee_.update(Resource::feeMalformedRequest, "ledger hash");
                return;
            }

            reply.set_ledgerhash(packet.ledgerhash());
        }

        fee_.update(
            Resource::feeModerateBurdenPeer,
            " received a get object by hash request");

        // This is a very minimal implementation
        for (int i = 0; i < packet.objects_size(); ++i)
        {
            auto const& obj = packet.objects(i);
            if (obj.has_hash() && stringIsUint256Sized(obj.hash()))
            {
                uint256 const hash{obj.hash()};
                // VFALCO TODO Move this someplace more sensible so we dont
                //             need to inject the NodeStore interfaces.
                std::uint32_t seq{obj.has_ledgerseq() ? obj.ledgerseq() : 0};
                auto nodeObject{app_.getNodeStore().fetchNodeObject(hash, seq)};
                if (nodeObject)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects();
                    newObj.set_hash(hash.begin(), hash.size());
                    newObj.set_data(
                        &nodeObject->getData().front(),
                        nodeObject->getData().size());

                    if (obj.has_nodeid())
                        newObj.set_index(obj.nodeid());
                    if (obj.has_ledgerseq())
                        newObj.set_ledgerseq(obj.ledgerseq());

                    // VFALCO NOTE "seq" in the message is obsolete
                }
            }
        }

        JLOG(p_journal_.trace()) << "GetObj: " << reply.objects_size() << " of "
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

            if (obj.has_hash() && stringIsUint256Sized(obj.hash()))
            {
                if (obj.has_ledgerseq())
                {
                    if (obj.ledgerseq() != pLSeq)
                    {
                        if (pLDo && (pLSeq != 0))
                        {
                            JLOG(p_journal_.debug())
                                << "GetObj: Full fetch pack for " << pLSeq;
                        }
                        pLSeq = obj.ledgerseq();
                        pLDo = !app_.getLedgerMaster().haveLedger(pLSeq);

                        if (!pLDo)
                        {
                            JLOG(p_journal_.debug())
                                << "GetObj: Late fetch pack for " << pLSeq;
                        }
                        else
                            progress = true;
                    }
                }

                if (pLDo)
                {
                    uint256 const hash{obj.hash()};

                    app_.getLedgerMaster().addFetchPack(
                        hash,
                        std::make_shared<Blob>(
                            obj.data().begin(), obj.data().end()));
                }
            }
        }

        if (pLDo && (pLSeq != 0))
        {
            JLOG(p_journal_.debug())
                << "GetObj: Partial fetch pack for " << pLSeq;
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
        JLOG(p_journal_.error())
            << "TMHaveTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::feeMalformedRequest, "disabled");
        return;
    }

    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(
        jtMISSING_TXN, "handleHaveTransactions", [weak, m]() {
            if (auto peer = weak.lock())
                peer->handleHaveTransactions(m);
        });
}

void
PeerImp::handleHaveTransactions(
    std::shared_ptr<protocol::TMHaveTransactions> const& m)
{
    protocol::TMGetObjectByHash tmBH;
    tmBH.set_type(protocol::TMGetObjectByHash_ObjectType_otTRANSACTIONS);
    tmBH.set_query(true);

    JLOG(p_journal_.trace())
        << "received TMHaveTransactions " << m->hashes_size();

    for (std::uint32_t i = 0; i < m->hashes_size(); i++)
    {
        if (!stringIsUint256Sized(m->hashes(i)))
        {
            JLOG(p_journal_.error())
                << "TMHaveTransactions with invalid hash size";
            fee_.update(Resource::feeMalformedRequest, "hash size");
            return;
        }

        uint256 hash(m->hashes(i));

        auto txn = app_.getMasterTransaction().fetch_from_cache(hash);

        JLOG(p_journal_.trace()) << "checking transaction " << (bool)txn;

        if (!txn)
        {
            JLOG(p_journal_.debug()) << "adding transaction to request";

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

    JLOG(p_journal_.trace())
        << "transaction request object is " << tmBH.objects_size();

    if (tmBH.objects_size() > 0)
        send(std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS));
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransactions> const& m)
{
    if (!txReduceRelayEnabled())
    {
        JLOG(p_journal_.error())
            << "TMTransactions: tx reduce-relay is disabled";
        fee_.update(Resource::feeMalformedRequest, "disabled");
        return;
    }

    JLOG(p_journal_.trace())
        << "received TMTransactions " << m->transactions_size();

    overlay_.addTxMetrics(m->transactions_size());

    for (std::uint32_t i = 0; i < m->transactions_size(); ++i)
        handleTransaction(
            std::shared_ptr<protocol::TMTransaction>(
                m->mutable_transactions(i), [](protocol::TMTransaction*) {}),
            false,
            true);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMSquelch> const& m)
{
    using on_message_fn =
        void (PeerImp::*)(std::shared_ptr<protocol::TMSquelch> const&);
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (on_message_fn)&PeerImp::onMessage, shared_from_this(), m));

    if (!m->has_validatorpubkey())
    {
        fee_.update(Resource::feeInvalidData, "squelch no pubkey");
        return;
    }
    auto validator = m->validatorpubkey();
    auto const slice{makeSlice(validator)};
    if (!publicKeyType(slice))
    {
        fee_.update(Resource::feeInvalidData, "squelch bad pubkey");
        return;
    }
    PublicKey key(slice);

    // Ignore the squelch for validator's own messages.
    if (key == app_.getValidationPublicKey())
    {
        JLOG(p_journal_.debug())
            << "onMessage: TMSquelch discarding validator's squelch " << slice;
        return;
    }

    std::uint32_t duration =
        m->has_squelchduration() ? m->squelchduration() : 0;
    if (!m->squelch())
        squelch_.removeSquelch(key);
    else if (!squelch_.addSquelch(key, std::chrono::seconds{duration}))
        fee_.update(Resource::feeInvalidData, "squelch duration");

    JLOG(p_journal_.debug())
        << "onMessage: TMSquelch " << slice << " " << id() << " " << duration;
}

//--------------------------------------------------------------------------

void
PeerImp::addLedger(
    uint256 const& hash,
    std::lock_guard<std::mutex> const& lockedRecentLock)
{
    // lockedRecentLock is passed as a reminder that recentLock_ must be
    // locked by the caller.
    (void)lockedRecentLock;

    if (std::find(recentLedgers_.begin(), recentLedgers_.end(), hash) !=
        recentLedgers_.end())
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
        (app_.getJobQueue().getJobCount(jtPACK) > 10))
    {
        JLOG(p_journal_.info()) << "Too busy to make fetch pack";
        return;
    }

    if (!stringIsUint256Sized(packet->ledgerhash()))
    {
        JLOG(p_journal_.warn()) << "FetchPack hash size malformed";
        fee_.update(Resource::feeMalformedRequest, "hash size");
        return;
    }

    fee_.fee = Resource::feeHeavyBurdenPeer;

    uint256 const hash{packet->ledgerhash()};

    std::weak_ptr<PeerImp> weak = shared_from_this();
    auto elapsed = UptimeClock::now();
    auto const pap = &app_;
    app_.getJobQueue().addJob(
        jtPACK, "MakeFetchPack", [pap, weak, packet, hash, elapsed]() {
            pap->getLedgerMaster().makeFetchPack(weak, packet, hash, elapsed);
        });
}

void
PeerImp::doTransactions(
    std::shared_ptr<protocol::TMGetObjectByHash> const& packet)
{
    protocol::TMTransactions reply;

    JLOG(p_journal_.trace()) << "received TMGetObjectByHash requesting tx "
                             << packet->objects_size();

    if (packet->objects_size() > reduce_relay::MAX_TX_QUEUE_SIZE)
    {
        JLOG(p_journal_.error()) << "doTransactions, invalid number of hashes";
        fee_.update(Resource::feeMalformedRequest, "too big");
        return;
    }

    for (std::uint32_t i = 0; i < packet->objects_size(); ++i)
    {
        auto const& obj = packet->objects(i);

        if (!stringIsUint256Sized(obj.hash()))
        {
            fee_.update(Resource::feeMalformedRequest, "hash size");
            return;
        }

        uint256 hash(obj.hash());

        auto txn = app_.getMasterTransaction().fetch_from_cache(hash);

        if (!txn)
        {
            JLOG(p_journal_.error()) << "doTransactions, transaction not found "
                                     << Slice(hash.data(), hash.size());
            fee_.update(Resource::feeMalformedRequest, "tx not found");
            return;
        }

        Serializer s;
        auto tx = reply.add_transactions();
        auto sttx = txn->getSTransaction();
        sttx->add(s);
        tx->set_rawtransaction(s.data(), s.size());
        tx->set_status(
            txn->getStatus() == INCLUDED ? protocol::tsCURRENT
                                         : protocol::tsNEW);
        tx->set_receivetimestamp(
            app_.timeKeeper().now().time_since_epoch().count());
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
        if (stx->isFlag(tfInnerBatchTxn) &&
            getCurrentTransactionRules()->enabled(featureBatch))
        {
            JLOG(p_journal_.warn()) << "Ignoring Network relayed Tx containing "
                                       "tfInnerBatchTxn (checkSignature).";
            charge(Resource::feeModerateBurdenPeer, "inner batch txn");
            return;
        }
        // LCOV_EXCL_STOP

        // Expired?
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32(sfLastLedgerSequence) <
             app_.getLedgerMaster().getValidLedgerIndex()))
        {
            app_.getHashRouter().setFlags(
                stx->getTransactionID(), HashRouterFlags::BAD);
            charge(Resource::feeUselessData, "expired tx");
            return;
        }

        if (isPseudoTx(*stx))
        {
            // Don't do anything with pseudo transactions except put them in the
            // TransactionMaster cache
            std::string reason;
            auto tx = std::make_shared<Transaction>(stx, reason, app_);
            XRPL_ASSERT(
                tx->getStatus() == NEW,
                "ripple::PeerImp::checkTransaction Transaction created "
                "correctly");
            if (tx->getStatus() == NEW)
            {
                JLOG(p_journal_.debug())
                    << "Processing " << (batch ? "batch" : "unsolicited")
                    << " pseudo-transaction tx " << tx->getID();

                app_.getMasterTransaction().canonicalize(&tx);
                // Tell the overlay about it, but don't relay it.
                auto const toSkip =
                    app_.getHashRouter().shouldRelay(tx->getID());
                if (toSkip)
                {
                    JLOG(p_journal_.debug())
                        << "Passing skipped pseudo pseudo-transaction tx "
                        << tx->getID();
                    app_.overlay().relay(tx->getID(), {}, *toSkip);
                }
                if (!batch)
                {
                    JLOG(p_journal_.debug())
                        << "Charging for pseudo-transaction tx " << tx->getID();
                    charge(Resource::feeUselessData, "pseudo tx");
                }

                return;
            }
        }

        if (checkSignature)
        {
            // Check the signature before handing off to the job queue.
            if (auto [valid, validReason] = checkValidity(
                    app_.getHashRouter(),
                    *stx,
                    app_.getLedgerMaster().getValidatedRules(),
                    app_.config());
                valid != Validity::Valid)
            {
                if (!validReason.empty())
                {
                    JLOG(p_journal_.trace())
                        << "Exception checking transaction: " << validReason;
                }

                // Probably not necessary to set HashRouterFlags::BAD, but
                // doesn't hurt.
                app_.getHashRouter().setFlags(
                    stx->getTransactionID(), HashRouterFlags::BAD);
                charge(
                    Resource::feeInvalidSignature,
                    "check transaction signature failure");
                return;
            }
        }
        else
        {
            forceValidity(
                app_.getHashRouter(), stx->getTransactionID(), Validity::Valid);
        }

        std::string reason;
        auto tx = std::make_shared<Transaction>(stx, reason, app_);

        if (tx->getStatus() == INVALID)
        {
            if (!reason.empty())
            {
                JLOG(p_journal_.trace())
                    << "Exception checking transaction: " << reason;
            }
            app_.getHashRouter().setFlags(
                stx->getTransactionID(), HashRouterFlags::BAD);
            charge(Resource::feeInvalidSignature, "tx (impossible)");
            return;
        }

        bool const trusted = any(flags & HashRouterFlags::TRUSTED);
        app_.getOPs().processTransaction(
            tx, trusted, false, NetworkOPs::FailHard::no);
    }
    catch (std::exception const& ex)
    {
        JLOG(p_journal_.warn())
            << "Exception in " << __func__ << ": " << ex.what();
        app_.getHashRouter().setFlags(
            stx->getTransactionID(), HashRouterFlags::BAD);
        using namespace std::string_literals;
        charge(Resource::feeInvalidData, "tx "s + ex.what());
    }
}

// Called from our JobQueue
void
PeerImp::checkPropose(
    bool isTrusted,
    std::shared_ptr<protocol::TMProposeSet> const& packet,
    RCLCxPeerPos peerPos)
{
    JLOG(p_journal_.trace())
        << "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    XRPL_ASSERT(packet, "ripple::PeerImp::checkPropose : non-null packet");

    if (!cluster() && !peerPos.checkSign())
    {
        std::string desc{"Proposal fails sig check"};
        JLOG(p_journal_.warn()) << desc;
        charge(Resource::feeInvalidSignature, desc);
        return;
    }

    bool relay;

    if (isTrusted)
        relay = app_.getOPs().processTrustedProposal(peerPos);
    else
        relay = app_.config().RELAY_UNTRUSTED_PROPOSALS == 1 || cluster();

    if (relay)
    {
        // haveMessage contains peers, which are suppressed; i.e. the peers
        // are the source of the message, consequently the message should
        // not be relayed to these peers. But the message must be counted
        // as part of the squelch logic.
        auto haveMessage = app_.overlay().relay(
            *packet, peerPos.suppressionID(), peerPos.publicKey());
        if (!haveMessage.empty())
            overlay_.updateSlotAndSquelch(
                peerPos.suppressionID(),
                peerPos.publicKey(),
                std::move(haveMessage),
                protocol::mtPROPOSE_LEDGER);
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
        std::string desc{"Validation forwarded by peer is invalid"};
        JLOG(p_journal_.debug()) << desc;
        charge(Resource::feeInvalidSignature, desc);
        return;
    }

    // FIXME it should be safe to remove this try/catch. Investigate codepaths.
    try
    {
        if (app_.getOPs().recvValidation(val, std::to_string(id())) ||
            cluster())
        {
            // haveMessage contains peers, which are suppressed; i.e. the peers
            // are the source of the message, consequently the message should
            // not be relayed to these peers. But the message must be counted
            // as part of the squelch logic.
            auto haveMessage =
                overlay_.relay(*packet, key, val->getSignerPublic());
            if (!haveMessage.empty())
            {
                overlay_.updateSlotAndSquelch(
                    key,
                    val->getSignerPublic(),
                    std::move(haveMessage),
                    protocol::mtVALIDATION);
            }
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(p_journal_.trace())
            << "Exception processing validation: " << ex.what();
        using namespace std::string_literals;
        charge(Resource::feeMalformedRequest, "validation "s + ex.what());
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

    ov.for_each([&](std::shared_ptr<PeerImp>&& p) {
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

    ov.for_each([&](std::shared_ptr<PeerImp>&& p) {
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
    JLOG(p_journal_.trace()) << "sendLedgerBase: Base data";

    Serializer s(sizeof(LedgerInfo));
    addRaw(ledger->info(), s);
    ledgerData.add_nodes()->set_nodedata(s.getDataPtr(), s.getLength());

    auto const& stateMap{ledger->stateMap()};
    if (stateMap.getHash() != beast::zero)
    {
        // Return account state root node if possible
        Serializer root(768);

        stateMap.serializeRoot(root);
        ledgerData.add_nodes()->set_nodedata(
            root.getDataPtr(), root.getLength());

        if (ledger->info().txHash != beast::zero)
        {
            auto const& txMap{ledger->txMap()};
            if (txMap.getHash() != beast::zero)
            {
                // Return TX root node if possible
                root.erase();
                txMap.serializeRoot(root);
                ledgerData.add_nodes()->set_nodedata(
                    root.getDataPtr(), root.getLength());
            }
        }
    }

    auto message{
        std::make_shared<Message>(ledgerData, protocol::mtLEDGER_DATA)};
    send(message);
}

std::shared_ptr<Ledger const>
PeerImp::getLedger(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    JLOG(p_journal_.trace()) << "getLedger: Ledger";

    std::shared_ptr<Ledger const> ledger;

    if (m->has_ledgerhash())
    {
        // Attempt to find ledger by hash
        uint256 const ledgerHash{m->ledgerhash()};
        ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
        if (!ledger)
        {
            JLOG(p_journal_.trace())
                << "getLedger: Don't have ledger with hash " << ledgerHash;

            if (m->has_querytype() && !m->has_requestcookie())
            {
                // Attempt to relay the request to a peer
                if (auto const peer = getPeerWithLedger(
                        overlay_,
                        ledgerHash,
                        m->has_ledgerseq() ? m->ledgerseq() : 0,
                        this))
                {
                    m->set_requestcookie(id());
                    peer->send(
                        std::make_shared<Message>(*m, protocol::mtGET_LEDGER));
                    JLOG(p_journal_.debug())
                        << "getLedger: Request relayed to peer";
                    return ledger;
                }

                JLOG(p_journal_.trace())
                    << "getLedger: Failed to find peer to relay request";
            }
        }
    }
    else if (m->has_ledgerseq())
    {
        // Attempt to find ledger by sequence
        if (m->ledgerseq() < app_.getLedgerMaster().getEarliestFetch())
        {
            JLOG(p_journal_.debug())
                << "getLedger: Early ledger sequence request";
        }
        else
        {
            ledger = app_.getLedgerMaster().getLedgerBySeq(m->ledgerseq());
            if (!ledger)
            {
                JLOG(p_journal_.debug())
                    << "getLedger: Don't have ledger with sequence "
                    << m->ledgerseq();
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
        auto const ledgerSeq{ledger->info().seq};
        if (m->has_ledgerseq())
        {
            if (ledgerSeq != m->ledgerseq())
            {
                // Do not resource charge a peer responding to a relay
                if (!m->has_requestcookie())
                    charge(
                        Resource::feeMalformedRequest, "get_ledger ledgerSeq");

                ledger.reset();
                JLOG(p_journal_.warn())
                    << "getLedger: Invalid ledger sequence " << ledgerSeq;
            }
        }
        else if (ledgerSeq < app_.getLedgerMaster().getEarliestFetch())
        {
            ledger.reset();
            JLOG(p_journal_.debug())
                << "getLedger: Early ledger sequence request " << ledgerSeq;
        }
    }
    else
    {
        JLOG(p_journal_.debug()) << "getLedger: Unable to find ledger";
    }

    return ledger;
}

std::shared_ptr<SHAMap const>
PeerImp::getTxSet(std::shared_ptr<protocol::TMGetLedger> const& m) const
{
    JLOG(p_journal_.trace()) << "getTxSet: TX set";

    uint256 const txSetHash{m->ledgerhash()};
    std::shared_ptr<SHAMap> shaMap{
        app_.getInboundTransactions().getSet(txSetHash, false)};
    if (!shaMap)
    {
        if (m->has_querytype() && !m->has_requestcookie())
        {
            // Attempt to relay the request to a peer
            if (auto const peer = getPeerWithTree(overlay_, txSetHash, this))
            {
                m->set_requestcookie(id());
                peer->send(
                    std::make_shared<Message>(*m, protocol::mtGET_LEDGER));
                JLOG(p_journal_.debug()) << "getTxSet: Request relayed";
            }
            else
            {
                JLOG(p_journal_.debug())
                    << "getTxSet: Failed to find relay peer";
            }
        }
        else
        {
            JLOG(p_journal_.debug()) << "getTxSet: Failed to find TX set";
        }
    }

    return shaMap;
}

void
PeerImp::processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    // Do not resource charge a peer responding to a relay
    if (!m->has_requestcookie())
        charge(
            Resource::feeModerateBurdenPeer, "received a get ledger request");

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
        if (send_queue_.size() >= Tuning::dropSendQueue)
        {
            JLOG(p_journal_.debug())
                << "processLedgerRequest: Large send queue";
            return;
        }
        if (app_.getFeeTrack().isLoadedLocal() && !cluster())
        {
            JLOG(p_journal_.debug()) << "processLedgerRequest: Too busy";
            return;
        }

        if (ledger = getLedger(m); !ledger)
            return;

        // Fill out the reply
        auto const ledgerHash{ledger->info().hash};
        ledgerData.set_ledgerhash(ledgerHash.begin(), ledgerHash.size());
        ledgerData.set_ledgerseq(ledger->info().seq);
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
                JLOG(p_journal_.trace()) << "processLedgerRequest: TX map hash "
                                         << to_string(map->getHash());
                break;

            case protocol::liAS_NODE:
                map = &ledger->stateMap();
                JLOG(p_journal_.trace())
                    << "processLedgerRequest: Account state map hash "
                    << to_string(map->getHash());
                break;

            default:
                // This case should not be possible here
                JLOG(p_journal_.error())
                    << "processLedgerRequest: Invalid ledger info type";
                return;
        }
    }

    if (!map)
    {
        JLOG(p_journal_.warn()) << "processLedgerRequest: Unable to find map";
        return;
    }

    // Add requested node data to reply
    if (m->nodeids_size() > 0)
    {
        auto const queryDepth{
            m->has_querydepth() ? m->querydepth() : (isHighLatency() ? 2 : 1)};

        std::vector<std::pair<SHAMapNodeID, Blob>> data;

        for (int i = 0; i < m->nodeids_size() &&
             ledgerData.nodes_size() < Tuning::softMaxReplyNodes;
             ++i)
        {
            auto const shaMapNodeId{deserializeSHAMapNodeID(m->nodeids(i))};

            data.clear();
            data.reserve(Tuning::softMaxReplyNodes);

            try
            {
                if (map->getNodeFat(*shaMapNodeId, data, fatLeaves, queryDepth))
                {
                    JLOG(p_journal_.trace())
                        << "processLedgerRequest: getNodeFat got "
                        << data.size() << " nodes";

                    for (auto const& d : data)
                    {
                        if (ledgerData.nodes_size() >=
                            Tuning::hardMaxReplyNodes)
                            break;
                        protocol::TMLedgerNode* node{ledgerData.add_nodes()};
                        node->set_nodeid(d.first.getRawString());
                        node->set_nodedata(d.second.data(), d.second.size());
                    }
                }
                else
                {
                    JLOG(p_journal_.warn())
                        << "processLedgerRequest: getNodeFat returns false";
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

                JLOG(p_journal_.warn())
                    << "processLedgerRequest: getNodeFat with nodeId "
                    << *shaMapNodeId << " and ledger info type " << info
                    << " throws exception: " << e.what();
            }
        }

        JLOG(p_journal_.info())
            << "processLedgerRequest: Got request for " << m->nodeids_size()
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
    static int const spRandomMax = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static int const spHaveItem = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static int const spLatency = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static int const spNoLatency = 8000;

    int score = rand_int(spRandomMax);

    if (haveItem)
        score += spHaveItem;

    std::optional<std::chrono::milliseconds> latency;
    {
        std::lock_guard sl(recentLock_);
        latency = latency_;
    }

    if (latency)
        score -= latency->count() * spLatency;
    else
        score -= spNoLatency;

    return score;
}

bool
PeerImp::isHighLatency() const
{
    std::lock_guard sl(recentLock_);
    return latency_ >= peerHighLatency;
}

void
PeerImp::Metrics::add_message(std::uint64_t bytes)
{
    using namespace std::chrono_literals;
    std::unique_lock lock{mutex_};

    totalBytes_ += bytes;
    accumBytes_ += bytes;
    auto const timeElapsed = clock_type::now() - intervalStart_;
    auto const timeElapsedInSecs =
        std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avgBytes = accumBytes_ / timeElapsedInSecs.count();
        rollingAvg_.push_back(avgBytes);

        auto const totalBytes =
            std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0ull);
        rollingAvgBytes_ = totalBytes / rollingAvg_.size();

        intervalStart_ = clock_type::now();
        accumBytes_ = 0;
    }
}

std::uint64_t
PeerImp::Metrics::average_bytes() const
{
    std::shared_lock lock{mutex_};
    return rollingAvgBytes_;
}

std::uint64_t
PeerImp::Metrics::total_bytes() const
{
    std::shared_lock lock{mutex_};
    return totalBytes_;
}

}  // namespace ripple
