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

#include <ripple/common/MultiSocket.h>
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
#include <ripple/data/protocol/Protocol.h>
#include <ripple/validators/Manager.h>

// VFALCO This is unfortunate. Comment this out and
//        just include what is needed.
#include <ripple/unity/app.h>

#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/http/message.h>
#include <beast/http/parser.h>

#include <boost/foreach.hpp>

#include <cstdint>

namespace ripple {

typedef boost::asio::ip::tcp::socket NativeSocketType;

class PeerImp;

std::string to_string (Peer const& peer);
std::ostream& operator<< (std::ostream& os, Peer const& peer);

std::string to_string (Peer const* peer);
std::ostream& operator<< (std::ostream& os, Peer const* peer);

std::string to_string (PeerImp const& peer);
std::ostream& operator<< (std::ostream& os, PeerImp const& peer);

std::string to_string (PeerImp const* peer);
std::ostream& operator<< (std::ostream& os, PeerImp const* peer);

//------------------------------------------------------------------------------

class PeerImp
    : public Peer
    , public std::enable_shared_from_this <PeerImp>
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
    // Time alloted for a peer to send a HELLO message (DEPRECATED)
    static const boost::posix_time::seconds nodeVerifySeconds;

    // The clock drift we allow a remote peer to have
    static const std::uint32_t clockToleranceDeltaSeconds = 20;

    // The length of the smallest valid finished message
    static const size_t sslMinimumFinishedLength = 12;

    NativeSocketType m_owned_socket;

    beast::Journal m_journal;

    // A unique identifier (up to a restart of rippled) for this particular
    // peer instance. A peer that disconnects will, upon reconnection, get a
    // new ID.
    ShortId m_shortId = 0;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible. This includes
    // the case where we learn the true IP via a PROXY handshake.
    beast::IP::Endpoint m_remoteAddress;

    // These is up here to prevent warnings about order of initializations
    //
    Resource::Manager& m_resourceManager;
    PeerFinder::Manager& m_peerFinder;
    OverlayImpl& m_overlay;
    bool m_inbound;

    std::unique_ptr <MultiSocket> m_socket;
    boost::asio::io_service::strand m_strand;

    State           m_state;          // Current state
    bool m_detaching = false;
    
    // True if peer is a node in our cluster
    bool m_clusterNode = false;

    // Node public key of peer.
    RippleAddress m_nodePublicKey;

    std::string m_nodeName;

    // Both sides of the peer calculate this value and verify that it matches
    // to detect/prevent man-in-the-middle attacks.
    //
    uint256 m_secureCookie;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex m_minLedger;
    LedgerIndex m_maxLedger;

    uint256 m_closedLedgerHash;
    uint256 m_previousLedgerHash;

    std::list<uint256>    m_recentLedgers;
    std::list<uint256>    m_recentTxSets;
    mutable std::mutex  m_recentLock;

    boost::asio::deadline_timer timer_;

    std::list <Message::pointer> mSendQ;
    Message::pointer mSendingPacket;
    protocol::TMStatusChange mLastStatus;
    protocol::TMHello mHello;

    Resource::Consumer m_usage;

    // The slot assigned to us by PeerFinder
    PeerFinder::Slot::ptr m_slot;

    // True if close was called
    bool m_was_canceled = false;

    boost::asio::streambuf read_buffer_;
    boost::optional <beast::http::message> http_message_;
    boost::optional <beast::http::parser> http_parser_;
    message_stream message_stream_;

    boost::asio::streambuf write_buffer_;
    
    std::unique_ptr <LoadEvent> load_event_;

    //--------------------------------------------------------------------------

public:
    /** Create an incoming peer from the specified socket */
    PeerImp (NativeSocketType&& socket, beast::IP::Endpoint remoteAddress,
        OverlayImpl& overlay, Resource::Manager& resourceManager,
            PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot,
                boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags);

    /** Create an outgoing peer
        @note Construction of outbound peers is a two step process: a second
              call is needed (to connect or accept) but we cannot make it from
              inside the constructor because you cannot call shared_from_this
              from inside constructors.
    */
    PeerImp (beast::IP::Endpoint remoteAddress, boost::asio::io_service& io_service,
        OverlayImpl& overlay, Resource::Manager& resourceManager,
            PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot,
                boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags);

    virtual
    ~PeerImp ();

    PeerImp (PeerImp const&) = delete;
    PeerImp& operator= (PeerImp const&) = delete;

    // Begin asynchronous initiation function calls
    void
    start ();

    /** Indicates that the peer must be activated.
        A peer is activated after the handshake is completed and if it is not
        a second connection from a peer that we already have. Once activated
        the peer transitions to `stateActive` and begins operating.
    */
    void
    activate ();

    /** Close the connection. */
    void close (bool graceful);

    void
    getLedger (protocol::TMGetLedger& packet);

    //
    // Network
    //

    void
    send (Message::pointer const& m) override;

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
    getNodePublic () const;

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
        return m_state;
    }

    void state (State new_state)
    {
        m_state = new_state;
    }

    //--------------------------------------------------------------------------

    /** Disconnect a peer

        The peer transitions from its current state into `stateGracefulClose`

        @param rsn a code indicating why the peer was disconnected
        @param onIOStrand true if called on an I/O strand. It if is not, then
               a callback will be queued up.
    */
    void
    detach (const char* rsn, bool graceful = true);

    void
    sendGetPeers ();

    static
    void
    charge (std::weak_ptr <Peer>& peer, Resource::Charge const& fee);

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
    doProofOfWork (Job&, std::weak_ptr <Peer> peer, ProofOfWork::pointer pow);

    static
    void checkTransaction (Job&, int flags, SerializedTransaction::pointer stx,
        std::weak_ptr<Peer> peer);

    // Called from our JobQueue
    static
    void
    checkPropose (Job& job, Overlay* pPeers,
        std::shared_ptr<protocol::TMProposeSet> packet,
            LedgerProposal::pointer proposal, uint256 consensusLCL,
                RippleAddress nodePublic, std::weak_ptr<Peer> peer,
                    bool fromCluster, beast::Journal journal);

    static
    void
    checkValidation (Job&, Overlay* pPeers, SerializedValidation::pointer val,
        bool isTrusted, bool isCluster,
            std::shared_ptr<protocol::TMValidation> packet,
                std::weak_ptr<Peer> peer, beast::Journal journal);

    static
    void
    sGetLedger (std::weak_ptr<PeerImp> wPeer,
        std::shared_ptr <protocol::TMGetLedger> packet);

    /** Called when we receive tx set data. */
    static
    void
    peerTXData (Job&, std::weak_ptr <Peer> wPeer, uint256 const& hash,
        std::shared_ptr <protocol::TMLedgerData> pPacket,
            beast::Journal journal);
};

//------------------------------------------------------------------------------

const boost::posix_time::seconds PeerImp::nodeVerifySeconds (15);

//------------------------------------------------------------------------------

// to_string should not be used we should just use lexical_cast maybe

inline
std::string
to_string (PeerImp const& peer)
{
    if (peer.isInCluster())
        return peer.getClusterNodeName();

    return peer.getRemoteAddress().to_string();
}

inline
std::string
to_string (PeerImp const* peer)
{
    return to_string (*peer);
}

inline
std::ostream&
operator<< (std::ostream& os, PeerImp const& peer)
{
    os << to_string (peer);

    return os;
}

inline
std::ostream&
operator<< (std::ostream& os, PeerImp const* peer)
{
    os << to_string (peer);
    return os;
}

//------------------------------------------------------------------------------

inline
std::string
to_string (Peer const& peer)
{
    if (peer.isInCluster())
        return peer.getClusterNodeName();
    return peer.getRemoteAddress().to_string();
}

inline
std::string
to_string (Peer const* peer)
{
    return to_string (*peer);
}

inline
std::ostream&
operator<< (std::ostream& os, Peer const& peer)
{
    os << to_string (peer);
    return os;
}

inline
std::ostream&
operator<< (std::ostream& os, Peer const* peer)
{
    os << to_string (peer);
    return os;
}

}

#endif
