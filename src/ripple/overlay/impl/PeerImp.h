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

#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/basics/Log.h> // deprecated
#include <ripple/nodestore/Database.h>
#include <ripple/overlay/predicates.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/resource/Fees.h>
#include <ripple/core/Config.h>
#include <ripple/core/Job.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/LoadEvent.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STValidation.h>
#include <beast/ByteOrder.h>
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/ssl_bundle.h>
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/utility/WrappedSink.h>
#include <cstdint>
#include <deque>
#include <queue>

namespace ripple {

class PeerImp
    : public Peer
    , public std::enable_shared_from_this <PeerImp>
    , public OverlayImpl::Child
{
public:
    /** Type of connection.
        The affects how messages are routed.
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

    using ptr = std::shared_ptr <PeerImp>;

private:
    using clock_type    = std::chrono::steady_clock;
    using error_code    = boost::system::error_code;
    using socket_type   = boost::asio::ip::tcp::socket;
    using stream_type   = boost::asio::ssl::stream <socket_type&>;
    using address_type  = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

    // The length of the smallest valid finished message
    static const size_t sslMinimumFinishedLength = 12;

    Application& app_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal journal_;
    beast::Journal p_journal_;
    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::io_service::strand strand_;
    boost::asio::basic_waitable_timer<
        std::chrono::steady_clock> timer_;

    //Type type_ = Type::legacy;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    OverlayImpl& overlay_;
    bool m_inbound;
    State state_;          // Current state
    std::atomic<Sanity> sanity_;
    clock_type::time_point insaneTime_;
    bool detaching_ = false;
    // Node public key of peer.
    RippleAddress publicKey_;
    std::string name_;
    uint256 sharedValue_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;
    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;
    std::deque<uint256> recentLedgers_;
    std::deque<uint256> recentTxSets_;

    std::chrono::milliseconds latency_ = std::chrono::milliseconds (-1);
    std::uint64_t lastPingSeq_ = 0;
    clock_type::time_point lastPingTime_;
    clock_type::time_point creationTime_;

    std::mutex mutable recentLock_;
    protocol::TMStatusChange last_status_;
    protocol::TMHello hello_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    PeerFinder::Slot::ptr slot_;
    beast::asio::streambuf read_buffer_;
    beast::http::message http_message_;
    beast::http::body http_body_;
    beast::asio::streambuf write_buffer_;
    std::queue<Message::pointer> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;
    int no_ping_ = 0;
    std::unique_ptr <LoadEvent> load_event_;
    bool hopsAware_ = false;

    friend class OverlayImpl;

public:
    PeerImp (PeerImp const&) = delete;
    PeerImp& operator= (PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    PeerImp (Application& app, id_t id, endpoint_type remote_endpoint,
        PeerFinder::Slot::ptr const& slot, beast::http::message&& request,
            protocol::TMHello const& hello, RippleAddress const& publicKey,
                Resource::Consumer consumer,
                    std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
                        OverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImp (Application& app, std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        Buffers const& buffers, PeerFinder::Slot::ptr&& slot,
            beast::http::message&& response, Resource::Consumer usage,
                protocol::TMHello&& hello,
                    RippleAddress const& legacyPublicKey, id_t id,
                        OverlayImpl& overlay);

    virtual
    ~PeerImp();

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    PeerFinder::Slot::ptr const&
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
    send (Message::pointer const& m) override;

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

    bool
    crawl() const;

    bool
    cluster() const override
    {
        return slot_->cluster();
    }

    bool
    hopsAware() const
    {
        return hopsAware_;
    }

    void
    check();

    /** Check if the peer is sane
        @param validationSeq The ledger sequence of a recently-validated ledger
    */
    void
    checkSanity (std::uint32_t validationSeq);

    void
    checkSanity (std::uint32_t seq1, std::uint32_t seq2);

    RippleAddress const&
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
    hasTxSet (uint256 const& hash) const override;

    void
    cycleStatus () override;

    bool
    supportsVersion (int version) override;

    bool
    hasRange (std::uint32_t uMin, std::uint32_t uMax) override;

    // Called to determine our priority for querying
    int
    getScore (bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const& reason);

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

    beast::http::message
    makeResponse (bool crawl, beast::http::message const& req,
        beast::IP::Endpoint remoteAddress,
        uint256 const& sharedValue);

    void
    onWriteResponse (error_code ec, std::size_t bytes_transferred);

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

    error_code
    onMessageUnknown (std::uint16_t type);

    error_code
    onMessageBegin (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m,
        std::size_t size);

    void
    onMessageEnd (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m);

    void onMessage (std::shared_ptr <protocol::TMHello> const& m);
    void onMessage (std::shared_ptr <protocol::TMManifests> const& m);
    void onMessage (std::shared_ptr <protocol::TMPing> const& m);
    void onMessage (std::shared_ptr <protocol::TMCluster> const& m);
    void onMessage (std::shared_ptr <protocol::TMGetPeers> const& m);
    void onMessage (std::shared_ptr <protocol::TMPeers> const& m);
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
    sendGetPeers();

    void
    addLedger (uint256 const& hash);

    void
    doFetchPack (const std::shared_ptr<protocol::TMGetObjectByHash>& packet);

    void
    checkTransaction (int flags, bool checkSignature,
        std::shared_ptr<STTx const> const& stx);

    void
    checkPropose (Job& job,
        std::shared_ptr<protocol::TMProposeSet> const& packet,
            LedgerProposal::pointer proposal);

    void
    checkValidation (STValidation::pointer val,
        bool isTrusted, std::shared_ptr<protocol::TMValidation> const& packet);

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
    Buffers const& buffers, PeerFinder::Slot::ptr&& slot,
        beast::http::message&& response, Resource::Consumer usage,
            protocol::TMHello&& hello,
                RippleAddress const& legacyPublicKey, id_t id,
                    OverlayImpl& overlay)
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
    , strand_ (socket_.get_io_service())
    , timer_ (socket_.get_io_service())
    , remote_address_ (slot->remote_endpoint())
    , overlay_ (overlay)
    , m_inbound (false)
    , state_ (State::active)
    , sanity_ (Sanity::unknown)
    , insaneTime_ (clock_type::now())
    , publicKey_ (legacyPublicKey)
    , creationTime_ (clock_type::now())
    , hello_ (std::move(hello))
    , usage_ (usage)
    , fee_ (Resource::feeLightPeer)
    , slot_ (std::move(slot))
    , http_message_(std::move(response))
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
        protocol::TMEndpoint& tme (*tm.add_endpoints());
        if (ep.address.is_v4())
            tme.mutable_ipv4()->set_ipv4(
                beast::toNetworkByteOrder (ep.address.to_v4().value));
        else
            tme.mutable_ipv4()->set_ipv4(0);
        tme.mutable_ipv4()->set_ipv4port (ep.address.port());
        tme.set_hops (ep.hops);
    }
    tm.set_version (1);

    send (std::make_shared <Message> (tm, protocol::mtENDPOINTS));
}

}

#endif
