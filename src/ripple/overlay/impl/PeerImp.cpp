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

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/UptimeClock.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/random.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/digest.h>

#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/ostream.hpp>

#include <algorithm>
#include <memory>
#include <numeric>
#include <sstream>

using namespace std::chrono_literals;

namespace ripple {

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
    , m_inbound(true)
    , protocol_(protocol)
    , state_(State::active)
    , sanity_(Sanity::unknown)
    , insaneTime_(clock_type::now())
    , publicKey_(publicKey)
    , creationTime_(clock_type::now())
    , usage_(consumer)
    , fee_(Resource::feeLightPeer)
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
          headers_["X-Offer-Compression"] == "lz4" ? Compressed::On
                                                   : Compressed::Off)
{
}

PeerImp::~PeerImp()
{
    const bool inCluster{cluster()};

    if (state_ == State::active)
        overlay_.onPeerDeactivate(id_);
    overlay_.peerFinder().on_closed(slot_);
    overlay_.remove(slot_);

    if (inCluster)
    {
        JLOG(journal_.warn()) << getName() << " left cluster";
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

    // We need to decipher
    auto parseLedgerHash =
        [](std::string const& value) -> boost::optional<uint256> {
        uint256 ret;
        if (ret.SetHexExact(value))
            return {ret};

        auto const s = base64_decode(value);
        if (s.size() != uint256::size())
            return boost::none;
        return uint256{s};
    };

    boost::optional<uint256> closed;
    boost::optional<uint256> previous;

    if (auto const iter = headers_.find("Closed-Ledger");
        iter != headers_.end())
    {
        closed = parseLedgerHash(iter->value().to_string());

        if (!closed)
            fail("Malformed handshake data (1)");
    }

    if (auto const iter = headers_.find("Previous-Ledger");
        iter != headers_.end())
    {
        previous = parseLedgerHash(iter->value().to_string());

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

    if (m_inbound)
    {
        doAccept();
    }
    else
    {
        assert(state_ == State::active);
        // XXX Set timer: connection is in grace period to be useful.
        // XXX Set timer: connection idle (idle may vary depending on connection
        // type.)
        doProtocolStart();
    }

    // Request shard info from peer
    protocol::TMGetPeerShardInfo tmGPS;
    tmGPS.set_hops(0);
    send(std::make_shared<Message>(tmGPS, protocol::mtGET_PEER_SHARD_INFO));

    setTimer();
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
        if (m_inbound)
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

    overlay_.reportTraffic(
        safe_cast<TrafficCount::category>(m->getCategory()),
        false,
        static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    auto sendq_size = send_queue_.size();

    if (sendq_size < Tuning::targetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        large_sendq_ = 0;
    }
    else if (
        journal_.active(beast::severities::kDebug) &&
        (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const name{getName()};
        JLOG(journal_.debug())
            << (name.empty() ? remote_address_.to_string() : name)
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
PeerImp::charge(Resource::Charge const& fee)
{
    if ((usage_.charge(fee) == Resource::drop) && usage_.disconnect() &&
        strand_.running_in_this_thread())
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
    if (m_inbound)
        return headers_["User-Agent"].to_string();
    return headers_["Server"].to_string();
}

Json::Value
PeerImp::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::public_key] = toBase58(TokenType::NodePublic, publicKey_);
    ret[jss::address] = remote_address_.to_string();

    if (m_inbound)
        ret[jss::inbound] = true;

    if (cluster())
    {
        ret[jss::cluster] = true;

        std::string name{getName()};
        if (!name.empty())
            ret[jss::name] = std::move(name);
    }

    ret[jss::load] = usage_.balance();

    {
        auto const version = getVersion();
        if (!version.empty())
            ret[jss::version] = version;
    }

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

    switch (sanity_.load())
    {
        case Sanity::insane:
            ret[jss::sanity] = "insane";
            break;

        case Sanity::unknown:
            ret[jss::sanity] = "unknown";
            break;

        case Sanity::sane:
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
            (sanity_.load() == Sanity::sane))
            return true;
        if (std::find(recentLedgers_.begin(), recentLedgers_.end(), hash) !=
            recentLedgers_.end())
            return true;
    }

    return seq >= app_.getNodeStore().earliestLedgerSeq() &&
        hasShard(NodeStore::seqToShardIndex(seq));
}

void
PeerImp::ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const
{
    std::lock_guard sl(recentLock_);

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

bool
PeerImp::hasShard(std::uint32_t shardIndex) const
{
    std::lock_guard l{shardInfoMutex_};
    auto const it{shardInfo_.find(publicKey_)};
    if (it != shardInfo_.end())
        return boost::icl::contains(it->second.shardIndexes, shardIndex);
    return false;
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
    return (sanity_ != Sanity::insane) && (uMin >= minLedger_) &&
        (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

void
PeerImp::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        timer_.cancel(ec);
        socket_.close(ec);
        overlay_.incPeerDisconnect();
        if (m_inbound)
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
                (void (Peer::*)(std::string const&)) & PeerImp::fail,
                shared_from_this(),
                reason));
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        std::string const name{getName()};
        JLOG(journal_.warn())
            << (name.empty() ? remote_address_.to_string() : name)
            << " failed: " << reason;
    }
    close();
}

void
PeerImp::fail(std::string const& name, error_code ec)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

boost::optional<RangeSet<std::uint32_t>>
PeerImp::getShardIndexes() const
{
    std::lock_guard l{shardInfoMutex_};
    auto it{shardInfo_.find(publicKey_)};
    if (it != shardInfo_.end())
        return it->second.shardIndexes;
    return boost::none;
}

boost::optional<hash_map<PublicKey, PeerImp::ShardInfo>>
PeerImp::getPeerShardInfo() const
{
    std::lock_guard l{shardInfoMutex_};
    if (!shardInfo_.empty())
        return shardInfo_;
    return boost::none;
}

void
PeerImp::gracefulClose()
{
    assert(strand_.running_in_this_thread());
    assert(socket_.is_open());
    assert(!gracefulClose_);
    gracefulClose_ = true;
#if 0
    // Flush messages
    while(send_queue_.size() > 1)
        send_queue_.pop_back();
#endif
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
    timer_.expires_from_now(std::chrono::seconds(Tuning::timerSeconds), ec);

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

    bool failedNoPing{false};
    boost::optional<std::uint32_t> pingSeq;
    // Operations on lastPingSeq_, lastPingTime_, no_ping_, and latency_
    // must be guarded by recentLock_.
    {
        std::lock_guard sl(recentLock_);
        if (no_ping_++ >= Tuning::noPing)
        {
            failedNoPing = true;
        }
        else if (!lastPingSeq_)
        {
            // Make the sequence unpredictable enough to prevent guessing
            lastPingSeq_ = rand_int<std::uint32_t>();
            lastPingTime_ = clock_type::now();
            pingSeq = lastPingSeq_;
        }
        else
        {
            // We have an outstanding ping, raise latency
            auto const minLatency =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    clock_type::now() - lastPingTime_);

            if (latency_ < minLatency)
                latency_ = minLatency;
        }
    }

    if (failedNoPing)
    {
        fail("No ping reply received");
        return;
    }

    if (pingSeq)
    {
        protocol::TMPing message;
        message.set_type(protocol::TMPing::ptPING);
        message.set_seq(*pingSeq);

        send(std::make_shared<Message>(message, protocol::mtPING));
    }

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
    assert(read_buffer_.size() == 0);

    JLOG(journal_.debug()) << "doAccept: " << remote_address_;

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    // TODO Apply headers to connection state.

    boost::beast::ostream(write_buffer_) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        remote_address_.address(),
        *sharedValue);

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    if (auto member = app_.cluster().member(publicKey_))
    {
        {
            std::unique_lock<std::shared_timed_mutex> lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    overlay_.activate(shared_from_this());

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    onWriteResponse(error_code(), 0);
}

http_response_type
PeerImp::makeResponse(
    bool crawl,
    http_request_type const& req,
    beast::IP::Address remote_ip,
    uint256 const& sharedValue)
{
    http_response_type resp;
    resp.result(boost::beast::http::status::switching_protocols);
    resp.version(req.version());
    resp.insert("Connection", "Upgrade");
    resp.insert("Upgrade", to_string(protocol_));
    resp.insert("Connect-As", "Peer");
    resp.insert("Server", BuildInfo::getFullVersionString());
    resp.insert("Crawl", crawl ? "public" : "private");
    if (req["X-Offer-Compression"] == "lz4" && app_.config().COMPRESSION)
        resp.insert("X-Offer-Compression", "lz4");

    buildHandshake(
        resp,
        sharedValue,
        overlay_.setup().networkID,
        overlay_.setup().public_ip,
        remote_ip,
        app_);

    return resp;
}

// Called repeatedly to send the bytes in the response
void
PeerImp::onWriteResponse(error_code ec, std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWriteResponse", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onWriteResponse: " << bytes_transferred << " bytes";
        else
            stream << "onWriteResponse";
    }

    write_buffer_.consume(bytes_transferred);
    if (write_buffer_.size() == 0)
        return doProtocolStart();

    stream_.async_write_some(
        write_buffer_.data(),
        bind_executor(
            strand_,
            std::bind(
                &PeerImp::onWriteResponse,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

std::string
PeerImp::getName() const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{nameMutex_};
    return name_;
}

//------------------------------------------------------------------------------

// Protocol logic

void
PeerImp::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    // Send all the validator lists that have been loaded
    if (supportsFeature(ProtocolFeature::ValidatorListPropagation))
    {
        app_.validators().for_each_available([&](std::string const& manifest,
                                                 std::string const& blob,
                                                 std::string const& signature,
                                                 std::uint32_t version,
                                                 PublicKey const& pubKey,
                                                 std::size_t sequence,
                                                 uint256 const& hash) {
            protocol::TMValidatorList vl;

            vl.set_manifest(manifest);
            vl.set_blob(blob);
            vl.set_signature(signature);
            vl.set_version(version);

            JLOG(p_journal_.debug())
                << "Sending validator list for " << strHex(pubKey)
                << " with sequence " << sequence << " to "
                << remote_address_.to_string() << " (" << id_ << ")";
            auto m = std::make_shared<Message>(vl, protocol::mtVALIDATORLIST);
            send(m);
            // Don't send it next time.
            app_.getHashRouter().addSuppressionPeer(hash, id_);
            setPublisherListSequence(pubKey, sequence);
        });
    }

    protocol::TMManifests tm;

    app_.validatorManifests().for_each_manifest(
        [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
        [&tm, &hr = app_.getHashRouter()](Manifest const& manifest) {
            auto const& s = manifest.serialized;
            auto& tm_e = *tm.add_list();
            tm_e.set_stobject(s.data(), s.size());
            hr.addSuppression(manifest.hash());
        });

    if (tm.list_size() > 0)
    {
        auto m = std::make_shared<Message>(tm, protocol::mtMANIFESTS);
        send(m);
    }
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

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;
        std::tie(bytes_consumed, ec) =
            invokeProtocolMessage(read_buffer_.data(), *this);
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
        read_buffer_.prepare(Tuning::readBufferBytes),
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

    assert(!send_queue_.empty());
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
    std::size_t size)
{
    load_event_ =
        app_.getJobQueue().makeLoadEvent(jtPEER, protocolMessageName(type));
    fee_ = Resource::feeLightPeer;
    overlay_.reportTraffic(
        TrafficCount::categorize(*m, type, true), true, static_cast<int>(size));
}

void
PeerImp::onMessageEnd(
    std::uint16_t,
    std::shared_ptr<::google::protobuf::Message> const&)
{
    load_event_.reset();
    charge(fee_);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMManifests> const& m)
{
    // VFALCO What's the right job type?
    auto that = shared_from_this();
    app_.getJobQueue().addJob(
        jtVALIDATION_ut, "receiveManifests", [this, that, m](Job&) {
            overlay_.onManifests(m, that);
        });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPing> const& m)
{
    if (m->type() == protocol::TMPing::ptPING)
    {
        // We have received a ping request, reply with a pong
        fee_ = Resource::feeMediumBurdenPeer;
        m->set_type(protocol::TMPing::ptPONG);
        send(std::make_shared<Message>(*m, protocol::mtPING));
        return;
    }

    if (m->type() == protocol::TMPing::ptPONG)
    {
        // Operations on lastPingSeq_, lastPingTime_, no_ping_, and latency_
        // must be guarded by recentLock_.
        std::lock_guard sl(recentLock_);

        if (m->has_seq() && m->seq() == lastPingSeq_)
        {
            no_ping_ = 0;

            // Only reset the ping sequence if we actually received a
            // PONG with the correct cookie. That way, any peers which
            // respond with incorrect cookies will eventually time out.
            lastPingSeq_.reset();

            // Update latency estimate
            auto const estimate =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    clock_type::now() - lastPingTime_);

            // Calculate the cumulative moving average of the latency:
            if (latency_)
                latency_ = (*latency_ * 7 + estimate) / 8;
            else
                latency_ = estimate;
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
        fee_ = Resource::feeUnwantedData;
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
        overlay_.resourceManager().importConsumers(getName(), gossip);
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
PeerImp::onMessage(std::shared_ptr<protocol::TMGetShardInfo> const& m)
{
    // DEPRECATED
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMShardInfo> const& m)
{
    // DEPRECATED
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetPeerShardInfo> const& m)
{
    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    if (m->hops() > csHopLimit)
        return badData("Invalid hops: " + std::to_string(m->hops()));
    if (m->peerchain_size() > csHopLimit)
        return badData("Invalid peer chain");

    // Reply with shard info we may have
    if (auto shardStore = app_.getShardStore())
    {
        fee_ = Resource::feeLightPeer;
        auto shards{shardStore->getCompleteShards()};
        if (!shards.empty())
        {
            protocol::TMPeerShardInfo reply;
            reply.set_shardindexes(shards);

            if (m->has_lastlink())
                reply.set_lastlink(true);

            if (m->peerchain_size() > 0)
                *reply.mutable_peerchain() = m->peerchain();

            send(std::make_shared<Message>(reply, protocol::mtPEER_SHARD_INFO));

            JLOG(p_journal_.trace()) << "Sent shard indexes " << shards;
        }
    }

    // Relay request to peers
    if (m->hops() > 0)
    {
        fee_ = Resource::feeMediumBurdenPeer;

        m->set_hops(m->hops() - 1);
        if (m->hops() == 0)
            m->set_lastlink(true);

        m->add_peerchain()->set_nodepubkey(
            publicKey_.data(), publicKey_.size());

        overlay_.foreach(send_if_not(
            std::make_shared<Message>(*m, protocol::mtGET_PEER_SHARD_INFO),
            match_peer(this)));
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPeerShardInfo> const& m)
{
    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    if (m->shardindexes().empty())
        return badData("Missing shard indexes");
    if (m->peerchain_size() > csHopLimit)
        return badData("Invalid peer chain");
    if (m->has_nodepubkey() && !publicKeyType(makeSlice(m->nodepubkey())))
        return badData("Invalid public key");

    // Check if the message should be forwarded to another peer
    if (m->peerchain_size() > 0)
    {
        // Get the Public key of the last link in the peer chain
        auto const s{
            makeSlice(m->peerchain(m->peerchain_size() - 1).nodepubkey())};
        if (!publicKeyType(s))
            return badData("Invalid pubKey");
        PublicKey peerPubKey(s);

        if (auto peer = overlay_.findPeerByPublicKey(peerPubKey))
        {
            if (!m->has_nodepubkey())
                m->set_nodepubkey(publicKey_.data(), publicKey_.size());

            if (!m->has_endpoint())
            {
                // Check if peer will share IP publicly
                if (crawl())
                    m->set_endpoint(remote_address_.address().to_string());
                else
                    m->set_endpoint("0");
            }

            m->mutable_peerchain()->RemoveLast();
            peer->send(
                std::make_shared<Message>(*m, protocol::mtPEER_SHARD_INFO));

            JLOG(p_journal_.trace())
                << "Relayed TMPeerShardInfo to peer with IP "
                << remote_address_.address().to_string();
        }
        else
        {
            // Peer is no longer available so the relay ends
            fee_ = Resource::feeUnwantedData;
            JLOG(p_journal_.info()) << "Unable to route shard info";
        }
        return;
    }

    // Parse the shard indexes received in the shard info
    RangeSet<std::uint32_t> shardIndexes;
    {
        if (!from_string(shardIndexes, m->shardindexes()))
            return badData("Invalid shard indexes");

        std::uint32_t earliestShard;
        boost::optional<std::uint32_t> latestShard;
        {
            auto const curLedgerSeq{
                app_.getLedgerMaster().getCurrentLedgerIndex()};
            if (auto shardStore = app_.getShardStore())
            {
                earliestShard = shardStore->earliestShardIndex();
                if (curLedgerSeq >= shardStore->earliestLedgerSeq())
                    latestShard = shardStore->seqToShardIndex(curLedgerSeq);
            }
            else
            {
                auto const earliestLedgerSeq{
                    app_.getNodeStore().earliestLedgerSeq()};
                earliestShard = NodeStore::seqToShardIndex(earliestLedgerSeq);
                if (curLedgerSeq >= earliestLedgerSeq)
                    latestShard = NodeStore::seqToShardIndex(curLedgerSeq);
            }
        }

        if (boost::icl::first(shardIndexes) < earliestShard ||
            (latestShard && boost::icl::last(shardIndexes) > latestShard))
        {
            return badData("Invalid shard indexes");
        }
    }

    // Get the IP of the node reporting the shard info
    beast::IP::Endpoint endpoint;
    if (m->has_endpoint())
    {
        if (m->endpoint() != "0")
        {
            auto result =
                beast::IP::Endpoint::from_string_checked(m->endpoint());
            if (!result)
                return badData("Invalid incoming endpoint: " + m->endpoint());
            endpoint = std::move(*result);
        }
    }
    else if (crawl())  // Check if peer will share IP publicly
    {
        endpoint = remote_address_;
    }

    // Get the Public key of the node reporting the shard info
    PublicKey publicKey;
    if (m->has_nodepubkey())
        publicKey = PublicKey(makeSlice(m->nodepubkey()));
    else
        publicKey = publicKey_;

    {
        std::lock_guard l{shardInfoMutex_};
        auto it{shardInfo_.find(publicKey)};
        if (it != shardInfo_.end())
        {
            // Update the IP address for the node
            it->second.endpoint = std::move(endpoint);

            // Join the shard index range set
            it->second.shardIndexes += shardIndexes;
        }
        else
        {
            // Add a new node
            ShardInfo shardInfo;
            shardInfo.endpoint = std::move(endpoint);
            shardInfo.shardIndexes = std::move(shardIndexes);
            shardInfo_.emplace(publicKey, std::move(shardInfo));
        }
    }

    JLOG(p_journal_.trace())
        << "Consumed TMPeerShardInfo originating from public key "
        << toBase58(TokenType::NodePublic, publicKey) << " shard indexes "
        << m->shardindexes();

    if (m->has_lastlink())
        overlay_.lastLink(id_);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMEndpoints> const& m)
{
    if (sanity_.load() != Sanity::sane)
    {
        // Don't allow endpoints from peer not known sane
        return;
    }

    std::vector<PeerFinder::Endpoint> endpoints;

    if (m->endpoints_v2().size())
    {
        endpoints.reserve(m->endpoints_v2().size());
        for (auto const& tm : m->endpoints_v2())
        {
            // these endpoint strings support ipv4 and ipv6
            auto result =
                beast::IP::Endpoint::from_string_checked(tm.endpoint());
            if (!result)
            {
                JLOG(p_journal_.error())
                    << "failed to parse incoming endpoint: {" << tm.endpoint()
                    << "}";
                continue;
            }

            // If hops == 0, this Endpoint describes the peer we are connected
            // to -- in that case, we take the remote address seen on the
            // socket and store that in the IP::Endpoint. If this is the first
            // time, then we'll verify that their listener can receive incoming
            // by performing a connectivity test.  if hops > 0, then we just
            // take the address/port we were given

            endpoints.emplace_back(
                tm.hops() > 0 ? *result
                              : remote_address_.at_port(result->port()),
                tm.hops());
            JLOG(p_journal_.trace())
                << "got v2 EP: " << endpoints.back().address
                << ", hops = " << endpoints.back().hops;
        }
    }
    else
    {
        // this branch can be removed once the entire network is operating with
        // endpoint_v2() items (strings)
        endpoints.reserve(m->endpoints().size());
        for (int i = 0; i < m->endpoints().size(); ++i)
        {
            PeerFinder::Endpoint endpoint;
            protocol::TMEndpoint const& tm(m->endpoints(i));

            // hops
            endpoint.hops = tm.hops();

            // ipv4
            if (endpoint.hops > 0)
            {
                in_addr addr;
                addr.s_addr = tm.ipv4().ipv4();
                beast::IP::AddressV4 v4(ntohl(addr.s_addr));
                endpoint.address =
                    beast::IP::Endpoint(v4, tm.ipv4().ipv4port());
            }
            else
            {
                // This Endpoint describes the peer we are connected to.
                // We will take the remote address seen on the socket and
                // store that in the IP::Endpoint. If this is the first time,
                // then we'll verify that their listener can receive incoming
                // by performing a connectivity test.
                //
                endpoint.address =
                    remote_address_.at_port(tm.ipv4().ipv4port());
            }
            endpoints.push_back(endpoint);
            JLOG(p_journal_.trace())
                << "got v1 EP: " << endpoints.back().address
                << ", hops = " << endpoints.back().hops;
        }
    }

    if (!endpoints.empty())
        overlay_.peerFinder().on_endpoints(slot_, endpoints);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransaction> const& m)
{
    if (sanity_.load() == Sanity::insane)
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

        int flags;
        constexpr std::chrono::seconds tx_interval = 10s;

        if (!app_.getHashRouter().shouldProcess(txID, id_, flags, tx_interval))
        {
            // we have seen this transaction recently
            if (flags & SF_BAD)
            {
                fee_ = Resource::feeInvalidSignature;
                JLOG(p_journal_.debug()) << "Ignoring known bad tx " << txID;
            }

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
                flags |= SF_TRUSTED;
            }

            if (app_.getValidationPublicKey().empty())
            {
                // For now, be paranoid and have each validator
                // check each transaction, regardless of source
                checkSignature = false;
            }
        }

        // The maximum number of transactions to have in the job queue.
        constexpr int max_transactions = 250;
        if (app_.getJobQueue().getJobCount(jtTRANSACTION) > max_transactions)
        {
            overlay_.incJqTransOverflow();
            JLOG(p_journal_.info()) << "Transaction queue is full";
        }
        else if (app_.getLedgerMaster().getValidatedLedgerAge() > 4min)
        {
            JLOG(p_journal_.trace())
                << "No new transactions until synchronized";
        }
        else
        {
            app_.getJobQueue().addJob(
                jtTRANSACTION,
                "recvTransaction->checkTransaction",
                [weak = std::weak_ptr<PeerImp>(shared_from_this()),
                 flags,
                 checkSignature,
                 stx](Job&) {
                    if (auto peer = weak.lock())
                        peer->checkTransaction(flags, checkSignature, stx);
                });
        }
    }
    catch (std::exception const&)
    {
        JLOG(p_journal_.warn())
            << "Transaction invalid: " << strHex(m->rawtransaction());
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(jtLEDGER_REQ, "recvGetLedger", [weak, m](Job&) {
        if (auto peer = weak.lock())
            peer->getLedger(m);
    });
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMLedgerData> const& m)
{
    protocol::TMLedgerData& packet = *m;

    if (m->nodes().size() <= 0)
    {
        JLOG(p_journal_.warn()) << "Ledger/TXset data with no nodes";
        return;
    }

    if (m->has_requestcookie())
    {
        std::shared_ptr<Peer> target =
            overlay_.findPeerByShortID(m->requestcookie());
        if (target)
        {
            m->clear_requestcookie();
            target->send(
                std::make_shared<Message>(packet, protocol::mtLEDGER_DATA));
        }
        else
        {
            JLOG(p_journal_.info()) << "Unable to route TX/ledger data reply";
            fee_ = Resource::feeUnwantedData;
        }
        return;
    }

    if (!stringIsUint256Sized(m->ledgerhash()))
    {
        JLOG(p_journal_.warn()) << "TX candidate reply with invalid hash size";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const hash{m->ledgerhash()};

    if (m->type() == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set
        std::weak_ptr<PeerImp> weak = shared_from_this();
        app_.getJobQueue().addJob(
            jtTXN_DATA, "recvPeerData", [weak, hash, m](Job&) {
                if (auto peer = weak.lock())
                    peer->app_.getInboundTransactions().gotData(hash, peer, m);
            });
        return;
    }

    if (!app_.getInboundLedgers().gotLedgerData(hash, shared_from_this(), m))
    {
        JLOG(p_journal_.trace()) << "Got data for unwanted ledger";
        fee_ = Resource::feeUnwantedData;
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMProposeSet> const& m)
{
    protocol::TMProposeSet& set = *m;

    auto const sig = makeSlice(set.signature());

    // Preliminary check for the validity of the signature: A DER encoded
    // signature can't be longer than 72 bytes.
    if ((boost::algorithm::clamp(sig.size(), 64, 72) != sig.size()) ||
        (publicKeyType(makeSlice(set.nodepubkey())) != KeyType::secp256k1))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_ = Resource::feeInvalidSignature;
        return;
    }

    if (!stringIsUint256Sized(set.currenttxhash()) ||
        !stringIsUint256Sized(set.previousledger()))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const proposeHash{set.currenttxhash()};
    uint256 const prevLedger{set.previousledger()};

    PublicKey const publicKey{makeSlice(set.nodepubkey())};
    NetClock::time_point const closeTime{NetClock::duration{set.closetime()}};

    uint256 const suppression = proposalUniqueId(
        proposeHash,
        prevLedger,
        set.proposeseq(),
        closeTime,
        publicKey.slice(),
        sig);

    if (!app_.getHashRouter().addSuppressionPeer(suppression, id_))
    {
        JLOG(p_journal_.trace()) << "Proposal: duplicate";
        return;
    }

    auto const isTrusted = app_.validators().trusted(publicKey);

    if (!isTrusted)
    {
        if (sanity_.load() == Sanity::insane)
        {
            JLOG(p_journal_.debug()) << "Proposal: Dropping UNTRUSTED (insane)";
            return;
        }

        if (!cluster() && app_.getFeeTrack().isLoadedLocal())
        {
            JLOG(p_journal_.debug()) << "Proposal: Dropping UNTRUSTED (load)";
            return;
        }
    }

    JLOG(p_journal_.trace())
        << "Proposal: " << (isTrusted ? "trusted" : "UNTRUSTED");

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
        [weak, m, proposal](Job& job) {
            if (auto peer = weak.lock())
                peer->checkPropose(job, m, proposal);
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
        checkSanity(
            m->ledgerseq(), app_.getLedgerMaster().getValidLedgerIndex());
    }

    app_.getOPs().pubPeerStatus([=]() -> Json::Value {
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
PeerImp::checkSanity(std::uint32_t validationSeq)
{
    std::uint32_t serverSeq;
    {
        // Extract the seqeuence number of the highest
        // ledger this peer has
        std::lock_guard sl(recentLock_);

        serverSeq = maxLedger_;
    }
    if (serverSeq != 0)
    {
        // Compare the peer's ledger sequence to the
        // sequence of a recently-validated ledger
        checkSanity(serverSeq, validationSeq);
    }
}

void
PeerImp::checkSanity(std::uint32_t seq1, std::uint32_t seq2)
{
    int diff = std::max(seq1, seq2) - std::min(seq1, seq2);

    if (diff < Tuning::saneLedgerLimit)
    {
        // The peer's ledger sequence is close to the validation's
        sanity_ = Sanity::sane;
    }

    if ((diff > Tuning::insaneLedgerLimit) &&
        (sanity_.load() != Sanity::insane))
    {
        // The peer's ledger sequence is way off the validation's
        std::lock_guard sl(recentLock_);

        sanity_ = Sanity::insane;
        insaneTime_ = clock_type::now();
    }
}

// Should this connection be rejected
// and considered a failure
void
PeerImp::check()
{
    if (m_inbound || (sanity_.load() == Sanity::sane))
        return;

    clock_type::time_point insaneTime;
    {
        std::lock_guard sl(recentLock_);

        insaneTime = insaneTime_;
    }

    bool reject = false;

    if (sanity_.load() == Sanity::insane)
        reject = (insaneTime - clock_type::now()) >
            std::chrono::seconds(Tuning::maxInsaneTime);

    if (sanity_.load() == Sanity::unknown)
        reject = (insaneTime - clock_type::now()) >
            std::chrono::seconds(Tuning::maxUnknownTime);

    if (reject)
    {
        overlay_.peerFinder().on_failure(slot_);
        post(
            strand_,
            std::bind(
                (void (PeerImp::*)(std::string const&)) & PeerImp::fail,
                shared_from_this(),
                "Not useful"));
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m)
{
    if (!stringIsUint256Sized(m->hash()))
    {
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const hash{m->hash()};

    if (m->status() == protocol::tsHAVE)
    {
        std::lock_guard sl(recentLock_);

        if (std::find(recentTxSets_.begin(), recentTxSets_.end(), hash) !=
            recentTxSets_.end())
        {
            fee_ = Resource::feeUnwantedData;
            return;
        }

        recentTxSets_.push_back(hash);
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
            fee_ = Resource::feeUnwantedData;
            return;
        }
        auto const& manifest = m->manifest();
        auto const& blob = m->blob();
        auto const& signature = m->signature();
        auto const version = m->version();
        auto const hash = sha512Half(manifest, blob, signature, version);

        JLOG(p_journal_.debug())
            << "Received validator list from " << remote_address_.to_string()
            << " (" << id_ << ")";

        if (!app_.getHashRouter().addSuppressionPeer(hash, id_))
        {
            JLOG(p_journal_.debug())
                << "ValidatorList: received duplicate validator list";
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_ = Resource::feeUnwantedData;
            return;
        }

        auto const applyResult = app_.validators().applyListAndBroadcast(
            manifest,
            blob,
            signature,
            version,
            remote_address_.to_string(),
            hash,
            app_.overlay(),
            app_.getHashRouter());
        auto const disp = applyResult.disposition;

        JLOG(p_journal_.debug())
            << "Processed validator list from "
            << (applyResult.publisherKey ? strHex(*applyResult.publisherKey)
                                         : "unknown or invalid publisher")
            << " from " << remote_address_.to_string() << " (" << id_
            << ") with result " << to_string(disp);

        switch (disp)
        {
            case ListDisposition::accepted:
                JLOG(p_journal_.debug())
                    << "Applied new validator list from peer "
                    << remote_address_;
                {
                    std::lock_guard<std::mutex> sl(recentLock_);

                    assert(applyResult.sequence && applyResult.publisherKey);
                    auto const& pubKey = *applyResult.publisherKey;
#ifndef NDEBUG
                    if (auto const iter = publisherListSequences_.find(pubKey);
                        iter != publisherListSequences_.end())
                    {
                        assert(iter->second < *applyResult.sequence);
                    }
#endif
                    publisherListSequences_[pubKey] = *applyResult.sequence;
                }
                break;
            case ListDisposition::same_sequence:
                JLOG(p_journal_.warn())
                    << "Validator list with current sequence from peer "
                    << remote_address_;
                // Charging this fee here won't hurt the peer in the normal
                // course of operation (ie. refresh every 5 minutes), but
                // will add up if the peer is misbehaving.
                fee_ = Resource::feeUnwantedData;
#ifndef NDEBUG
                {
                    std::lock_guard<std::mutex> sl(recentLock_);
                    assert(applyResult.sequence && applyResult.publisherKey);
                    assert(
                        publisherListSequences_[*applyResult.publisherKey] ==
                        *applyResult.sequence);
                }
#endif  // !NDEBUG

                break;
            case ListDisposition::stale:
                JLOG(p_journal_.warn())
                    << "Stale validator list from peer " << remote_address_;
                // There are very few good reasons for a peer to send an
                // old list, particularly more than once.
                fee_ = Resource::feeBadData;
                break;
            case ListDisposition::untrusted:
                JLOG(p_journal_.warn())
                    << "Untrusted validator list from peer " << remote_address_;
                // Charging this fee here won't hurt the peer in the normal
                // course of operation (ie. refresh every 5 minutes), but
                // will add up if the peer is misbehaving.
                fee_ = Resource::feeUnwantedData;
                break;
            case ListDisposition::invalid:
                JLOG(p_journal_.warn())
                    << "Invalid validator list from peer " << remote_address_;
                // This shouldn't ever happen with a well-behaved peer
                fee_ = Resource::feeInvalidSignature;
                break;
            case ListDisposition::unsupported_version:
                JLOG(p_journal_.warn())
                    << "Unsupported version validator list from peer "
                    << remote_address_;
                // During a version transition, this may be legitimate.
                // If it happens frequently, that's probably bad.
                fee_ = Resource::feeBadData;
                break;
            default:
                assert(false);
        }
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn()) << "ValidatorList: Exception, " << e.what()
                                << " from peer " << remote_address_;
        fee_ = Resource::feeBadData;
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidation> const& m)
{
    auto const closeTime = app_.timeKeeper().closeTime();

    if (m->validation().size() < 50)
    {
        JLOG(p_journal_.warn()) << "Validation: Too small";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    try
    {
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
            fee_ = Resource::feeUnwantedData;
            return;
        }

        if (!app_.getHashRouter().addSuppressionPeer(
                sha512Half(makeSlice(m->validation())), id_))
        {
            JLOG(p_journal_.trace()) << "Validation: duplicate";
            return;
        }

        auto const isTrusted =
            app_.validators().trusted(val->getSignerPublic());

        if (!isTrusted && (sanity_.load() == Sanity::insane))
        {
            JLOG(p_journal_.debug())
                << "Validation: dropping untrusted from insane peer";
        }
        if (isTrusted || cluster() || !app_.getFeeTrack().isLoadedLocal())
        {
            std::weak_ptr<PeerImp> weak = shared_from_this();
            app_.getJobQueue().addJob(
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                "recvValidation->checkValidation",
                [weak, val, m](Job&) {
                    if (auto peer = weak.lock())
                        peer->checkValidation(val, m);
                });
        }
        else
        {
            JLOG(p_journal_.debug()) << "Validation: Dropping UNTRUSTED (load)";
        }
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn()) << "Validation: Exception, " << e.what();
        fee_ = Resource::feeInvalidRequest;
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash& packet = *m;

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

        fee_ = Resource::feeMediumBurdenPeer;

        protocol::TMGetObjectByHash reply;

        reply.set_query(false);

        if (packet.has_seq())
            reply.set_seq(packet.seq());

        reply.set_type(packet.type());

        if (packet.has_ledgerhash())
            reply.set_ledgerhash(packet.ledgerhash());

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
                auto hObj{app_.getNodeStore().fetch(hash, seq)};
                if (!hObj)
                {
                    if (auto shardStore = app_.getShardStore())
                    {
                        if (seq >= shardStore->earliestLedgerSeq())
                            hObj = shardStore->fetch(hash, seq);
                    }
                }
                if (hObj)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects();
                    newObj.set_hash(hash.begin(), hash.size());
                    newObj.set_data(
                        &hObj->getData().front(), hObj->getData().size());

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
            const protocol::TMIndexedObject& obj = packet.objects(i);

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

                    std::shared_ptr<Blob> data(std::make_shared<Blob>(
                        obj.data().begin(), obj.data().end()));

                    app_.getLedgerMaster().addFetchPack(hash, data);
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
PeerImp::doFetchPack(const std::shared_ptr<protocol::TMGetObjectByHash>& packet)
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
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    fee_ = Resource::feeHighBurdenPeer;

    uint256 const hash{packet->ledgerhash()};

    std::weak_ptr<PeerImp> weak = shared_from_this();
    auto elapsed = UptimeClock::now();
    auto const pap = &app_;
    app_.getJobQueue().addJob(
        jtPACK, "MakeFetchPack", [pap, weak, packet, hash, elapsed](Job&) {
            pap->getLedgerMaster().makeFetchPack(weak, packet, hash, elapsed);
        });
}

void
PeerImp::checkTransaction(
    int flags,
    bool checkSignature,
    std::shared_ptr<STTx const> const& stx)
{
    // VFALCO TODO Rewrite to not use exceptions
    try
    {
        // Expired?
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32(sfLastLedgerSequence) <
             app_.getLedgerMaster().getValidLedgerIndex()))
        {
            app_.getHashRouter().setFlags(stx->getTransactionID(), SF_BAD);
            charge(Resource::feeUnwantedData);
            return;
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

                // Probably not necessary to set SF_BAD, but doesn't hurt.
                app_.getHashRouter().setFlags(stx->getTransactionID(), SF_BAD);
                charge(Resource::feeInvalidSignature);
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
            app_.getHashRouter().setFlags(stx->getTransactionID(), SF_BAD);
            charge(Resource::feeInvalidSignature);
            return;
        }

        bool const trusted(flags & SF_TRUSTED);
        app_.getOPs().processTransaction(
            tx, trusted, false, NetworkOPs::FailHard::no);
    }
    catch (std::exception const&)
    {
        app_.getHashRouter().setFlags(stx->getTransactionID(), SF_BAD);
        charge(Resource::feeBadData);
    }
}

// Called from our JobQueue
void
PeerImp::checkPropose(
    Job& job,
    std::shared_ptr<protocol::TMProposeSet> const& packet,
    RCLCxPeerPos peerPos)
{
    bool isTrusted = (job.getType() == jtPROPOSAL_t);

    JLOG(p_journal_.trace())
        << "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    assert(packet);

    if (!cluster() && !peerPos.checkSign())
    {
        JLOG(p_journal_.warn()) << "Proposal fails sig check";
        charge(Resource::feeInvalidSignature);
        return;
    }

    if (isTrusted)
    {
        app_.getOPs().processTrustedProposal(peerPos, packet);
    }
    else
    {
        if (cluster() ||
            (app_.getOPs().getConsensusLCL() ==
             peerPos.proposal().prevLedger()))
        {
            // relay untrusted proposal
            JLOG(p_journal_.trace()) << "relaying UNTRUSTED proposal";
            overlay_.relay(*packet, peerPos.suppressionID());
        }
        else
        {
            JLOG(p_journal_.debug()) << "Not relaying UNTRUSTED proposal";
        }
    }
}

void
PeerImp::checkValidation(
    std::shared_ptr<STValidation> const& val,
    std::shared_ptr<protocol::TMValidation> const& packet)
{
    try
    {
        // VFALCO Which functions throw?
        if (!cluster() && !val->isValid())
        {
            JLOG(p_journal_.warn()) << "Validation is invalid";
            charge(Resource::feeInvalidRequest);
            return;
        }

        if (app_.getOPs().recvValidation(val, std::to_string(id())) ||
            cluster())
        {
            auto const suppression =
                sha512Half(makeSlice(val->getSerialized()));
            overlay_.relay(*packet, suppression);
        }
    }
    catch (std::exception const&)
    {
        JLOG(p_journal_.trace()) << "Exception processing validation";
        charge(Resource::feeInvalidRequest);
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

// VFALCO NOTE This function is way too big and cumbersome.
void
PeerImp::getLedger(std::shared_ptr<protocol::TMGetLedger> const& m)
{
    protocol::TMGetLedger& packet = *m;
    std::shared_ptr<SHAMap> shared;
    SHAMap const* map = nullptr;
    protocol::TMLedgerData reply;
    bool fatLeaves = true;
    std::shared_ptr<Ledger const> ledger;

    if (packet.has_requestcookie())
        reply.set_requestcookie(packet.requestcookie());

    std::string logMe;

    if (packet.itype() == protocol::liTS_CANDIDATE)
    {
        // Request is for a transaction candidate set
        JLOG(p_journal_.trace()) << "GetLedger: Tx candidate set";

        if (!packet.has_ledgerhash() ||
            !stringIsUint256Sized(packet.ledgerhash()))
        {
            charge(Resource::feeInvalidRequest);
            JLOG(p_journal_.warn()) << "GetLedger: Tx candidate set invalid";
            return;
        }

        uint256 const txHash{packet.ledgerhash()};

        shared = app_.getInboundTransactions().getSet(txHash, false);
        map = shared.get();

        if (!map)
        {
            if (packet.has_querytype() && !packet.has_requestcookie())
            {
                JLOG(p_journal_.debug()) << "GetLedger: Routing Tx set request";

                auto const v = getPeerWithTree(overlay_, txHash, this);
                if (!v)
                {
                    JLOG(p_journal_.info()) << "GetLedger: Route TX set failed";
                    return;
                }

                packet.set_requestcookie(id());
                v->send(
                    std::make_shared<Message>(packet, protocol::mtGET_LEDGER));
                return;
            }

            JLOG(p_journal_.debug()) << "GetLedger: Can't provide map ";
            charge(Resource::feeInvalidRequest);
            return;
        }

        reply.set_ledgerseq(0);
        reply.set_ledgerhash(txHash.begin(), txHash.size());
        reply.set_type(protocol::liTS_CANDIDATE);
        fatLeaves = false;  // We'll already have most transactions
    }
    else
    {
        if (send_queue_.size() >= Tuning::dropSendQueue)
        {
            JLOG(p_journal_.debug()) << "GetLedger: Large send queue";
            return;
        }

        if (app_.getFeeTrack().isLoadedLocal() && !cluster())
        {
            JLOG(p_journal_.debug()) << "GetLedger: Too busy";
            return;
        }

        // Figure out what ledger they want
        JLOG(p_journal_.trace()) << "GetLedger: Received";

        if (packet.has_ledgerhash())
        {
            if (!stringIsUint256Sized(packet.ledgerhash()))
            {
                charge(Resource::feeInvalidRequest);
                JLOG(p_journal_.warn()) << "GetLedger: Invalid request";
                return;
            }

            uint256 const ledgerhash{packet.ledgerhash()};
            logMe += "LedgerHash:";
            logMe += to_string(ledgerhash);
            ledger = app_.getLedgerMaster().getLedgerByHash(ledgerhash);

            if (!ledger && packet.has_ledgerseq())
            {
                if (auto shardStore = app_.getShardStore())
                {
                    auto seq = packet.ledgerseq();
                    if (seq >= shardStore->earliestLedgerSeq())
                        ledger = shardStore->fetchLedger(ledgerhash, seq);
                }
            }

            if (!ledger)
            {
                JLOG(p_journal_.trace())
                    << "GetLedger: Don't have " << ledgerhash;
            }

            if (!ledger &&
                (packet.has_querytype() && !packet.has_requestcookie()))
            {
                // We don't have the requested ledger
                // Search for a peer who might
                auto const v = getPeerWithLedger(
                    overlay_,
                    ledgerhash,
                    packet.has_ledgerseq() ? packet.ledgerseq() : 0,
                    this);
                if (!v)
                {
                    JLOG(p_journal_.trace()) << "GetLedger: Cannot route";
                    return;
                }

                packet.set_requestcookie(id());
                v->send(
                    std::make_shared<Message>(packet, protocol::mtGET_LEDGER));
                JLOG(p_journal_.debug()) << "GetLedger: Request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq())
        {
            if (packet.ledgerseq() < app_.getLedgerMaster().getEarliestFetch())
            {
                JLOG(p_journal_.debug()) << "GetLedger: Early ledger request";
                return;
            }
            ledger = app_.getLedgerMaster().getLedgerBySeq(packet.ledgerseq());
            if (!ledger)
            {
                JLOG(p_journal_.debug())
                    << "GetLedger: Don't have " << packet.ledgerseq();
            }
        }
        else if (packet.has_ltype() && (packet.ltype() == protocol::ltCLOSED))
        {
            ledger = app_.getLedgerMaster().getClosedLedger();
            assert(!ledger->open());
            // VFALCO ledger should never be null!
            // VFALCO How can the closed ledger be open?
#if 0
            if (ledger && ledger->info().open)
                ledger = app_.getLedgerMaster ().getLedgerBySeq (
                    ledger->info().seq - 1);
#endif
        }
        else
        {
            charge(Resource::feeInvalidRequest);
            JLOG(p_journal_.warn()) << "GetLedger: Unknown request";
            return;
        }

        if ((!ledger) ||
            (packet.has_ledgerseq() &&
             (packet.ledgerseq() != ledger->info().seq)))
        {
            charge(Resource::feeInvalidRequest);

            if (ledger)
            {
                JLOG(p_journal_.warn()) << "GetLedger: Invalid sequence";
            }
            return;
        }

        if (!packet.has_ledgerseq() &&
            (ledger->info().seq < app_.getLedgerMaster().getEarliestFetch()))
        {
            JLOG(p_journal_.debug()) << "GetLedger: Early ledger request";
            return;
        }

        // Fill out the reply
        auto const lHash = ledger->info().hash;
        reply.set_ledgerhash(lHash.begin(), lHash.size());
        reply.set_ledgerseq(ledger->info().seq);
        reply.set_type(packet.itype());

        if (packet.itype() == protocol::liBASE)
        {
            // they want the ledger base data
            JLOG(p_journal_.trace()) << "GetLedger: Base data";
            Serializer nData(128);
            addRaw(ledger->info(), nData);
            reply.add_nodes()->set_nodedata(
                nData.getDataPtr(), nData.getLength());

            auto const& stateMap = ledger->stateMap();
            if (stateMap.getHash() != beast::zero)
            {
                // return account state root node if possible
                Serializer rootNode(768);
                if (stateMap.getRootNode(rootNode, snfWIRE))
                {
                    reply.add_nodes()->set_nodedata(
                        rootNode.getDataPtr(), rootNode.getLength());

                    if (ledger->info().txHash != beast::zero)
                    {
                        auto const& txMap = ledger->txMap();

                        if (txMap.getHash() != beast::zero)
                        {
                            rootNode.erase();

                            if (txMap.getRootNode(rootNode, snfWIRE))
                                reply.add_nodes()->set_nodedata(
                                    rootNode.getDataPtr(),
                                    rootNode.getLength());
                        }
                    }
                }
            }

            auto oPacket =
                std::make_shared<Message>(reply, protocol::mtLEDGER_DATA);
            send(oPacket);
            return;
        }

        if (packet.itype() == protocol::liTX_NODE)
        {
            map = &ledger->txMap();
            logMe += " TX:";
            logMe += to_string(map->getHash());
        }
        else if (packet.itype() == protocol::liAS_NODE)
        {
            map = &ledger->stateMap();
            logMe += " AS:";
            logMe += to_string(map->getHash());
        }
    }

    if (!map || (packet.nodeids_size() == 0))
    {
        JLOG(p_journal_.warn()) << "GetLedger: Can't find map or empty request";
        charge(Resource::feeInvalidRequest);
        return;
    }

    JLOG(p_journal_.trace()) << "GetLedger: " << logMe;

    auto const depth = packet.has_querydepth()
        ? (std::min(packet.querydepth(), 3u))
        : (isHighLatency() ? 2 : 1);

    for (int i = 0;
         (i < packet.nodeids().size() &&
          (reply.nodes().size() < Tuning::maxReplyNodes));
         ++i)
    {
        SHAMapNodeID mn(packet.nodeids(i).data(), packet.nodeids(i).size());

        if (!mn.isValid())
        {
            JLOG(p_journal_.warn()) << "GetLedger: Invalid node " << logMe;
            charge(Resource::feeInvalidRequest);
            return;
        }

        std::vector<SHAMapNodeID> nodeIDs;
        std::vector<Blob> rawNodes;

        try
        {
            if (map->getNodeFat(mn, nodeIDs, rawNodes, fatLeaves, depth))
            {
                assert(nodeIDs.size() == rawNodes.size());
                JLOG(p_journal_.trace()) << "GetLedger: getNodeFat got "
                                         << rawNodes.size() << " nodes";
                std::vector<SHAMapNodeID>::iterator nodeIDIterator;
                std::vector<Blob>::iterator rawNodeIterator;

                for (nodeIDIterator = nodeIDs.begin(),
                    rawNodeIterator = rawNodes.begin();
                     nodeIDIterator != nodeIDs.end();
                     ++nodeIDIterator, ++rawNodeIterator)
                {
                    Serializer nID(33);
                    nodeIDIterator->addIDRaw(nID);
                    protocol::TMLedgerNode* node = reply.add_nodes();
                    node->set_nodeid(nID.getDataPtr(), nID.getLength());
                    node->set_nodedata(
                        &rawNodeIterator->front(), rawNodeIterator->size());
                }
            }
            else
            {
                JLOG(p_journal_.warn())
                    << "GetLedger: getNodeFat returns false";
            }
        }
        catch (std::exception&)
        {
            std::string info;

            if (packet.itype() == protocol::liTS_CANDIDATE)
                info = "TS candidate";
            else if (packet.itype() == protocol::liBASE)
                info = "Ledger base";
            else if (packet.itype() == protocol::liTX_NODE)
                info = "TX node";
            else if (packet.itype() == protocol::liAS_NODE)
                info = "AS node";

            if (!packet.has_ledgerhash())
                info += ", no hash specified";

            JLOG(p_journal_.warn())
                << "getNodeFat( " << mn << ") throws exception: " << info;
        }
    }

    JLOG(p_journal_.info())
        << "Got request for " << packet.nodeids().size() << " nodes at depth "
        << depth << ", return " << reply.nodes().size() << " nodes";

    auto oPacket = std::make_shared<Message>(reply, protocol::mtLEDGER_DATA);
    send(oPacket);
}

int
PeerImp::getScore(bool haveItem) const
{
    // Random component of score, used to break ties and avoid
    // overloading the "best" peer
    static const int spRandomMax = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static const int spHaveItem = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static const int spLatency = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static const int spNoLatency = 8000;

    int score = rand_int(spRandomMax);

    if (haveItem)
        score += spHaveItem;

    boost::optional<std::chrono::milliseconds> latency;
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
    return latency_ >= Tuning::peerHighLatency;
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
