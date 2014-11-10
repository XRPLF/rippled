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

#include <ripple/nodestore/Database.h>
#include <ripple/overlay/predicates.h>
#include <ripple/overlay/impl/message_name.h>
#include <ripple/overlay/impl/message_stream.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/peer_protocol_detector.h>
#include <ripple/app/misc/ProofOfWork.h>
#include <ripple/app/misc/ProofOfWorkFactory.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/validators/Manager.h>
#include <ripple/unity/app.h> // VFALCO REMOVE
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/ssl_bundle.h>
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/utility/WrappedSink.h>
#include <cstdint>

namespace ripple {

class PeerImp;

class PeerImp
    : public Peer
    , public std::enable_shared_from_this <PeerImp>
    , public OverlayImpl::Child
    , private beast::LeakChecked <Peer>
    , private abstract_protocol_handler
{
public:
    /** Current state */
    enum State
    {
        /** A connection is being established (outbound) */
         stateConnecting

        /** Connection has been successfully established */
        ,stateConnected

        /** Handshake has been received from this peer */
        ,stateHandshaked

        /** Running the Ripple protocol actively */
        ,stateActive

        /** Gracefully closing */
        ,stateGracefulClose
    };

    typedef std::shared_ptr <PeerImp> ptr;

private:
    /** Wraps a Journal::Sink to prefix it's output. */
    class WrappedSink : public beast::Journal::Sink
    {
    private:
        std::string prefix_;
        beast::Journal::Sink& sink_;

    public:
        explicit
        WrappedSink (beast::Journal::Sink& sink)
            : sink_ (sink)
        {
        }

        explicit
        WrappedSink (beast::Journal const& journal)
            : sink_ (journal.sink())
        {
        }

        void prefix (std::string const& s)
        {
            prefix_ = s;
        }

        bool
        active (beast::Journal::Severity level) const override
        {
            return sink_.active (level);
        }

        bool
        console () const override
        {
            return sink_.console ();
        }

        void console (bool output) override
        {
            sink_.console (output);
        }

        beast::Journal::Severity
        severity() const
        {
            return sink_.severity();
        }

        void severity (beast::Journal::Severity level)
        {
            sink_.severity (level);
        }

        void write (beast::Journal::Severity level, std::string const& text)
        {
            using beast::Journal;
            sink_.write (level, prefix_ + text);
        }
    };

    using clock_type = std::chrono::steady_clock;
    using error_code= boost::system::error_code ;
    using yield_context = boost::asio::yield_context;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream <socket_type&>;

    // Time alloted for a peer to send a HELLO message (DEPRECATED)
    static const boost::posix_time::seconds nodeVerifySeconds;

    // The clock drift we allow a remote peer to have
    static const std::uint32_t clockToleranceDeltaSeconds = 20;

    // The length of the smallest valid finished message
    static const size_t sslMinimumFinishedLength = 12;

    WrappedSink sink_;
    WrappedSink p_sink_;
    beast::Journal journal_;
    beast::Journal p_journal_;
    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::io_service::strand strand_;
    boost::asio::deadline_timer timer_;

    // A unique identifier (up to a restart of rippled) for this particular
    // peer instance. A peer that disconnects will, upon reconnection, get a
    // new ID.
    ShortId shortId_ = 0;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint remote_address_;

    // These is up here to prevent warnings about order of initializations
    //
    Resource::Manager& resourceManager_;
    PeerFinder::Manager& peerFinder_;
    OverlayImpl& overlay_;
    bool m_inbound;

    State state_;          // Current state
    bool detaching_ = false;

    // True if peer is a node in our cluster
    bool clusterNode_ = false;

    // Node public key of peer.
    RippleAddress publicKey_;

    std::string name_;

    // Both sides of the peer calculate this value and verify that it matches
    // to detect/prevent man-in-the-middle attacks.
    //
    uint256 secureCookie_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;

    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;

    std::list<uint256> recentLedgers_;
    std::list<uint256> recentTxSets_;
    mutable std::mutex recentLock_;

    std::list <Message::pointer> send_queue_;
    Message::pointer send_packet_;
    protocol::TMStatusChange last_status_;
    protocol::TMHello hello_;

    Resource::Consumer usage_;

    // The slot assigned to us by PeerFinder
    PeerFinder::Slot::ptr slot_;

    beast::asio::streambuf read_buffer_;
    boost::optional <beast::http::message> http_message_;
    boost::optional <beast::http::parser> http_parser_;
    beast::http::body http_body_;
    message_stream message_stream_;

    beast::asio::streambuf write_buffer_;

    std::unique_ptr <LoadEvent> load_event_;

    //--------------------------------------------------------------------------

public:
    PeerImp (PeerImp const&) = delete;
    PeerImp& operator= (PeerImp const&) = delete;

    /** Create an incoming legacy peer from an established ssl connection. */
    template <class ConstBufferSequence>
    PeerImp (std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        ConstBufferSequence const& buffer, beast::IP::Endpoint remoteAddress,
            OverlayImpl& overlay, Resource::Manager& resourceManager,
                PeerFinder::Manager& peerFinder,
                    PeerFinder::Slot::ptr const& slot);

    /** Create an active incoming peer from an established ssl connection. */
    PeerImp (std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        beast::http::message&& request, beast::IP::Endpoint remoteAddress,
            OverlayImpl& overlay, Resource::Manager& resourceManager,
                PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot);

    /** Create an outgoing peer. */
    PeerImp (beast::IP::Endpoint remoteAddress, boost::asio::io_service& io_service,
        OverlayImpl& overlay, Resource::Manager& resourceManager,
            PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot,
                std::shared_ptr<boost::asio::ssl::context> const& context);

    virtual
    ~PeerImp ();

    PeerFinder::Slot::ptr const&
    slot()
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    void
    start();

    void
    getLedger (protocol::TMGetLedger& packet);

    // Cancel all I/O and close the socket
    void
    close() override;

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
    send_endpoints (FwdIt first, FwdIt last);

    beast::IP::Endpoint
    getRemoteAddress() const override;

    void
    charge (Resource::Charge const& fee) override;

    //
    // Identity
    //

    Peer::ShortId
    getShortId () const override;

    RippleAddress const&
    getNodePublic () const override;

    Json::Value
    json() override;

    bool
    isInCluster () const override;

    std::string const&
    getClusterNodeName() const override;

    //
    // Ledger
    //

    uint256 const&
    getClosedLedgerHash () const override;

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

private:
    void
    setPrefix();

    //
    // client role
    //

    void
    do_connect();

    void
    on_connect (error_code ec);

    beast::http::message
    make_request();

    void
    on_connect_ssl (error_code ec);

    void
    on_write_http_request (error_code ec, std::size_t bytes_transferred);

    template <class Streambuf>
    void
    processResponse (beast::http::message const& m, Streambuf const& body);

    void
    on_read_http_response (error_code ec, std::size_t bytes_transferred);

    //
    // server role
    //

    void
    do_accept();

    void
    on_accept_ssl (error_code ec);

    void
    on_read_http_detect (error_code ec, std::size_t bytes_transferred);

    void
    on_read_http_request (error_code ec, std::size_t bytes_transferred);

    beast::http::message
    make_response (beast::http::message const& req);

    void
    on_write_http_response (error_code ec, std::size_t bytes_transferred);

    //
    // protocol
    //

    void
    do_protocol_start();

    void
    on_read_protocol (error_code ec, std::size_t bytes_transferred);

    void
    on_write_protocol (error_code ec, std::size_t bytes_transferred);

    void
    handleShutdown (boost::system::error_code const& ec);

    void
    handleWrite (boost::system::error_code const& ec, size_t bytes);

    void
    handleVerifyTimer (boost::system::error_code const& ec);

    //--------------------------------------------------------------------------
    //
    // abstract_protocol_handler
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
    on_message_unknown (std::uint16_t type) override;

    error_code
    on_message_begin (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m) override;

    void
    on_message_end (std::uint16_t type,
        std::shared_ptr <::google::protobuf::Message> const& m) override;

    // message handlers
    error_code on_message (std::shared_ptr <protocol::TMHello> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMPing> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMProofWork> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMCluster> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMGetPeers> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMPeers> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMEndpoints> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMTransaction> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMGetLedger> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMLedgerData> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMProposeSet> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMStatusChange> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMHaveTransactionSet> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMValidation> const& m) override;
    error_code on_message (std::shared_ptr <protocol::TMGetObjectByHash> const& m) override;

    //--------------------------------------------------------------------------

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

    // DEPRECATED Close the socket
    void
    detach (const char* rsn);

    void
    sendGetPeers ();

    static
    void
    charge (std::weak_ptr <PeerImp>& peer, Resource::Charge const& fee);

    void
    sendForce (const Message::pointer& packet);

    /** Hashes the latest finished message from an SSL stream
        @param sslSession the session to get the message from.
        @param hash       the buffer into which the hash of the retrieved
                          message will be saved. The buffer MUST be at least
                          64 bytes long.
        @param getMessage a pointer to the function to call to retrieve the
                          finished message. This be either:
                          `SSL_get_finished` or
                          `SSL_get_peer_finished`.
        @return `true` if successful, `false` otherwise.
    */
    bool
    hashLatestFinishedMessage (const SSL *sslSession, unsigned char *hash,
        size_t (*getFinishedMessage)(const SSL *, void *buf, size_t));

    /** Generates a secure cookie to protect against man-in-the-middle attacks
        This function should never fail under normal circumstances and regular
        server operation.
        A failure prevents the cookie value from being calculated which is an
        important component of connection security. If this function fails, a
        secure connection cannot be established and the link MUST be dropped.
        @return `true` if the cookie was generated, `false` otherwise.
        @note failure is an exceptional situation - it should never happen and
              will almost always indicate an active man-in-the-middle attack is
              taking place.
    */
    bool
    calculateSessionCookie ();

    /** Perform a secure handshake with the peer at the other end.
        If this function returns false then we cannot guarantee that there
        is no active man-in-the-middle attack taking place and the link
        MUST be disconnected.
        @return true if successful, false otherwise.
    */
    bool
    sendHello();

    void
    addLedger (uint256 const& hash);

    void
    addTxSet (uint256 const& hash);

    void
    doFetchPack (const std::shared_ptr<protocol::TMGetObjectByHash>& packet);

    void
    doProofOfWork (Job&, std::weak_ptr <PeerImp> peer, ProofOfWork::pointer pow);

    static
    void checkTransaction (Job&, int flags, SerializedTransaction::pointer stx,
        std::weak_ptr<PeerImp> peer);

    // Called from our JobQueue
    static
    void
    checkPropose (Job& job, Overlay* pPeers,
        std::shared_ptr<protocol::TMProposeSet> packet,
            LedgerProposal::pointer proposal, uint256 consensusLCL,
                RippleAddress nodePublic, std::weak_ptr<PeerImp> peer,
                    bool fromCluster, beast::Journal journal);

    static
    void
    checkValidation (Job&, Overlay* pPeers, SerializedValidation::pointer val,
        bool isTrusted, bool isCluster,
            std::shared_ptr<protocol::TMValidation> packet,
                std::weak_ptr<PeerImp> peer, beast::Journal journal);

    static
    void
    sGetLedger (std::weak_ptr<PeerImp> wPeer,
        std::shared_ptr <protocol::TMGetLedger> packet);

    /** Called when we receive tx set data. */
    static
    void
    peerTXData (Job&, std::weak_ptr <PeerImp> wPeer, uint256 const& hash,
        std::shared_ptr <protocol::TMLedgerData> pPacket,
            beast::Journal journal);
};

//------------------------------------------------------------------------------

template <class ConstBufferSequence>
PeerImp::PeerImp (std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
    ConstBufferSequence const& buffer, beast::IP::Endpoint remoteAddress,
        OverlayImpl& overlay, Resource::Manager& resourceManager,
            PeerFinder::Manager& peerFinder,
                PeerFinder::Slot::ptr const& slot)
    : Child (overlay)
    , sink_(deprecatedLogs().journal("Peer"))
    , p_sink_(deprecatedLogs().journal("Protocol"))
    , journal_ (sink_)
    , p_journal_(p_sink_)
    , ssl_bundle_(std::move(ssl_bundle))
    , socket_ (ssl_bundle_->socket)
    , stream_ (ssl_bundle_->stream)
    , strand_ (socket_.get_io_service())
    , timer_ (socket_.get_io_service())
    , remote_address_ (remoteAddress)
    , resourceManager_ (resourceManager)
    , peerFinder_ (peerFinder)
    , overlay_ (overlay)
    , m_inbound (true)
    , state_ (stateConnected)
    , slot_ (slot)
    , message_stream_(*this)
{
    setPrefix();
    read_buffer_.commit(boost::asio::buffer_copy(read_buffer_.prepare(
        boost::asio::buffer_size(buffer)), buffer));
}

template <class FwdIt, class>
void
PeerImp::send_endpoints (FwdIt first, FwdIt last)
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

//------------------------------------------------------------------------------

// DEPRECATED
const boost::posix_time::seconds PeerImp::nodeVerifySeconds (15);

}

#endif
