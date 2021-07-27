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
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/digest.h>

#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string/predicate.hpp>

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
    : P2PeerImp(
          overlay.p2pConfig(),
          id,
          slot,
          std::move(request),
          publicKey,
          protocol,
          std::move(stream_ptr),
          overlay)
    , app_(app)
    , overlay_(overlay)
    , p_sink_(app_.journal("Protocol"), makePrefix(id))
    , p_journal_(p_sink_)
    , timer_(waitable_timer{getSocketExecutor()})
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.journal("Squelch"))
    , usage_(consumer)
    , fee_(Resource::feeLightPeer)
    , vpReduceRelayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_VPRR,
          app_.config().VP_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_LEDGER_REPLAY,
          app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.debug()) << " compression enabled "
                           << (compressionEnabled_ == Compressed::On)
                           << " vp reduce-relay enabled "
                           << vpReduceRelayEnabled_ << " on " << remote_address_
                           << " " << id_;
}

PeerImp::PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& stream_ptr,
    const_buffers_type const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl& overlay)
    : P2PeerImp(
          overlay.p2pConfig(),
          std::move(stream_ptr),
          buffers,
          std::move(slot),
          std::move(response),
          publicKey,
          protocol,
          id,
          overlay)
    , app_(app)
    , overlay_(overlay)
    , p_sink_(app_.journal("Protocol"), makePrefix(id))
    , p_journal_(p_sink_)
    , timer_(waitable_timer{getSocketExecutor()})
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.journal("Squelch"))
    , usage_(usage)
    , fee_(Resource::feeLightPeer)
    , vpReduceRelayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_VPRR,
          app_.config().VP_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_LEDGER_REPLAY,
          app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.debug()) << "compression enabled "
                           << (compressionEnabled_ == Compressed::On)
                           << " vp reduce-relay enabled "
                           << vpReduceRelayEnabled_ << " on " << remote_address_
                           << " " << id_;
}

PeerImp::~PeerImp()
{
    const bool inCluster{cluster()};

    overlay_.deletePeer(id_);

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
PeerImp::onEvtRun()
{
    auto parseLedgerHash =
        [](std::string const& value) -> std::optional<uint256> {
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
        ret[jss::server_domain] = domain();

    if (auto const nid = headers_["Network-ID"].to_string(); !nid.empty())
        ret[jss::network_id] = nid;

    ret[jss::load] = usage_.balance();

    if (auto const version = getVersion(); !version.empty())
        ret[jss::version] = version;

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

    if (seq >= app_.getNodeStore().earliestLedgerSeq())
    {
        std::lock_guard lock{shardInfoMutex_};
        auto const it{shardInfos_.find(publicKey_)};
        if (it != shardInfos_.end())
        {
            auto const shardIndex{app_.getNodeStore().seqToShardIndex(seq)};
            return boost::icl::contains(it->second.finalized(), shardIndex);
        }
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
PeerImp::onEvtClose()
{
    error_code ec;
    timer_.cancel(ec);
    overlay_.incPeerDisconnect();
}

hash_map<PublicKey, NodeStore::ShardInfo> const
PeerImp::getPeerShardInfos() const
{
    std::lock_guard l{shardInfoMutex_};
    return shardInfos_;
}

void
PeerImp::onEvtGracefulClose()
{
    setTimer();
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
        std::bind(&PeerImp::onTimer, shared(), std::placeholders::_1)));
}

// convenience for ignoring the error code
void
PeerImp::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

//------------------------------------------------------------------------------

void
PeerImp::onTimer(error_code const& ec)
{
    if (!isSocketOpen())
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        return close();
    }

    if (incLargeSendQueue() >= Tuning::sendqIntervals)
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
PeerImp::onEvtShutdown()
{
    cancelTimer();
}

//------------------------------------------------------------------------------

// Protocol logic

void
PeerImp::onEvtDoProtocolStart()
{
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

    // Request shard info from peer
    protocol::TMGetPeerShardInfoV2 tmGPS;
    tmGPS.set_relays(0);
    send(std::make_shared<Message>(tmGPS, protocol::mtGET_PEER_SHARD_INFO_V2));

    setTimer();
}

bool
PeerImp::onEvtSendFilter(std::shared_ptr<Message> const& m)
{
    auto validator = m->getValidatorKey();
    auto res = validator && !squelch_.expireSquelch(*validator);
    if (!res)
        overlay_.reportTraffic(
            safe_cast<TrafficCount::category>(m->getCategory()),
            false,
            static_cast<int>(m->getBuffer(compressionEnabled_).size()));
    return res;
}

bool
PeerImp::onEvtProtocolMessage(
    detail::MessageHeader const& header,
    const_buffers_type const& buffers)
{
    bool success = false;

    switch (header.message_type)
    {
        case protocol::mtMANIFESTS:
            success = detail::PM::invoke<protocol::TMManifests>(
                header, buffers, *this);
            break;
        case protocol::mtPING:
            success =
                detail::PM::invoke<protocol::TMPing>(header, buffers, *this);
            break;
        case protocol::mtCLUSTER:
            success =
                detail::PM::invoke<protocol::TMCluster>(header, buffers, *this);
            break;
        case protocol::mtTRANSACTION:
            success = detail::PM::invoke<protocol::TMTransaction>(
                header, buffers, *this);
            break;
        case protocol::mtGET_LEDGER:
            success = detail::PM::invoke<protocol::TMGetLedger>(
                header, buffers, *this);
            break;
        case protocol::mtLEDGER_DATA:
            success = detail::PM::invoke<protocol::TMLedgerData>(
                header, buffers, *this);
            break;
        case protocol::mtPROPOSE_LEDGER:
            success = detail::PM::invoke<protocol::TMProposeSet>(
                header, buffers, *this);
            break;
        case protocol::mtSTATUS_CHANGE:
            success = detail::PM::invoke<protocol::TMStatusChange>(
                header, buffers, *this);
            break;
        case protocol::mtHAVE_SET:
            success = detail::PM::invoke<protocol::TMHaveTransactionSet>(
                header, buffers, *this);
            break;
        case protocol::mtVALIDATION:
            success = detail::PM::invoke<protocol::TMValidation>(
                header, buffers, *this);
            break;
        case protocol::mtGET_PEER_SHARD_INFO:
            success = detail::PM::invoke<protocol::TMGetPeerShardInfo>(
                header, buffers, *this);
            break;
        case protocol::mtPEER_SHARD_INFO:
            success = detail::PM::invoke<protocol::TMPeerShardInfo>(
                header, buffers, *this);
            break;
        case protocol::mtVALIDATORLIST:
            success = detail::PM::invoke<protocol::TMValidatorList>(
                header, buffers, *this);
            break;
        case protocol::mtVALIDATORLISTCOLLECTION:
            success = detail::PM::invoke<protocol::TMValidatorListCollection>(
                header, buffers, *this);
            break;
        case protocol::mtGET_OBJECTS:
            success = detail::PM::invoke<protocol::TMGetObjectByHash>(
                header, buffers, *this);
            break;
        case protocol::mtSQUELCH:
            success =
                detail::PM::invoke<protocol::TMSquelch>(header, buffers, *this);
            break;
        case protocol::mtPROOF_PATH_REQ:
            success = detail::PM::invoke<protocol::TMProofPathRequest>(
                header, buffers, *this);
            break;
        case protocol::mtPROOF_PATH_RESPONSE:
            success = detail::PM::invoke<protocol::TMProofPathResponse>(
                header, buffers, *this);
            break;
        case protocol::mtREPLAY_DELTA_REQ:
            success = detail::PM::invoke<protocol::TMReplayDeltaRequest>(
                header, buffers, *this);
            break;
        case protocol::mtREPLAY_DELTA_RESPONSE:
            success = detail::PM::invoke<protocol::TMReplayDeltaResponse>(
                header, buffers, *this);
            break;
        case protocol::mtGET_PEER_SHARD_INFO_V2:
            success = detail::PM::invoke<protocol::TMGetPeerShardInfoV2>(
                header, buffers, *this);
            break;
        case protocol::mtPEER_SHARD_INFO_V2:
            success = detail::PM::invoke<protocol::TMPeerShardInfoV2>(
                header, buffers, *this);
            break;
        default:
            onMessageUnknown(header.message_type);
            success = true;
            break;
    }
    return success;
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
    load_event_ =
        app_.getJobQueue().makeLoadEvent(jtPEER, protocolMessageName(type));
    fee_ = Resource::feeLightPeer;
    overlay_.reportTraffic(
        TrafficCount::categorize(*m, type, true), true, static_cast<int>(size));
    JLOG(journal_.trace()) << "onMessageBegin: " << type << " " << size << " "
                           << uncompressed_size << " " << isCompressed;
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
    auto const s = m->list_size();

    if (s == 0)
    {
        fee_ = Resource::feeUnwantedData;
        return;
    }

    if (s > 100)
        fee_ = Resource::feeMediumBurdenPeer;

    // VFALCO What's the right job type?
    auto that = shared();
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
PeerImp::onMessage(std::shared_ptr<protocol::TMGetPeerShardInfo> const& m)
{
    // DEPRECATED
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPeerShardInfo> const& m)
{
    // DEPRECATED
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMGetPeerShardInfoV2> const& m)
{
    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    // Verify relays
    if (m->relays() > relayLimit)
        return badData("Invalid relays");

    // Verify peer chain
    // The peer chain should not contain this node's public key
    // nor the public key of the sending peer
    std::set<PublicKey> pubKeyChain;
    pubKeyChain.insert(app_.nodeIdentity().first);
    pubKeyChain.insert(publicKey_);

    auto const peerChainSz{m->peerchain_size()};
    if (peerChainSz > 0)
    {
        if (peerChainSz > relayLimit)
            return badData("Invalid peer chain size");

        if (peerChainSz + m->relays() > relayLimit)
            return badData("Invalid relays and peer chain size");

        for (int i = 0; i < peerChainSz; ++i)
        {
            auto const slice{makeSlice(m->peerchain(i).publickey())};

            // Verify peer public key
            if (!publicKeyType(slice))
                return badData("Invalid peer public key");

            // Verify peer public key is unique in the peer chain
            if (!pubKeyChain.emplace(slice).second)
                return badData("Invalid peer public key");
        }
    }

    // Reply with shard info this node may have
    if (auto shardStore = app_.getShardStore())
    {
        auto reply{shardStore->getShardInfo()->makeMessage(app_)};
        if (peerChainSz > 0)
            *(reply.mutable_peerchain()) = m->peerchain();
        send(std::make_shared<Message>(reply, protocol::mtPEER_SHARD_INFO_V2));
    }

    if (m->relays() == 0)
        return;

    // Charge originating peer a fee for requesting relays
    if (peerChainSz == 0)
        fee_ = Resource::feeMediumBurdenPeer;

    // Add peer to the peer chain
    m->add_peerchain()->set_publickey(publicKey_.data(), publicKey_.size());

    // Relay the request to peers, exclude the peer chain
    m->set_relays(m->relays() - 1);
    overlay_.foreach(send_if_not(
        std::make_shared<Message>(*m, protocol::mtGET_PEER_SHARD_INFO_V2),
        [&](std::shared_ptr<Peer> const& peer) {
            return pubKeyChain.find(peer->getNodePublic()) != pubKeyChain.end();
        }));
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMPeerShardInfoV2> const& m)
{
    // Find the earliest and latest shard indexes
    auto const& db{app_.getNodeStore()};
    auto const earliestShardIndex{db.earliestShardIndex()};
    auto const latestShardIndex{[&]() -> std::optional<std::uint32_t> {
        auto const curLedgerSeq{app_.getLedgerMaster().getCurrentLedgerIndex()};
        if (curLedgerSeq >= db.earliestLedgerSeq())
            return db.seqToShardIndex(curLedgerSeq);
        return std::nullopt;
    }()};

    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    // Used to create a digest and verify the message signature
    Serializer s;
    s.add32(HashPrefix::shardInfo);

    // Verify message creation time
    NodeStore::ShardInfo shardInfo;
    {
        auto const timestamp{
            NetClock::time_point{std::chrono::seconds{m->timestamp()}}};
        auto const now{app_.timeKeeper().now()};
        if (timestamp > (now + 5s))
            return badData("Invalid timestamp");

        // Check if stale
        using namespace std::chrono_literals;
        if (timestamp < (now - 5min))
            return badData("Stale timestamp");

        s.add32(m->timestamp());
        shardInfo.setMsgTimestamp(timestamp);
    }

    // Verify incomplete shards
    auto const numIncomplete{m->incomplete_size()};
    if (numIncomplete > 0)
    {
        if (latestShardIndex && numIncomplete > *latestShardIndex)
            return badData("Invalid number of incomplete shards");

        // Verify each incomplete shard
        for (int i = 0; i < numIncomplete; ++i)
        {
            auto const& incomplete{m->incomplete(i)};
            auto const shardIndex{incomplete.shardindex()};

            // Verify shard index
            if (shardIndex < earliestShardIndex ||
                (latestShardIndex && shardIndex > latestShardIndex))
            {
                return badData("Invalid incomplete shard index");
            }
            s.add32(shardIndex);

            // Verify state
            auto const state{static_cast<ShardState>(incomplete.state())};
            switch (state)
            {
                // Incomplete states
                case ShardState::acquire:
                case ShardState::complete:
                case ShardState::finalizing:
                case ShardState::queued:
                    break;

                // case ShardState::finalized:
                default:
                    return badData("Invalid incomplete shard state");
            };
            s.add32(incomplete.state());

            // Verify progress
            std::uint32_t progress{0};
            if (incomplete.has_progress())
            {
                progress = incomplete.progress();
                if (progress < 1 || progress > 100)
                    return badData("Invalid incomplete shard progress");
                s.add32(progress);
            }

            // Verify each incomplete shard is unique
            if (!shardInfo.update(shardIndex, state, progress))
                return badData("Invalid duplicate incomplete shards");
        }
    }

    // Verify finalized shards
    if (m->has_finalized())
    {
        auto const& str{m->finalized()};
        if (str.empty())
            return badData("Invalid finalized shards");

        if (!shardInfo.setFinalizedFromString(str))
            return badData("Invalid finalized shard indexes");

        auto const& finalized{shardInfo.finalized()};
        auto const numFinalized{boost::icl::length(finalized)};
        if (numFinalized == 0 ||
            boost::icl::first(finalized) < earliestShardIndex ||
            (latestShardIndex &&
             boost::icl::last(finalized) > latestShardIndex))
        {
            return badData("Invalid finalized shard indexes");
        }

        if (latestShardIndex &&
            (numFinalized + numIncomplete) > *latestShardIndex)
        {
            return badData("Invalid number of finalized and incomplete shards");
        }

        s.addRaw(str.data(), str.size());
    }

    // Verify public key
    auto slice{makeSlice(m->publickey())};
    if (!publicKeyType(slice))
        return badData("Invalid public key");

    // Verify peer public key isn't this nodes's public key
    PublicKey const publicKey(slice);
    if (publicKey == app_.nodeIdentity().first)
        return badData("Invalid public key");

    // Verify signature
    if (!verify(publicKey, s.slice(), makeSlice(m->signature()), false))
        return badData("Invalid signature");

    // Forward the message if a peer chain exists
    auto const peerChainSz{m->peerchain_size()};
    if (peerChainSz > 0)
    {
        // Verify peer chain
        if (peerChainSz > relayLimit)
            return badData("Invalid peer chain size");

        // The peer chain should not contain this node's public key
        // nor the public key of the sending peer
        std::set<PublicKey> pubKeyChain;
        pubKeyChain.insert(app_.nodeIdentity().first);
        pubKeyChain.insert(publicKey_);

        for (int i = 0; i < peerChainSz; ++i)
        {
            // Verify peer public key
            slice = makeSlice(m->peerchain(i).publickey());
            if (!publicKeyType(slice))
                return badData("Invalid peer public key");

            // Verify peer public key is unique in the peer chain
            if (!pubKeyChain.emplace(slice).second)
                return badData("Invalid peer public key");
        }

        // If last peer in the chain is connected, relay the message
        PublicKey const peerPubKey(
            makeSlice(m->peerchain(peerChainSz - 1).publickey()));
        if (auto peer = overlay_.findPeerByPublicKey(peerPubKey))
        {
            m->mutable_peerchain()->RemoveLast();
            peer->send(
                std::make_shared<Message>(*m, protocol::mtPEER_SHARD_INFO_V2));
            JLOG(p_journal_.trace())
                << "Relayed TMPeerShardInfoV2 from peer IP "
                << remote_address_.address().to_string() << " to peer IP "
                << peer->getRemoteAddress().to_string();
        }
        else
        {
            // Peer is no longer available so the relay ends
            JLOG(p_journal_.info()) << "Unable to relay peer shard info";
        }
    }

    JLOG(p_journal_.trace())
        << "Consumed TMPeerShardInfoV2 originating from public key "
        << toBase58(TokenType::NodePublic, publicKey) << " finalized shards["
        << ripple::to_string(shardInfo.finalized()) << "] incomplete shards["
        << (shardInfo.incomplete().empty() ? "empty"
                                           : shardInfo.incompleteToString())
        << "]";

    // Consume the message
    {
        std::lock_guard lock{shardInfoMutex_};
        auto const it{shardInfos_.find(publicKey_)};
        if (it == shardInfos_.end())
            shardInfos_.emplace(publicKey, std::move(shardInfo));
        else if (shardInfo.msgTimestamp() > it->second.msgTimestamp())
            it->second = std::move(shardInfo);
    }

    // Notify overlay a reply was received from the last peer in this chain
    if (peerChainSz == 0)
        overlay_.endOfPeerChain(id_);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMEndpoints> const& m)
{
    // Don't allow endpoints from peers that are not known tracking or are
    // not using a version of the message that we support:
    if (tracking_.load() != Tracking::converged)
        return;

    P2PeerImp<PeerImp>::onMessage(m);
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMTransaction> const& m)
{
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

        if (app_.getJobQueue().getJobCount(jtTRANSACTION) >
            app_.config().MAX_TRANSACTIONS)
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
                [weak = std::weak_ptr<PeerImp>(shared()),
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
    auto badData = [&](std::string const& msg) {
        charge(Resource::feeBadData);
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
        if (ledgerSeq < app_.getNodeStore().earliestLedgerSeq())
        {
            return badData(
                "Invalid ledger sequence " + std::to_string(ledgerSeq));
        }

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
    std::weak_ptr<PeerImp> weak = shared();
    app_.getJobQueue().addJob(jtLEDGER_REQ, "recvGetLedger", [weak, m](Job&) {
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
        charge(Resource::feeInvalidRequest);
        return;
    }

    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared();
    app_.getJobQueue().addJob(
        jtREPLAY_REQ, "recvProofPathRequest", [weak, m](Job&) {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processProofPathRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(Resource::feeInvalidRequest);
                    else
                        peer->charge(Resource::feeRequestNoReply);
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
        charge(Resource::feeInvalidRequest);
        return;
    }

    if (!ledgerReplayMsgHandler_.processProofPathResponse(m))
    {
        charge(Resource::feeBadData);
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m)
{
    JLOG(p_journal_.trace()) << "onMessage, TMReplayDeltaRequest";
    if (!ledgerReplayEnabled_)
    {
        charge(Resource::feeInvalidRequest);
        return;
    }

    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared();
    app_.getJobQueue().addJob(
        jtREPLAY_REQ, "recvReplayDeltaRequest", [weak, m](Job&) {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processReplayDeltaRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(Resource::feeInvalidRequest);
                    else
                        peer->charge(Resource::feeRequestNoReply);
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
        charge(Resource::feeInvalidRequest);
        return;
    }

    if (!ledgerReplayMsgHandler_.processReplayDeltaResponse(m))
    {
        charge(Resource::feeBadData);
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMLedgerData> const& m)
{
    auto badData = [&](std::string const& msg) {
        fee_ = Resource::feeBadData;
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
            if (ledgerSeq < app_.getNodeStore().earliestLedgerSeq())
            {
                return badData(
                    "Invalid ledger sequence " + std::to_string(ledgerSeq));
            }

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

    // Verify ledger nodes
    if (m->nodes_size() <= 0 || m->nodes_size() > Tuning::maxReplyNodes)
    {
        return badData(
            "Invalid Ledger/TXset nodes " + std::to_string(m->nodes_size()));
    }

    // Verify reply error
    if (m->has_error() &&
        (m->error() < protocol::reNO_LEDGER ||
         m->error() > protocol::reBAD_REQUEST))
    {
        return badData("Invalid reply error");
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
        std::weak_ptr<PeerImp> weak{shared()};
        app_.getJobQueue().addJob(
            jtTXN_DATA, "recvPeerData", [weak, ledgerHash, m](Job&) {
                if (auto peer = weak.lock())
                {
                    peer->app_.getInboundTransactions().gotData(
                        ledgerHash, peer, m);
                }
            });
        return;
    }

    // Consume the message
    app_.getInboundLedgers().gotLedgerData(ledgerHash, shared(), m);
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

    if (auto [added, relayed] =
            app_.getHashRouter().addSuppressionPeerWithStatus(suppression, id_);
        !added)
    {
        // Count unique messages (Slots has it's own 'HashRouter'), which a peer
        // receives within IDLED seconds since the message has been relayed.
        if (reduceRelayReady() && relayed &&
            (stopwatch().now() - *relayed) < reduce_relay::IDLED)
            overlay_.updateSlotAndSquelch(
                suppression, publicKey, id_, protocol::mtPROPOSE_LEDGER);
        JLOG(p_journal_.trace()) << "Proposal: duplicate";
        return;
    }

    auto const isTrusted = app_.validators().trusted(publicKey);

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

    std::weak_ptr<PeerImp> weak = shared();
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
        checkTracking(
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
        fee_ = Resource::feeHighBurdenPeer;
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
        fee_ = Resource::feeUnwantedData;
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

            assert(applyResult.publisherKey);
            auto const& pubKey = *applyResult.publisherKey;
#ifndef NDEBUG
            if (auto const iter = publisherListSequences_.find(pubKey);
                iter != publisherListSequences_.end())
            {
                assert(iter->second < applyResult.sequence);
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
            assert(applyResult.sequence && applyResult.publisherKey);
            assert(
                publisherListSequences_[*applyResult.publisherKey] <=
                applyResult.sequence);
        }
#endif  // !NDEBUG

        break;
        case ListDisposition::stale:
        case ListDisposition::untrusted:
        case ListDisposition::invalid:
        case ListDisposition::unsupported_version:
            break;
        default:
            assert(false);
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
            fee_ = Resource::feeUnwantedData;
            break;
        case ListDisposition::stale:
            // There are very few good reasons for a peer to send an
            // old list, particularly more than once.
            fee_ = Resource::feeBadData;
            break;
        case ListDisposition::untrusted:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_ = Resource::feeUnwantedData;
            break;
        case ListDisposition::invalid:
            // This shouldn't ever happen with a well-behaved peer
            fee_ = Resource::feeInvalidSignature;
            break;
        case ListDisposition::unsupported_version:
            // During a version transition, this may be legitimate.
            // If it happens frequently, that's probably bad.
            fee_ = Resource::feeBadData;
            break;
        default:
            assert(false);
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
                assert(false);
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
            fee_ = Resource::feeUnwantedData;
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
        fee_ = Resource::feeBadData;
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
            fee_ = Resource::feeUnwantedData;
            return;
        }
        else if (m->version() < 2)
        {
            JLOG(p_journal_.debug())
                << "ValidatorListCollection: received invalid validator list "
                   "version "
                << m->version() << " from peer using protocol version "
                << to_string(protocol_);
            fee_ = Resource::feeBadData;
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
        fee_ = Resource::feeBadData;
    }
}

void
PeerImp::onMessage(std::shared_ptr<protocol::TMValidation> const& m)
{
    if (m->validation().size() < 50)
    {
        JLOG(p_journal_.warn()) << "Validation: Too small";
        fee_ = Resource::feeInvalidRequest;
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
            fee_ = Resource::feeUnwantedData;
            return;
        }

        auto key = sha512Half(makeSlice(m->validation()));
        if (auto [added, relayed] =
                app_.getHashRouter().addSuppressionPeerWithStatus(key, id_);
            !added)
        {
            // Count unique messages (Slots has it's own 'HashRouter'), which a
            // peer receives within IDLED seconds since the message has been
            // relayed. Wait WAIT_ON_BOOTUP time to let the server establish
            // connections to peers.
            if (reduceRelayReady() && relayed &&
                (stopwatch().now() - *relayed) < reduce_relay::IDLED)
                overlay_.updateSlotAndSquelch(
                    key, val->getSignerPublic(), id_, protocol::mtVALIDATION);
            JLOG(p_journal_.trace()) << "Validation: duplicate";
            return;
        }

        auto const isTrusted =
            app_.validators().trusted(val->getSignerPublic());

        if (!isTrusted && (tracking_.load() == Tracking::diverged))
        {
            JLOG(p_journal_.debug())
                << "Validation: dropping untrusted from diverged peer";
        }
        if (isTrusted || cluster() || !app_.getFeeTrack().isLoadedLocal())
        {
            std::weak_ptr<PeerImp> weak = shared();
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
        JLOG(p_journal_.warn())
            << "Exception processing validation: " << e.what();
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
        if (getSendQueueSize() >= Tuning::dropSendQueue)
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
        {
            if (!stringIsUint256Sized(packet.ledgerhash()))
            {
                fee_ = Resource::feeInvalidRequest;
                return;
            }

            reply.set_ledgerhash(packet.ledgerhash());
        }

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
                if (!nodeObject)
                {
                    if (auto shardStore = app_.getShardStore())
                    {
                        if (seq >= shardStore->earliestLedgerSeq())
                            nodeObject = shardStore->fetchNodeObject(hash, seq);
                    }
                }
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
PeerImp::onMessage(std::shared_ptr<protocol::TMSquelch> const& m)
{
    using on_message_fn =
        void (PeerImp::*)(std::shared_ptr<protocol::TMSquelch> const&);
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind((on_message_fn)&PeerImp::onMessage, shared(), m));

    if (!m->has_validatorpubkey())
    {
        charge(Resource::feeBadData);
        return;
    }
    auto validator = m->validatorpubkey();
    auto const slice{makeSlice(validator)};
    if (!publicKeyType(slice))
    {
        charge(Resource::feeBadData);
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
        charge(Resource::feeBadData);

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

    std::weak_ptr<PeerImp> weak = shared();
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

    bool relay;

    if (isTrusted)
        relay = app_.getOPs().processTrustedProposal(peerPos);
    else
        relay = app_.config().RELAY_UNTRUSTED_PROPOSALS || cluster();

    if (relay)
    {
        // haveMessage contains peers, which are suppressed; i.e. the peers
        // are the source of the message, consequently the message should
        // not be relayed to these peers. But the message must be counted
        // as part of the squelch logic.
        auto haveMessage = app_.overlay().relay(
            *packet, peerPos.suppressionID(), peerPos.publicKey());
        if (reduceRelayReady() && !haveMessage.empty())
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
    std::shared_ptr<protocol::TMValidation> const& packet)
{
    if (!cluster() && !val->isValid())
    {
        JLOG(p_journal_.debug()) << "Validation forwarded by peer is invalid";
        charge(Resource::feeInvalidRequest);
        return;
    }

    // FIXME it should be safe to remove this try/catch. Investigate codepaths.
    try
    {
        if (app_.getOPs().recvValidation(val, std::to_string(id())) ||
            cluster())
        {
            auto const suppression =
                sha512Half(makeSlice(val->getSerialized()));
            // haveMessage contains peers, which are suppressed; i.e. the peers
            // are the source of the message, consequently the message should
            // not be relayed to these peers. But the message must be counted
            // as part of the squelch logic.
            auto haveMessage =
                overlay_.relay(*packet, suppression, val->getSignerPublic());
            if (reduceRelayReady() && !haveMessage.empty())
            {
                overlay_.updateSlotAndSquelch(
                    suppression,
                    val->getSignerPublic(),
                    std::move(haveMessage),
                    protocol::mtVALIDATION);
            }
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
            if (m->has_ledgerseq())
            {
                // Attempt to find ledger by sequence in the shard store
                if (auto shards = app_.getShardStore())
                {
                    if (m->ledgerseq() >= shards->earliestLedgerSeq())
                    {
                        ledger =
                            shards->fetchLedger(ledgerHash, m->ledgerseq());
                    }
                }
            }

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
                        peer->send(std::make_shared<Message>(
                            *m, protocol::mtGET_LEDGER));
                        JLOG(p_journal_.debug())
                            << "getLedger: Request relayed to peer";
                        return ledger;
                    }

                    JLOG(p_journal_.trace())
                        << "getLedger: Failed to find peer to relay request";
                }
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
                    charge(Resource::feeInvalidRequest);

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
        JLOG(p_journal_.warn()) << "getLedger: Unable to find ledger";
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
        charge(Resource::feeMediumBurdenPeer);

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
        if (getSendQueueSize() >= Tuning::dropSendQueue)
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
        std::vector<SHAMapNodeID> nodeIds;
        std::vector<Blob> rawNodes;

        for (int i = 0; i < m->nodeids_size() &&
             ledgerData.nodes_size() < Tuning::maxReplyNodes;
             ++i)
        {
            auto const shaMapNodeId{deserializeSHAMapNodeID(m->nodeids(i))};

            nodeIds.clear();
            rawNodes.clear();
            try
            {
                if (map->getNodeFat(
                        *shaMapNodeId,
                        nodeIds,
                        rawNodes,
                        fatLeaves,
                        queryDepth))
                {
                    assert(nodeIds.size() == rawNodes.size());
                    JLOG(p_journal_.trace())
                        << "processLedgerRequest: getNodeFat got "
                        << rawNodes.size() << " nodes";

                    auto rawNodeIter{rawNodes.begin()};
                    for (auto const& nodeId : nodeIds)
                    {
                        protocol::TMLedgerNode* node{ledgerData.add_nodes()};
                        node->set_nodeid(nodeId.getRawString());
                        node->set_nodedata(
                            &rawNodeIter->front(), rawNodeIter->size());
                        ++rawNodeIter;
                    }
                }
                else
                {
                    JLOG(p_journal_.warn())
                        << "processLedgerRequest: getNodeFat returns false";
                }
            }
            catch (std::exception& e)
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

                JLOG(p_journal_.error())
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

    auto message{
        std::make_shared<Message>(ledgerData, protocol::mtLEDGER_DATA)};
    send(message);
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

bool
PeerImp::reduceRelayReady()
{
    if (!reduceRelayReady_)
        reduceRelayReady_ =
            reduce_relay::epoch<std::chrono::minutes>(UptimeClock::now()) >
            reduce_relay::WAIT_ON_BOOTUP;
    return vpReduceRelayEnabled_ && reduceRelayReady_;
}

}  // namespace ripple
