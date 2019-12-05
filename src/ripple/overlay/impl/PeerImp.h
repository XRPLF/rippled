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

#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/resource/Fees.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/optional.hpp>
#include <cstdint>
#include <deque>
#include <queue>
#include <shared_mutex>

namespace ripple {

class PeerImp
    : public Peer
    , public std::enable_shared_from_this <PeerImp>
    , public OverlayImpl::Child
{
public:
    /** Type of connection.
        This affects how messages are routed.
    */
    enum class Type
    {
        legacy,
        leaf,
        peer
    };

    /** Current state */
    enum class State
    {
        /** A connection is being established (outbound) */
        connecting

        /** Connection has been successfully established */
        ,connected

        /** Handshake has been received from this peer */
        ,handshaked

        /** Running the Ripple protocol actively */
        ,active
    };

    enum class Sanity
    {
        insane
        ,unknown
        ,sane
    };

    struct ShardInfo
    {
        beast::IP::Endpoint endpoint;
        RangeSet<std::uint32_t> shardIndexes;
    };

    using ptr = std::shared_ptr <PeerImp>;

private:
    using clock_type    = std::chrono::steady_clock;
    using error_code    = boost::system::error_code;
    using socket_type   = boost::asio::ip::tcp::socket;
    using stream_type   = boost::asio::ssl::stream <socket_type&>;
    using address_type  = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    Application& app_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal const journal_;
    beast::Journal const p_journal_;
    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;
    waitable_timer timer_;

    //Type type_ = Type::legacy;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    OverlayImpl& overlay_;
    bool const m_inbound;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    State state_;          // Current state
    std::atomic<Sanity> sanity_;
    clock_type::time_point insaneTime_;
    bool detaching_ = false;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    std::shared_timed_mutex mutable nameMutex_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;
    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;
    std::deque<uint256> recentLedgers_;
    std::deque<uint256> recentTxSets_;

    boost::optional<std::chrono::milliseconds> latency_;
    boost::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;

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
    // o insaneTime_
    // o latency_
    //
    // The following variables are being protected preemptively:
    //
    // o name_
    // o last_status_
    // o lastPingSeq_
    // o lastPingTime_
    // o no_ping_
    // 
    // June 2019

    std::mutex mutable recentLock_;
    protocol::TMStatusChange last_status_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    boost::beast::multi_buffer write_buffer_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;
    int no_ping_ = 0;
    std::unique_ptr <LoadEvent> load_event_;

    std::mutex mutable shardInfoMutex_;
    hash_map<PublicKey, ShardInfo> shardInfo_;

    friend class OverlayImpl;

    class Metrics {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics& operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics& operator=(Metrics&&) = delete;

        void add_message(std::uint64_t bytes);
        std::uint64_t average_bytes() const;
        std::uint64_t total_bytes() const;

    private:
        std::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{ 30, 0ull };
        clock_type::time_point intervalStart_{ clock_type::now() };
        std::uint64_t totalBytes_{ 0 };
        std::uint64_t accumBytes_{ 0 };
        std::uint64_t rollingAvgBytes_{ 0 };
    };

    struct {
        Metrics sent;
        Metrics recv;
    } metrics_;

public:
    PeerImp (PeerImp const&) = delete;
    PeerImp& operator= (PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    PeerImp (Application& app, id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot, http_request_type&& request,
            PublicKey const& publicKey,
                ProtocolVersion protocol, Resource::Consumer consumer,
                    std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
                        OverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImp (Application& app, std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        Buffers const& buffers, std::shared_ptr<PeerFinder::Slot>&& slot,
            http_response_type&& response, Resource::Consumer usage,
                PublicKey const& publicKey,
                    ProtocolVersion protocol, id_t id, OverlayImpl& overlay);

    virtual
    ~PeerImp();

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
    void
    run();

    // Called when Overlay gets a stop request.
    void
    stop() override;

    //
    // Network
    //

    void
    send (std::shared_ptr<Message> const& m) override;

    /** Send a set of PeerFinder endpoints as a protocol message. */
    template <class FwdIt, class = typename std::enable_if_t<std::is_same<
        typename std::iterator_traits<FwdIt>::value_type,
            PeerFinder::Endpoint>::value>>
    void
    sendEndpoints (FwdIt first, FwdIt last);

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    void
    charge (Resource::Charge const& fee) override;

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

    void
    check();

    /** Check if the peer is sane
        @param validationSeq The ledger sequence of a recently-validated ledger
    */
    void
    checkSanity (std::uint32_t validationSeq);

    void
    checkSanity (std::uint32_t seq1, std::uint32_t seq2);

    PublicKey const&
    getNodePublic () const override
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

    //
    // Ledger
    //

    uint256 const&
    getClosedLedgerHash () const override
    {
        return closedLedgerHash_;
    }

    bool
    hasLedger (uint256 const& hash, std::uint32_t seq) const override;

    void
    ledgerRange (std::uint32_t& minSeq, std::uint32_t& maxSeq) const override;

    bool
    hasShard (std::uint32_t shardIndex) const override;

    bool
    hasTxSet (uint256 const& hash) const override;

    void
    cycleStatus () override;

    bool
    hasRange (std::uint32_t uMin, std::uint32_t uMax) override;

    // Called to determine our priority for querying
    int
    getScore (bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const& reason);

    /** Return a range set of known shard indexes from this peer. */
    boost::optional<RangeSet<std::uint32_t>>
    getShardIndexes() const;

    /** Return any known shard info from this peer and its sub peers. */
    boost::optional<hash_map<PublicKey, ShardInfo>>
    getPeerShardInfo() const;

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

    static
    std::string
    makePrefix(id_t id);

    // Called when the timer wait completes
    void
    onTimer (boost::system::error_code const& ec);

    // Called when SSL shutdown completes
    void
    onShutdown (error_code ec);

    void
    doAccept();

    http_response_type
    makeResponse (bool crawl, http_request_type const& req,
        beast::IP::Address remote_ip,
        uint256 const& sharedValue);

    void
    onWriteResponse (error_code ec, std::size_t bytes_transferred);

    // A thread-safe way of getting the name.
    std::string
    getName() const;

    //
    // protocol message loop
    //

    // Starts the protocol message loop
    void
    doProtocolStart ();

    // Called when protocol message bytes are received
    void
    onReadMessage (error_code ec, std::size_t bytes_transferred);

    // Called when protocol messages bytes are sent
    void
    onWriteMessage (error_code ec, std::size_t bytes_transferred);

public:
    //--------------------------------------------------------------------------
    //
    // ProtocolStream
    //
    //--------------------------------------------------------------------------

    static
    error_code
    invalid_argument_error()
    {
        return boost::system::errc::make_error_code (
            boost::system::errc::invalid_argument);
    }

    void
    onMessageUnknown (std::uint16_t type);

    void
    onMessageBegin (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m,
        std::size_t size);

    void
    onMessageEnd (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m);

    void onMessage (std::shared_ptr <protocol::TMManifests> const& m);
    void onMessage (std::shared_ptr <protocol::TMPing> const& m);
    void onMessage (std::shared_ptr <protocol::TMCluster> const& m);
    void onMessage (std::shared_ptr <protocol::TMGetShardInfo> const& m);
    void onMessage (std::shared_ptr <protocol::TMShardInfo> const& m);
    void onMessage (std::shared_ptr <protocol::TMGetPeerShardInfo> const& m);
    void onMessage (std::shared_ptr <protocol::TMPeerShardInfo> const& m);
    void onMessage (std::shared_ptr <protocol::TMEndpoints> const& m);
    void onMessage (std::shared_ptr <protocol::TMTransaction> const& m);
    void onMessage (std::shared_ptr <protocol::TMGetLedger> const& m);
    void onMessage (std::shared_ptr <protocol::TMLedgerData> const& m);
    void onMessage (std::shared_ptr <protocol::TMProposeSet> const& m);
    void onMessage (std::shared_ptr <protocol::TMStatusChange> const& m);
    void onMessage (std::shared_ptr <protocol::TMHaveTransactionSet> const& m);
    void onMessage (std::shared_ptr <protocol::TMValidation> const& m);
    void onMessage (std::shared_ptr <protocol::TMGetObjectByHash> const& m);

private:
    State state() const
    {
        return state_;
    }

    void state (State new_state)
    {
        state_ = new_state;
    }

    //--------------------------------------------------------------------------
    void
    setLedgerState();

    // lockedRecentLock is passed as a reminder to callers that recentLock_
    // must be locked.
    void
    addLedger (uint256 const& hash,
        std::lock_guard<std::mutex> const& lockedRecentLock);

    void
    doFetchPack (const std::shared_ptr<protocol::TMGetObjectByHash>& packet);

    void
    checkTransaction (int flags, bool checkSignature,
        std::shared_ptr<STTx const> const& stx);

    void
    checkPropose (Job& job,
        std::shared_ptr<protocol::TMProposeSet> const& packet,
            RCLCxPeerPos peerPos);

    void
    checkValidation (STValidation::pointer val,
        std::shared_ptr<protocol::TMValidation> const& packet);

    void
    getLedger (std::shared_ptr<protocol::TMGetLedger> const&packet);

    // Called when we receive tx set data.
    void
    peerTXData (uint256 const& hash,
        std::shared_ptr <protocol::TMLedgerData> const& pPacket,
            beast::Journal journal);
};


//------------------------------------------------------------------------------

template <class Buffers>
PeerImp::PeerImp (Application& app, std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
    Buffers const& buffers, std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response, Resource::Consumer usage,
            PublicKey const& publicKey,
                ProtocolVersion protocol, id_t id, OverlayImpl& overlay)
    : Child (overlay)
    , app_ (app)
    , id_ (id)
    , sink_ (app_.journal("Peer"), makePrefix(id))
    , p_sink_ (app_.journal("Protocol"), makePrefix(id))
    , journal_ (sink_)
    , p_journal_ (p_sink_)
    , ssl_bundle_(std::move(ssl_bundle))
    , socket_ (ssl_bundle_->socket)
    , stream_ (ssl_bundle_->stream)
    , strand_ (socket_.get_executor())
    , timer_ (waitable_timer{socket_.get_executor()})
    , remote_address_ (slot->remote_endpoint())
    , overlay_ (overlay)
    , m_inbound (false)
    , protocol_ (protocol)
    , state_ (State::active)
    , sanity_ (Sanity::unknown)
    , insaneTime_ (clock_type::now())
    , publicKey_ (publicKey)
    , creationTime_ (clock_type::now())
    , usage_ (usage)
    , fee_ (Resource::feeLightPeer)
    , slot_ (std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
{
    read_buffer_.commit (boost::asio::buffer_copy(read_buffer_.prepare(
        boost::asio::buffer_size(buffers)), buffers));
}

template <class FwdIt, class>
void
PeerImp::sendEndpoints (FwdIt first, FwdIt last)
{
    protocol::TMEndpoints tm;
    for (;first != last; ++first)
    {
        auto const& ep = *first;
        // eventually remove endpoints and just keep endpoints_v2
        // (once we are sure the entire network understands endpoints_v2)
        protocol::TMEndpoint& tme (*tm.add_endpoints());
        if (ep.address.is_v4())
            tme.mutable_ipv4()->set_ipv4(
                boost::endian::native_to_big(
                    static_cast<std::uint32_t>(ep.address.to_v4().to_ulong())));
        else
            tme.mutable_ipv4()->set_ipv4(0);
        tme.mutable_ipv4()->set_ipv4port (ep.address.port());
        tme.set_hops (ep.hops);

        // add v2 endpoints (strings)
        auto& tme2 (*tm.add_endpoints_v2());
        tme2.set_endpoint(ep.address.to_string());
        tme2.set_hops (ep.hops);
    }
    tm.set_version (2);

    send (std::make_shared <Message> (tm, protocol::mtENDPOINTS));
}

}

#endif
