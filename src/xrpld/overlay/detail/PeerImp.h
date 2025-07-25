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

#ifndef RIPPLE_OVERLAY_PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_PEERIMP_H_INCLUDED

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/ledger/detail/LedgerReplayMsgHandler.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/overlay/Squelch.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/peerfinder/PeerfinderManager.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/resource/Fees.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <cstdint>
#include <optional>
#include <queue>

namespace ripple {

struct ValidatorBlobInfo;
class SHAMap;

class PeerImp : public Peer,
                public std::enable_shared_from_this<PeerImp>,
                public OverlayImpl::Child
{
public:
    /** Whether the peer's view of the ledger converges or diverges from ours */
    enum class Tracking { diverged, unknown, converged };

private:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using Compressed = compression::Compressed;

    Application& app_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal const journal_;
    beast::Journal const p_journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;
    waitable_timer timer_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    OverlayImpl& overlay_;
    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    std::atomic<Tracking> tracking_;
    clock_type::time_point trackingTime_;
    bool detaching_ = false;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    std::shared_mutex mutable nameMutex_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;
    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;

    boost::circular_buffer<uint256> recentLedgers_{128};
    boost::circular_buffer<uint256> recentTxSets_{128};

    std::optional<std::chrono::milliseconds> latency_;
    std::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;

    reduce_relay::Squelch<UptimeClock> squelch_;

    // Notes on thread locking:
    //
    // During an audit it was noted that some member variables that looked
    // like they need thread protection were not receiving it.  And, indeed,
    // that was correct.  But the multi-phase initialization of PeerImp
    // makes such an audit difficult.  A further audit suggests that the
    // locking is now protecting variables that don't need it.  We're
    // leaving that locking in place (for now) as a form of future proofing.
    //
    // Here are the variables that appear to need locking currently:
    //
    // o closedLedgerHash_
    // o previousLedgerHash_
    // o minLedger_
    // o maxLedger_
    // o recentLedgers_
    // o recentTxSets_
    // o trackingTime_
    // o latency_
    //
    // The following variables are being protected preemptively:
    //
    // o name_
    // o last_status_
    //
    // June 2019

    struct ChargeWithContext
    {
        Resource::Charge fee = Resource::feeTrivialPeer;
        std::string context = {};

        void
        update(Resource::Charge f, std::string const& add)
        {
            XRPL_ASSERT(
                f >= fee,
                "ripple::PeerImp::ChargeWithContext::update : fee increases");
            fee = f;
            if (!context.empty())
            {
                context += " ";
            }
            context += add;
        }
    };

    std::mutex mutable recentLock_;
    protocol::TMStatusChange last_status_;
    Resource::Consumer usage_;
    ChargeWithContext fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;
    std::unique_ptr<LoadEvent> load_event_;
    // The highest sequence of each PublisherList that has
    // been sent to or received from this peer.
    hash_map<PublicKey, std::size_t> publisherListSequences_;

    Compressed compressionEnabled_ = Compressed::Off;

    // Queue of transactions' hashes that have not been
    // relayed. The hashes are sent once a second to a peer
    // and the peer requests missing transactions from the node.
    hash_set<uint256> txQueue_;
    // true if tx reduce-relay feature is enabled on the peer.
    bool txReduceRelayEnabled_ = false;

    bool ledgerReplayEnabled_ = false;
    LedgerReplayMsgHandler ledgerReplayMsgHandler_;

    friend class OverlayImpl;

    class Metrics
    {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics&
        operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics&
        operator=(Metrics&&) = delete;

        void
        add_message(std::uint64_t bytes);
        std::uint64_t
        average_bytes() const;
        std::uint64_t
        total_bytes() const;

    private:
        std::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{30, 0ull};
        clock_type::time_point intervalStart_{clock_type::now()};
        std::uint64_t totalBytes_{0};
        std::uint64_t accumBytes_{0};
        std::uint64_t rollingAvgBytes_{0};
    };

    struct
    {
        Metrics sent;
        Metrics recv;
    } metrics_;

public:
    PeerImp(PeerImp const&) = delete;
    PeerImp&
    operator=(PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    PeerImp(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr,
        OverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImp(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        OverlayImpl& overlay);

    virtual ~PeerImp();

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    virtual void
    run();

    // Called when Overlay gets a stop request.
    void
    stop() override;

    //
    // Network
    //

    void
    send(std::shared_ptr<Message> const& m) override;

    /** Send aggregated transactions' hashes */
    void
    sendTxQueue() override;

    /** Add transaction's hash to the transactions' hashes queue
       @param hash transaction's hash
     */
    void
    addTxQueue(uint256 const& hash) override;

    /** Remove transaction's hash from the transactions' hashes queue
       @param hash transaction's hash
     */
    void
    removeTxQueue(uint256 const& hash) override;

    /** Send a set of PeerFinder endpoints as a protocol message. */
    template <
        class FwdIt,
        class = typename std::enable_if_t<std::is_same<
            typename std::iterator_traits<FwdIt>::value_type,
            PeerFinder::Endpoint>::value>>
    void
    sendEndpoints(FwdIt first, FwdIt last);

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    void
    charge(Resource::Charge const& fee, std::string const& context) override;

    //
    // Identity
    //

    Peer::id_t
    id() const override
    {
        return id_;
    }

    /** Returns `true` if this connection will publicly share its IP address. */
    bool
    crawl() const;

    bool
    cluster() const override;

    /** Check if the peer is tracking
        @param validationSeq The ledger sequence of a recently-validated ledger
    */
    void
    checkTracking(std::uint32_t validationSeq);

    void
    checkTracking(std::uint32_t seq1, std::uint32_t seq2);

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    /** Return the version of rippled that the peer is running, if reported. */
    std::string
    getVersion() const;

    // Return the connection elapsed time.
    clock_type::duration
    uptime() const
    {
        return clock_type::now() - creationTime_;
    }

    Json::Value
    json() override;

    bool
    supportsFeature(ProtocolFeature f) const override;

    std::optional<std::size_t>
    publisherListSequence(PublicKey const& pubKey) const override
    {
        std::lock_guard<std::mutex> sl(recentLock_);

        auto iter = publisherListSequences_.find(pubKey);
        if (iter != publisherListSequences_.end())
            return iter->second;
        return {};
    }

    void
    setPublisherListSequence(PublicKey const& pubKey, std::size_t const seq)
        override
    {
        std::lock_guard<std::mutex> sl(recentLock_);

        publisherListSequences_[pubKey] = seq;
    }

    //
    // Ledger
    //

    uint256 const&
    getClosedLedgerHash() const override
    {
        return closedLedgerHash_;
    }

    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override;

    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override;

    bool
    hasTxSet(uint256 const& hash) const override;

    void
    cycleStatus() override;

    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override;

    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const& reason);

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

    bool
    txReduceRelayEnabled() const override
    {
        return txReduceRelayEnabled_;
    }

private:
    void
    close();

    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    void
    setTimer();

    void
    cancelTimer();

    static std::string
    makePrefix(id_t id);

    // Called when the timer wait completes
    void
    onTimer(boost::system::error_code const& ec);

    // Called when SSL shutdown completes
    void
    onShutdown(error_code ec);

    void
    doAccept();

    std::string
    name() const;

    std::string
    domain() const;

    //
    // protocol message loop
    //

    // Starts the protocol message loop
    void
    doProtocolStart();

    // Called when protocol message bytes are received
    void
    onReadMessage(error_code ec, std::size_t bytes_transferred);

    // Called when protocol messages bytes are sent
    void
    onWriteMessage(error_code ec, std::size_t bytes_transferred);

    /** Called from onMessage(TMTransaction(s)).
       @param m Transaction protocol message
       @param eraseTxQueue is true when called from onMessage(TMTransaction)
       and is false when called from onMessage(TMTransactions). If true then
       the transaction hash is erased from txQueue_. Don't need to erase from
       the queue when called from onMessage(TMTransactions) because this
       message is a response to the missing transactions request and the queue
       would not have any of these transactions.
       @param batch is false when called from onMessage(TMTransaction)
       and is true when called from onMessage(TMTransactions). If true, then the
       transaction is part of a batch, and should not be charged an extra fee.
     */
    void
    handleTransaction(
        std::shared_ptr<protocol::TMTransaction> const& m,
        bool eraseTxQueue,
        bool batch);

    /** Handle protocol message with hashes of transactions that have not
       been relayed by an upstream node down to its peers - request
       transactions, which have not been relayed to this peer.
       @param m protocol message with transactions' hashes
     */
    void
    handleHaveTransactions(
        std::shared_ptr<protocol::TMHaveTransactions> const& m);

public:
    //--------------------------------------------------------------------------
    //
    // ProtocolStream
    //
    //--------------------------------------------------------------------------

    void
    onMessageUnknown(std::uint16_t type);

    void
    onMessageBegin(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m,
        std::size_t size,
        std::size_t uncompressed_size,
        bool isCompressed);

    void
    onMessageEnd(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m);

    void
    onMessage(std::shared_ptr<protocol::TMManifests> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMPing> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMCluster> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMTransaction> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetLedger> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMLedgerData> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProposeSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMStatusChange> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorList> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorListCollection> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidation> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMHaveTransactions> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMTransactions> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMSquelch> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m);

private:
    //--------------------------------------------------------------------------
    // lockedRecentLock is passed as a reminder to callers that recentLock_
    // must be locked.
    void
    addLedger(
        uint256 const& hash,
        std::lock_guard<std::mutex> const& lockedRecentLock);

    void
    doFetchPack(std::shared_ptr<protocol::TMGetObjectByHash> const& packet);

    void
    onValidatorListMessage(
        std::string const& messageType,
        std::string const& manifest,
        std::uint32_t version,
        std::vector<ValidatorBlobInfo> const& blobs);

    /** Process peer's request to send missing transactions. The request is
        sent in response to TMHaveTransactions.
        @param packet protocol message containing missing transactions' hashes.
     */
    void
    doTransactions(std::shared_ptr<protocol::TMGetObjectByHash> const& packet);

    void
    checkTransaction(
        HashRouterFlags flags,
        bool checkSignature,
        std::shared_ptr<STTx const> const& stx,
        bool batch);

    void
    checkPropose(
        bool isTrusted,
        std::shared_ptr<protocol::TMProposeSet> const& packet,
        RCLCxPeerPos peerPos);

    void
    checkValidation(
        std::shared_ptr<STValidation> const& val,
        uint256 const& key,
        std::shared_ptr<protocol::TMValidation> const& packet);

    void
    sendLedgerBase(
        std::shared_ptr<Ledger const> const& ledger,
        protocol::TMLedgerData& ledgerData);

    std::shared_ptr<Ledger const>
    getLedger(std::shared_ptr<protocol::TMGetLedger> const& m);

    std::shared_ptr<SHAMap const>
    getTxSet(std::shared_ptr<protocol::TMGetLedger> const& m) const;

    void
    processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m);
};

//------------------------------------------------------------------------------

template <class Buffers>
PeerImp::PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
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
    , inbound_(false)
    , protocol_(protocol)
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.journal("Squelch"))
    , usage_(usage)
    , fee_{Resource::feeTrivialPeer}
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
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
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
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

template <class FwdIt, class>
void
PeerImp::sendEndpoints(FwdIt first, FwdIt last)
{
    protocol::TMEndpoints tm;

    while (first != last)
    {
        auto& tme2(*tm.add_endpoints_v2());
        tme2.set_endpoint(first->address.to_string());
        tme2.set_hops(first->hops);
        first++;
    }
    tm.set_version(2);

    send(std::make_shared<Message>(tm, protocol::mtENDPOINTS));
}

}  // namespace ripple

#endif
