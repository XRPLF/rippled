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
#include <ripple/module/app/misc/ProofOfWork.h>
#include <ripple/module/app/misc/ProofOfWorkFactory.h>
#include <ripple/module/data/protocol/Protocol.h>
#include <ripple/unity/validators.h>
#include <ripple/unity/peerfinder.h>

// VFALCO This is unfortunate. Comment this out and
//        just include what is needed.
#include <ripple/unity/app.h>

#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
    
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
private:
    /** Time alloted for a peer to send a HELLO message (DEPRECATED) */
    static const boost::posix_time::seconds nodeVerifySeconds;

    /** The clock drift we allow a remote peer to have */
    static const std::uint32_t clockToleranceDeltaSeconds = 20;

    /** The length of the smallest valid finished message */
    static const size_t sslMinimumFinishedLength = 12;

    //--------------------------------------------------------------------------
    /** We have accepted an inbound connection.

        The connection state transitions from `stateConnect` to `stateConnected`
        as `stateConnect`.
    */
    void accept ()
    {
        m_journal.info << "Accepted " << m_remoteAddress;

        m_socket->set_verify_mode (boost::asio::ssl::verify_none);
        m_socket->async_handshake (
            boost::asio::ssl::stream_base::server,
            m_strand.wrap (std::bind (
                &PeerImp::handleStart,
                std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
    }

    /** Attempt an outbound connection.

        The connection may fail (for a number of reasons) and we do not know
        what will happen at this point.

        The connection state does not transition with this function and remains
        as `stateConnecting`.
    */
    void connect ()
    {
        m_journal.info << "Connecting to " << m_remoteAddress;

        boost::system::error_code err;

        m_timer.expires_from_now (nodeVerifySeconds, err);

        m_timer.async_wait (m_strand.wrap (std::bind (
            &PeerImp::handleVerifyTimer,
            shared_from_this (), beast::asio::placeholders::error)));

        if (err)
        {
            m_journal.error << "Failed to set verify timer.";
            detach ("c2");
            return;
        }

        m_socket->next_layer <NativeSocketType>().async_connect (
            beast::IPAddressConversion::to_asio_endpoint (m_remoteAddress),
                m_strand.wrap (std::bind (&PeerImp::onConnect,
                    shared_from_this (), beast::asio::placeholders::error)));
    }

public:
    /** Current state */
    enum State
    {
        /** An connection is being established (outbound) */
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

    NativeSocketType m_owned_socket;

    beast::Journal m_journal;

    // A unique identifier (up to a restart of rippled) for this particular
    // peer instance. A peer that disconnects will, upon reconnection, get a
    // new ID.
    ShortId m_shortId;

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
    bool            m_detaching;      // True if detaching.
    bool            m_clusterNode;    // True if peer is a node in our cluster
    RippleAddress   m_nodePublicKey;  // Node public key of peer.
    std::string     m_nodeName;

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

    boost::asio::deadline_timer         m_timer;

    std::vector<uint8_t>                m_readBuffer;
    std::list<Message::pointer>   mSendQ;
    Message::pointer              mSendingPacket;
    protocol::TMStatusChange            mLastStatus;
    protocol::TMHello                   mHello;

    Resource::Consumer m_usage;

    // The slot assigned to us by PeerFinder
    PeerFinder::Slot::ptr m_slot;

    // True if close was called
    bool m_was_canceled;

    boost::asio::streambuf read_buffer_;
    message_stream message_stream_;
    std::unique_ptr <LoadEvent> load_event_;

    //--------------------------------------------------------------------------
    /** New incoming peer from the specified socket */
    PeerImp (
        NativeSocketType&& socket,
        beast::IP::Endpoint remoteAddress,
        OverlayImpl& overlay,
        Resource::Manager& resourceManager,
        PeerFinder::Manager& peerFinder,
        PeerFinder::Slot::ptr const& slot,
        boost::asio::ssl::context& ssl_context,
        MultiSocket::Flag flags)
            : m_owned_socket (std::move (socket))
            , m_journal (LogPartition::getJournal <Peer> ())
            , m_shortId (0)
            , m_remoteAddress (remoteAddress)
            , m_resourceManager (resourceManager)
            , m_peerFinder (peerFinder)
            , m_overlay (overlay)
            , m_inbound (true)
            , m_socket (MultiSocket::New (
                m_owned_socket, ssl_context, flags.asBits ()))
            , m_strand (m_owned_socket.get_io_service())
            , m_state (stateConnected)
            , m_detaching (false)
            , m_clusterNode (false)
            , m_minLedger (0)
            , m_maxLedger (0)
            , m_timer (m_owned_socket.get_io_service())
            , m_slot (slot)
            , m_was_canceled (false)
            , message_stream_(*this)
    {
    }

    /** New outgoing peer
        @note Construction of outbound peers is a two step process: a second
              call is needed (to connect or accept) but we cannot make it from
              inside the constructor because you cannot call shared_from_this
              from inside constructors.
    */
    PeerImp (
        beast::IP::Endpoint remoteAddress,
        boost::asio::io_service& io_service,
        OverlayImpl& overlay,
        Resource::Manager& resourceManager,
        PeerFinder::Manager& peerFinder,
        PeerFinder::Slot::ptr const& slot,
        boost::asio::ssl::context& ssl_context,
        MultiSocket::Flag flags)
            : m_owned_socket (io_service)
            , m_journal (LogPartition::getJournal <Peer> ())
            , m_shortId (0)
            , m_remoteAddress (remoteAddress)
            , m_resourceManager (resourceManager)
            , m_peerFinder (peerFinder)
            , m_overlay (overlay)
            , m_inbound (false)
            , m_socket (MultiSocket::New (
                io_service, ssl_context, flags.asBits ()))
            , m_strand (io_service)
            , m_state (stateConnecting)
            , m_detaching (false)
            , m_clusterNode (false)
            , m_minLedger (0)
            , m_maxLedger (0)
            , m_timer (io_service)
            , m_slot (slot)
            , m_was_canceled (false)
            , message_stream_(*this)
    {
    }

    virtual
    ~PeerImp ()
    {
        m_overlay.remove (m_slot);
    }

    PeerImp (PeerImp const&) = delete;
    PeerImp& operator= (PeerImp const&) = delete;

    MultiSocket& getStream ()
    {
        return *m_socket;
    }

    static char const* getCountedObjectName () { return "Peer"; }

    void getLedger (protocol::TMGetLedger& packet);

    //
    // i/o
    //

    void
    async_read_some();

    void
    on_read_some (error_code ec, std::size_t bytes_transferred);

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
    void detach (const char* rsn, bool graceful = true)
    {
        if (! m_strand.running_in_this_thread ())
        {
            m_strand.post (std::bind (&PeerImp::detach,
                shared_from_this (), rsn, graceful));
            return;
        }

        if (!m_detaching)
        {
            // NIKB TODO No - a race is NOT ok. This needs to be fixed
            //           to have PeerFinder work reliably.
            m_detaching  = true; // Race is ok.

            if (m_was_canceled)
                m_peerFinder.on_cancel (m_slot);
            else
                m_peerFinder.on_closed (m_slot);

            if (m_state == stateActive)
                m_overlay.onPeerDisconnect (shared_from_this ());

            m_state = stateGracefulClose;

            if (m_clusterNode && m_journal.active(beast::Journal::Severity::kWarning))
                m_journal.warning << "Cluster peer " << m_nodeName <<
                                     " detached: " << rsn;

            mSendQ.clear ();

            (void) m_timer.cancel ();

            if (graceful)
            {
                m_socket->async_shutdown (
                    m_strand.wrap ( std::bind(
                        &PeerImp::handleShutdown,
                        std::static_pointer_cast <PeerImp> (shared_from_this ()),
                        beast::asio::placeholders::error)));
            }
            else
            {
                m_socket->cancel ();
            }

            // VFALCO TODO Stop doing this.
            if (m_nodePublicKey.isValid ())
                m_nodePublicKey.clear ();       // Be idempotent.
        }
    }

    /** Close the connection. */
    void close (bool graceful)
    {
        m_was_canceled = true;
        detach ("stop", graceful);
    }

    /** Outbound connection attempt has completed (not necessarily successfully)

        The connection may fail for a number of reasons. Perhaps we do not have
        a route to the remote endpoint, or there is no server listening at that
        address.

        If the connection succeeded, we transition to the `stateConnected` state
        and move on.

        If the connection failed, we simply disconnect.

        @param ec indicates success or an error code.
    */
    void onConnect (boost::system::error_code ec)
    {
        if (m_detaching)
            return;

        NativeSocketType::endpoint_type local_endpoint;

        if (! ec)
            local_endpoint = m_socket->this_layer <
                NativeSocketType> ().local_endpoint (ec);

        if (ec)
        {
            // VFALCO NOTE This log statement looks like ass
            m_journal.info <<
                "Connect to " << m_remoteAddress <<
                " failed: " << ec.message();
            // This should end up calling onPeerClosed()
            detach ("hc");
            return;
        }

        bassert (m_state == stateConnecting);
        m_state = stateConnected;

        m_peerFinder.on_connected (m_slot,
            beast::IPAddressConversion::from_asio (local_endpoint));

        m_socket->set_verify_mode (boost::asio::ssl::verify_none);
        m_socket->async_handshake (
            boost::asio::ssl::stream_base::client,
            m_strand.wrap (std::bind (&PeerImp::handleStart,
                std::static_pointer_cast <PeerImp> (shared_from_this ()),
                    beast::asio::placeholders::error)));
    }

    /** Indicates that the peer must be activated.
        A peer is activated after the handshake is completed and if it is not
        a second connection from a peer that we already have. Once activated
        the peer transitions to `stateActive` and begins operating.
    */
    void activate ()
    {
        bassert (m_state == stateHandshaked);
        m_state = stateActive;
        bassert(m_shortId == 0);
        m_shortId = m_overlay.next_id();
        m_overlay.onPeerActivated(shared_from_this ());
    }

    void start ()
    {
        if (m_inbound)
            accept ();
        else
            connect ();
    }

    //--------------------------------------------------------------------------
    std::string getClusterNodeName() const
    {
        return m_nodeName;
    }

    //--------------------------------------------------------------------------

    void
    send (Message::pointer const& m) override
    {
        if (m)
        {
            if (m_strand.running_in_this_thread())
            {
                if (mSendingPacket)
                    mSendQ.push_back (m);
                else
                    sendForce (m);
            }
            else
            {
                m_strand.post (std::bind (&PeerImp::send, shared_from_this(), m));
            }

        }
    }

    void sendGetPeers ()
    {
        // Ask peer for known other peers.
        protocol::TMGetPeers msg;

        msg.set_doweneedthis (1);

        Message::pointer packet = std::make_shared<Message> (
            msg, protocol::mtGET_PEERS);

        send (packet);
    }

    void charge (Resource::Charge const& fee)
    {
         if ((m_usage.charge (fee) == Resource::drop) && m_usage.disconnect ())
             detach ("resource");
    }

    static void charge (std::weak_ptr <Peer>& peer, Resource::Charge const& fee)
    {
        Peer::ptr p (peer.lock());

        if (p != nullptr)
            p->charge (fee);
    }

    Json::Value json ()
    {
        Json::Value ret (Json::objectValue);

        ret["public_key"]   = m_nodePublicKey.ToString ();
        ret["address"]      = m_remoteAddress.to_string();

        if (m_inbound)
            ret["inbound"] = true;

        if (m_clusterNode)
        {
            ret["cluster"] = true;

            if (!m_nodeName.empty ())
                ret["name"] = m_nodeName;
        }

        if (mHello.has_fullversion ())
            ret["version"] = mHello.fullversion ();

        if (mHello.has_protoversion () &&
                (mHello.protoversion () != BuildInfo::getCurrentProtocol().toPacked ()))
        {
            ret["protocol"] = BuildInfo::Protocol (mHello.protoversion ()).toStdString ();
        }

        std::uint32_t minSeq, maxSeq;
        ledgerRange(minSeq, maxSeq);

        if ((minSeq != 0) || (maxSeq != 0))
            ret["complete_ledgers"] = boost::lexical_cast<std::string>(minSeq) + " - " +
                                      boost::lexical_cast<std::string>(maxSeq);

        if (!!m_closedLedgerHash)
            ret["ledger"] = to_string (m_closedLedgerHash);

        if (mLastStatus.has_newstatus ())
        {
            switch (mLastStatus.newstatus ())
            {
            case protocol::nsCONNECTING:
                ret["status"] = "connecting";
                break;

            case protocol::nsCONNECTED:
                ret["status"] = "connected";
                break;

            case protocol::nsMONITORING:
                ret["status"] = "monitoring";
                break;

            case protocol::nsVALIDATING:
                ret["status"] = "validating";
                break;

            case protocol::nsSHUTTING:
                ret["status"] = "shutting";
                break;

            default:
                // FIXME: do we really want this?
                m_journal.warning << "Unknown status: " << mLastStatus.newstatus ();
            }
        }

        return ret;
    }

    bool isInCluster () const
    {
        return m_clusterNode;
    }

    uint256 const& getClosedLedgerHash () const
    {
        return m_closedLedgerHash;
    }

    bool hasLedger (uint256 const& hash, std::uint32_t seq) const
    {
        std::lock_guard<std::mutex> sl(m_recentLock);

        if ((seq != 0) && (seq >= m_minLedger) && (seq <= m_maxLedger))
            return true;

        BOOST_FOREACH (uint256 const & ledger, m_recentLedgers)
        {
            if (ledger == hash)
                return true;
        }

        return false;
    }

    void ledgerRange (std::uint32_t& minSeq, std::uint32_t& maxSeq) const
    {
        std::lock_guard<std::mutex> sl(m_recentLock);

        minSeq = m_minLedger;
        maxSeq = m_maxLedger;
    }

    bool hasTxSet (uint256 const& hash) const
    {
        std::lock_guard<std::mutex> sl(m_recentLock);
        BOOST_FOREACH (uint256 const & set, m_recentTxSets)

        if (set == hash)
            return true;

        return false;
    }

    Peer::ShortId getShortId () const
    {
        return m_shortId;
    }

    const RippleAddress& getNodePublic () const
    {
        return m_nodePublicKey;
    }

    void cycleStatus ()
    {
        m_previousLedgerHash = m_closedLedgerHash;
        m_closedLedgerHash.zero ();
    }

    bool supportsVersion (int version)
    {
        return mHello.has_protoversion () && (mHello.protoversion () >= version);
    }

    bool hasRange (std::uint32_t uMin, std::uint32_t uMax)
    {
        return (uMin >= m_minLedger) && (uMax <= m_maxLedger);
    }

    beast::IP::Endpoint getRemoteAddress() const
    {
        return m_remoteAddress;
    }

private:
    void handleShutdown (boost::system::error_code const& ec)
    {
        if (m_detaching)
            return;

        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.info << "Shutdown: " << ec.message ();
            detach ("hsd");
            return;
        }
    }

    void handleWrite (boost::system::error_code const& ec, size_t bytes)
    {
        if (m_detaching)
            return;

        // Call on IO strand

        mSendingPacket.reset ();

        if (ec == boost::asio::error::operation_aborted)
            return;

        if (m_detaching)
            return;

        if (ec)
        {
            m_journal.info << "Write: " << ec.message ();
            detach ("hw");
            return;
        }

        if (!mSendQ.empty ())
        {
            Message::pointer packet = mSendQ.front ();

            if (packet)
            {
                sendForce (packet);
                mSendQ.pop_front ();
            }
        }
    }

    // We have an encrypted connection to the peer.
    // Have it say who it is so we know to avoid redundant connections.
    // Establish that it really who we are talking to by having it sign a
    // connection detail. Also need to establish no man in the middle attack
    // is in progress.
    void handleStart (boost::system::error_code const& ec)
    {
        if (m_detaching)
            return;

        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.info << "Handshake: " << ec.message ();
            detach ("hs");
            return;
        }

        if (m_inbound)
            m_usage = m_resourceManager.newInboundEndpoint (m_remoteAddress);
        else
            m_usage = m_resourceManager.newOutboundEndpoint (m_remoteAddress);

        if (m_usage.disconnect ())
        {
            detach ("resource");
            return;
        }

        if(!sendHello ())
        {
            m_journal.error << "Unable to send HELLO to " << m_remoteAddress;
            detach ("hello");
            return;
        }

        async_read_some();
    }

    void handleVerifyTimer (boost::system::error_code const& ec)
    {
        if (m_detaching)
            return;

        if (ec == boost::asio::error::operation_aborted)
        {
            // Timer canceled because deadline no longer needed.
        }
        else if (ec)
        {
            m_journal.info << "Peer verify timer error";
        }
        else
        {
            //  m_journal.info << "Verify: Peer failed to verify in time.";

            detach ("hvt");
        }
    }

    void sendForce (const Message::pointer& packet)
    {
        // must be on IO strand
        if (!m_detaching)
        {
            mSendingPacket = packet;

            boost::asio::async_write (getStream (),
                boost::asio::buffer (packet->getBuffer ()),
                m_strand.wrap (std::bind (
                    &PeerImp::handleWrite,
                    std::static_pointer_cast <PeerImp> (shared_from_this ()),
                    beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
        }
    }

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
    bool hashLatestFinishedMessage (const SSL *sslSession, unsigned char *hash,
        size_t (*getFinishedMessage)(const SSL *, void *buf, size_t))
    {
        unsigned char buf[1024];

        // Get our finished message and hash it.
        std::memset(hash, 0, 64);

        size_t len = getFinishedMessage (sslSession, buf, sizeof (buf));

        if(len < sslMinimumFinishedLength)
            return false;

        SHA512 (buf, len, hash);

        return true;
    }

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
    bool calculateSessionCookie ()
    {
        SSL* ssl = m_socket->ssl_handle ();

        if (!ssl)
        {
            m_journal.error << "Cookie generation: No underlying connection";
            return false;
        }

        unsigned char sha1[64];
        unsigned char sha2[64];

        if (!hashLatestFinishedMessage(ssl, sha1, SSL_get_finished))
        {
            m_journal.error << "Cookie generation: local setup not complete";
            return false;
        }

        if (!hashLatestFinishedMessage(ssl, sha2, SSL_get_peer_finished))
        {
            m_journal.error << "Cookie generation: peer setup not complete";
            return false;
        }

        // If both messages hash to the same value (i.e. match) something is
        // wrong. This would cause the resulting cookie to be 0.
        if (memcmp (sha1, sha2, sizeof (sha1)) == 0)
        {
            m_journal.error << "Cookie generation: identical finished messages";
            return false;
        }

        for (size_t i = 0; i < sizeof (sha1); ++i)
            sha1[i] ^= sha2[i];

        // Finally, derive the actual cookie for the values that we have
        // calculated.
        m_secureCookie = Serializer::getSHA512Half (sha1, sizeof(sha1));

        return true;
    }

    /** Perform a secure handshake with the peer at the other end.

        If this function returns false then we cannot guarantee that there
        is no active man-in-the-middle attack taking place and the link
        MUST be disconnected.

        @return true if successful, false otherwise.
    */
    bool sendHello ()
    {
        if (!calculateSessionCookie())
            return false;

        Blob vchSig;
        getApp().getLocalCredentials ().getNodePrivate ().signNodePrivate (m_secureCookie, vchSig);

        protocol::TMHello h;

        h.set_protoversion (BuildInfo::getCurrentProtocol().toPacked ());
        h.set_protoversionmin (BuildInfo::getMinimumProtocol().toPacked ());
        h.set_fullversion (BuildInfo::getFullVersionString ());
        h.set_nettime (getApp().getOPs ().getNetworkTimeNC ());
        h.set_nodepublic (getApp().getLocalCredentials ().getNodePublic ().humanNodePublic ());
        h.set_nodeproof (&vchSig[0], vchSig.size ());
        h.set_ipv4port (getConfig ().peerListeningPort);
        h.set_testnet (false);

        // We always advertise ourselves as private in the HELLO message. This
        // suppresses the old peer advertising code and allows PeerFinder to
        // take over the functionality.
        h.set_nodeprivate (true);

        Ledger::pointer closedLedger = getApp().getLedgerMaster ().getClosedLedger ();

        if (closedLedger && closedLedger->isClosed ())
        {
            uint256 hash = closedLedger->getHash ();
            h.set_ledgerclosed (hash.begin (), hash.size ());
            hash = closedLedger->getParentHash ();
            h.set_ledgerprevious (hash.begin (), hash.size ());
        }

        Message::pointer packet = std::make_shared<Message> (
            h, protocol::mtHELLO);
        send (packet);

        return true;
    }

    void addLedger (uint256 const& hash)
    {
        std::lock_guard<std::mutex> sl(m_recentLock);
        BOOST_FOREACH (uint256 const & ledger, m_recentLedgers)

        if (ledger == hash)
            return;

        if (m_recentLedgers.size () == 128)
            m_recentLedgers.pop_front ();

        m_recentLedgers.push_back (hash);
    }

    void addTxSet (uint256 const& hash)
    {
        std::lock_guard<std::mutex> sl(m_recentLock);

        if(std::find (m_recentTxSets.begin (), m_recentTxSets.end (), hash) != m_recentTxSets.end ())
        	return;

        if (m_recentTxSets.size () == 128)
            m_recentTxSets.pop_front ();

        m_recentTxSets.push_back (hash);
    }

    void doFetchPack (const std::shared_ptr<protocol::TMGetObjectByHash>& packet)
    {
        // VFALCO TODO Invert this dependency using an observer and shared state object.
        // Don't queue fetch pack jobs if we're under load or we already have
        // some queued.
        if (getApp().getFeeTrack ().isLoadedLocal () ||
            (getApp().getLedgerMaster().getValidatedLedgerAge() > 40) ||
            (getApp().getJobQueue().getJobCount(jtPACK) > 10))
        {
            m_journal.info << "Too busy to make fetch pack";
            return;
        }

        if (packet->ledgerhash ().size () != 32)
        {
            m_journal.warning << "FetchPack hash size malformed";
            charge (Resource::feeInvalidRequest);
            return;
        }

        uint256 hash;
        memcpy (hash.begin (), packet->ledgerhash ().data (), 32);

        getApp().getJobQueue ().addJob (jtPACK, "MakeFetchPack",
            std::bind (&NetworkOPs::makeFetchPack, &getApp().getOPs (), std::placeholders::_1,
                std::weak_ptr<Peer> (shared_from_this ()), packet,
                    hash, UptimeTimer::getInstance ().getElapsedSeconds ()));
    }

    void doProofOfWork (Job&, std::weak_ptr <Peer> peer, ProofOfWork::pointer pow)
    {
        if (peer.expired ())
            return;

        uint256 solution = pow->solve ();

        if (solution.isZero ())
        {
             m_journal.warning << "Failed to solve proof of work";
        }
        else
        {
            Peer::ptr pptr (peer.lock ());

            if (pptr)
            {
                protocol::TMProofWork reply;
                reply.set_token (pow->getToken ());
                reply.set_response (solution.begin (), solution.size ());
                pptr->send (std::make_shared<Message> (reply, protocol::mtPROOFOFWORK));
            }
            else
            {
                // WRITEME: Save solved proof of work for new connection
            }
        }
    }

    static void checkTransaction (Job&, int flags, SerializedTransaction::pointer stx, std::weak_ptr<Peer> peer)
    {
    #ifndef TRUST_NETWORK
        try
        {
    #endif

            if (stx->isFieldPresent(sfLastLedgerSequence) &&
                (stx->getFieldU32 (sfLastLedgerSequence) <
                getApp().getLedgerMaster().getValidLedgerIndex()))
	    { // Transaction has expired
                getApp().getHashRouter().setFlag(stx->getTransactionID(), SF_BAD);
                charge (peer, Resource::feeUnwantedData);
                return;
            }

            bool const needCheck = !(flags & SF_SIGGOOD);
            Transaction::pointer tx =
                std::make_shared<Transaction> (stx, needCheck);

            if (tx->getStatus () == INVALID)
            {
                getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
                charge (peer, Resource::feeInvalidSignature);
                return;
            }
            else
                getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_SIGGOOD);

            bool const trusted (flags & SF_TRUSTED);
            getApp().getOPs ().processTransaction (tx, trusted, false, false);

    #ifndef TRUST_NETWORK
        }
        catch (...)
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            charge (peer, Resource::feeInvalidRequest);
        }

    #endif
    }

    // Called from our JobQueue
    static void checkPropose (Job& job, Overlay* pPeers, std::shared_ptr<protocol::TMProposeSet> packet,
                              LedgerProposal::pointer proposal, uint256 consensusLCL, RippleAddress nodePublic,
                              std::weak_ptr<Peer> peer, bool fromCluster)
    {
        bool sigGood = false;
        bool isTrusted = (job.getType () == jtPROPOSAL_t);

        WriteLog (lsTRACE, Peer)  << "Checking " <<
                                     (isTrusted ? "trusted" : "UNTRUSTED") <<
                                     " proposal";

        assert (packet);
        protocol::TMProposeSet& set = *packet;

        uint256 prevLedger;

        if (set.has_previousledger ())
        {
            // proposal includes a previous ledger
            WriteLog(lsTRACE, Peer) << "proposal with previous ledger";
            memcpy (prevLedger.begin (), set.previousledger ().data (), 256 / 8);

            if (!fromCluster && !proposal->checkSign (set.signature ()))
            {
                Peer::ptr p = peer.lock ();
                WriteLog(lsWARNING, Peer) << "proposal with previous ledger fails sig check: " <<
                                             *p;
                charge (peer, Resource::feeInvalidSignature);
                return;
            }
            else
                sigGood = true;
        }
        else
        {
            if (consensusLCL.isNonZero () && proposal->checkSign (set.signature ()))
            {
                prevLedger = consensusLCL;
                sigGood = true;
            }
            else
            {
                // Could be mismatched prev ledger
                WriteLog(lsWARNING, Peer) << "Ledger proposal fails signature check";
                proposal->setSignature (set.signature ());
            }
        }

        if (isTrusted)
        {
            getApp().getOPs ().processTrustedProposal (proposal, packet, nodePublic, prevLedger, sigGood);
        }
        else if (sigGood && (prevLedger == consensusLCL))
        {
            // relay untrusted proposal
            WriteLog(lsTRACE, Peer) << "relaying UNTRUSTED proposal";
            std::set<Peer::ShortId> peers;

            if (getApp().getHashRouter ().swapSet (
                proposal->getSuppressionID (), peers, SF_RELAYED))
            {
                pPeers->foreach (send_if_not (
                    std::make_shared<Message> (set, protocol::mtPROPOSE_LEDGER),
                    peer_in_set(peers)));
	    }
        }
        else
        {
            WriteLog(lsDEBUG, Peer) << "Not relaying UNTRUSTED proposal";
        }
    }

    static void checkValidation (Job&, Overlay* pPeers, SerializedValidation::pointer val, bool isTrusted, bool isCluster,
                                 std::shared_ptr<protocol::TMValidation> packet, std::weak_ptr<Peer> peer)
    {
    #ifndef TRUST_NETWORK

        try
    #endif
        {
            uint256 signingHash = val->getSigningHash();
            if (!isCluster && !val->isValid (signingHash))
            {
                WriteLog(lsWARNING, Peer) << "Validation is invalid";
                charge (peer, Resource::feeInvalidRequest);
                return;
            }

            std::string source;
            Peer::ptr lp = peer.lock ();

            if (lp)
                source = to_string(*lp);
            else
                source = "unknown";

            std::set<Peer::ShortId> peers;

            //----------------------------------------------------------------------
            //
            {
                SerializedValidation const& sv (*val);
                Validators::ReceivedValidation rv;
                rv.ledgerHash = sv.getLedgerHash ();
                rv.publicKey = sv.getSignerPublic();
                getApp ().getValidators ().receiveValidation (rv);
            }
            //
            //----------------------------------------------------------------------

            if (getApp().getOPs ().recvValidation (val, source) &&
                    getApp().getHashRouter ().swapSet (signingHash, peers, SF_RELAYED))
            {
                pPeers->foreach (send_if_not (
                    std::make_shared<Message> (*packet, protocol::mtVALIDATION),
                    peer_in_set(peers)));
            }
        }

    #ifndef TRUST_NETWORK
        catch (...)
        {
            WriteLog(lsTRACE, Peer) << "Exception processing validation";
            charge (peer, Resource::feeInvalidRequest);
        }
    #endif
    }
};

//------------------------------------------------------------------------------

const boost::posix_time::seconds PeerImp::nodeVerifySeconds (15);

//------------------------------------------------------------------------------

// to_string should not be used we should just use lexical_cast maybe

inline std::string to_string (PeerImp const& peer)
{
    if (peer.isInCluster())
        return peer.getClusterNodeName();

    return peer.getRemoteAddress().to_string();
}

inline std::string to_string (PeerImp const* peer)
{
    return to_string (*peer);
}

inline std::ostream& operator<< (std::ostream& os, PeerImp const& peer)
{
    os << to_string (peer);

    return os;
}

inline std::ostream& operator<< (std::ostream& os, PeerImp const* peer)
{
    os << to_string (peer);

    return os;
}

//------------------------------------------------------------------------------

inline std::string to_string (Peer const& peer)
{
    if (peer.isInCluster())
        return peer.getClusterNodeName();

    return peer.getRemoteAddress().to_string();
}

inline std::string to_string (Peer const* peer)
{
    return to_string (*peer);
}

inline std::ostream& operator<< (std::ostream& os, Peer const& peer)
{
    os << to_string (peer);

    return os;
}

inline std::ostream& operator<< (std::ostream& os, Peer const* peer)
{
    os << to_string (peer);

    return os;
}

}

#endif
