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

#include "../api/predicates.h"

#include "../../ripple/common/MultiSocket.h"
#include "../../ripple_data/protocol/Protocol.h"
#include "../ripple/validators/ripple_validators.h"
#include "../ripple/peerfinder/ripple_peerfinder.h"
#include "../ripple_app/misc/ProofOfWork.h"
#include "../ripple_app/misc/ProofOfWorkFactory.h"

// VFALCO This is unfortunate. Comment this out and
//        just include what is needed.
#include "../ripple_app/ripple_app.h"

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
    , public boost::enable_shared_from_this <PeerImp>
    , private beast::LeakChecked <Peer>
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
            m_strand.wrap (boost::bind (
                &PeerImp::handleStart,
                boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                boost::asio::placeholders::error)));
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

        m_timer.async_wait (m_strand.wrap (boost::bind (
            &PeerImp::handleVerifyTimer,
            shared_from_this (), boost::asio::placeholders::error)));

        if (err)
        {
            m_journal.error << "Failed to set verify timer.";
            detach ("c2");
            return;
        }

        m_socket->next_layer <NativeSocketType>().async_connect (
            beast::IPAddressConversion::to_asio_endpoint (m_remoteAddress),
                m_strand.wrap (boost::bind (&PeerImp::onConnect,
                    shared_from_this (), boost::asio::placeholders::error)));
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

    typedef boost::shared_ptr <PeerImp> ptr;

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
    mutable boost::mutex  m_recentLock;

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
            m_strand.post (BIND_TYPE (&PeerImp::detach,
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
                    m_strand.wrap ( boost::bind(
                        &PeerImp::handleShutdown,
                        boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                        boost::asio::placeholders::error)));
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
            m_strand.wrap (boost::bind (&PeerImp::handleStart,
                boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                    boost::asio::placeholders::error)));
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

    void sendPacket (const Message::pointer& packet, bool onStrand)
    {
        if (packet)
        {
            if (!onStrand)
            {
                m_strand.post (BIND_TYPE (
                    &Peer::sendPacket, shared_from_this (), packet, true));
                return;
            }

            if (mSendingPacket)
            {
                mSendQ.push_back (packet);
            }
            else
            {
                sendPacketForce (packet);
            }
        }
    }

    void sendGetPeers ()
    {
        // Ask peer for known other peers.
        protocol::TMGetPeers msg;

        msg.set_doweneedthis (1);

        Message::pointer packet = boost::make_shared<Message> (
            msg, protocol::mtGET_PEERS);

        sendPacket (packet, true);
    }

    void charge (Resource::Charge const& fee)
    {
         if ((m_usage.charge (fee) == Resource::drop) && m_usage.disconnect ())
             detach ("resource");
    }

    static void charge (boost::weak_ptr <Peer>& peer, Resource::Charge const& fee)
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
        boost::mutex::scoped_lock sl(m_recentLock);

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
        boost::mutex::scoped_lock sl(m_recentLock);

        minSeq = m_minLedger;
        maxSeq = m_maxLedger;
    }

    bool hasTxSet (uint256 const& hash) const
    {
        boost::mutex::scoped_lock sl(m_recentLock);
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
                sendPacketForce (packet);
                mSendQ.pop_front ();
            }
        }
    }

    void handleReadHeader (boost::system::error_code const& ec,
                           std::size_t bytes)
    {
        if (m_detaching)
            return;

        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.info << "ReadHeader: " << ec.message ();
            detach ("hrh1");
            return;
        }

        unsigned msg_len = Message::getLength (m_readBuffer);

        // WRITEME: Compare to maximum message length, abort if too large
        if ((msg_len > (32 * 1024 * 1024)) || (msg_len == 0))
        {
            detach ("hrh2");
            return;
        }

        startReadBody (msg_len);
    }

    void handleReadBody (boost::system::error_code const& ec,
                         std::size_t bytes)
    {
        if (m_detaching)
            return;

        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.info << "ReadBody: " << ec.message ();

            {
            Application::ScopedLockType lock (getApp ().getMasterLock ());
            detach ("hrb");
            }
            
            return;
        }

        processReadBuffer ();
        startReadHeader ();
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

        startReadHeader ();
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

    void processReadBuffer ()
    {
        // must not hold peer lock
        int type = Message::getType (m_readBuffer);

        LoadEvent::autoptr event (
            getApp().getJobQueue ().getLoadEventAP (jtPEER, "Peer::read"));

        {
            // An mtHELLO message must be the first message receiced by a peer
            // and it must be received *exactly* once during a connection; any
            // other scenario constitutes a protocol violation.

            if ((m_state == stateHandshaked) && (type == protocol::mtHELLO))
            {
                m_journal.warning << "Protocol: HELLO expected!";
                detach ("prb-hello-expected");
                return;
            }

            if ((m_state == stateActive) && (type == protocol::mtHELLO))
            {
                m_journal.warning << "Protocol: HELLO unexpected!";
                detach ("prb-hello-unexpected");
                return;
            }

            size_t msgLen (m_readBuffer.size () - Message::kHeaderBytes);

            switch (type)
            {
            case protocol::mtHELLO:
            {
                event->reName ("Peer::hello");
                protocol::TMHello msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvHello (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtCLUSTER:
            {
                event->reName ("Peer::cluster");
                protocol::TMCluster msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvCluster (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtERROR_MSG:
            {
                event->reName ("Peer::errormessage");
                protocol::TMErrorMsg msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvErrorMessage (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtPING:
            {
                event->reName ("Peer::ping");
                protocol::TMPing msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvPing (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtGET_CONTACTS:
            {
                event->reName ("Peer::getcontacts");
                protocol::TMGetContacts msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvGetContacts (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtCONTACT:
            {
                event->reName ("Peer::contact");
                protocol::TMContact msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvContact (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtGET_PEERS:
            {
                event->reName ("Peer::getpeers");
                protocol::TMGetPeers msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvGetPeers (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtPEERS:
            {
                event->reName ("Peer::peers");
                protocol::TMPeers msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvPeers (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtENDPOINTS:
            {
                event->reName ("Peer::endpoints");
                protocol::TMEndpoints msg;

                if(msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                       msgLen))
                    recvEndpoints (msg);
                else
                    m_journal.warning << "parse error: " << type;;
            }
                break;

            case protocol::mtSEARCH_TRANSACTION:
            {
                event->reName ("Peer::searchtransaction");
                protocol::TMSearchTransaction msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvSearchTransaction (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtGET_ACCOUNT:
            {
                event->reName ("Peer::getaccount");
                protocol::TMGetAccount msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvGetAccount (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtACCOUNT:
            {
                event->reName ("Peer::account");
                protocol::TMAccount msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvAccount (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtTRANSACTION:
            {
                event->reName ("Peer::transaction");
                protocol::TMTransaction msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvTransaction (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtSTATUS_CHANGE:
            {
                event->reName ("Peer::statuschange");
                protocol::TMStatusChange msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvStatus (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtPROPOSE_LEDGER:
            {
                event->reName ("Peer::propose");
                boost::shared_ptr<protocol::TMProposeSet> msg (
                	boost::make_shared<protocol::TMProposeSet> ());

                if (msg->ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                         msgLen))
                    recvPropose (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtGET_LEDGER:
            {
                event->reName ("Peer::getledger");
                boost::shared_ptr<protocol::TMGetLedger> msg ( 
                    boost::make_shared<protocol::TMGetLedger> ());

                if (msg->ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvGetLedger (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtLEDGER_DATA:
            {
                event->reName ("Peer::ledgerdata");
                boost::shared_ptr<protocol::TMLedgerData> msg (
                	boost::make_shared<protocol::TMLedgerData> ());

                if (msg->ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                         msgLen))
                    recvLedger (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtHAVE_SET:
            {
                event->reName ("Peer::haveset");
                protocol::TMHaveTransactionSet msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvHaveTxSet (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtVALIDATION:
            {
                event->reName ("Peer::validation");
                boost::shared_ptr<protocol::TMValidation> msg (
                	boost::make_shared<protocol::TMValidation> ());

                if (msg->ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                         msgLen))
                    recvValidation (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;
#if 0

            case protocol::mtGET_VALIDATION:
            {
                protocol::TM msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes], msgLen))
                    recv (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

#endif

            case protocol::mtGET_OBJECTS:
            {
                event->reName ("Peer::getobjects");
                boost::shared_ptr<protocol::TMGetObjectByHash> msg =
                    boost::make_shared<protocol::TMGetObjectByHash> ();

                if (msg->ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                         msgLen))
                    recvGetObjectByHash (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;

            case protocol::mtPROOFOFWORK:
            {
                event->reName ("Peer::proofofwork");
                protocol::TMProofWork msg;

                if (msg.ParseFromArray (&m_readBuffer[Message::kHeaderBytes],
                                        msgLen))
                    recvProofWork (msg);
                else
                    m_journal.warning << "parse error: " << type;
            }
                break;


            default:
                event->reName ("Peer::unknown");
                m_journal.warning << "Unknown Msg: " << type;
                m_journal.warning << strHex (&m_readBuffer[0], m_readBuffer.size ());
            }
        }
    }

    void startReadHeader ()
    {
        if (!m_detaching)
        {
            m_readBuffer.clear ();
            m_readBuffer.resize (Message::kHeaderBytes);

            boost::asio::async_read (getStream (),
                boost::asio::buffer (m_readBuffer),
                m_strand.wrap (boost::bind (&PeerImp::handleReadHeader,
                    boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred)));
        }
    }

    void startReadBody (unsigned msg_len)
    {
        // The first Message::kHeaderBytes bytes of m_readbuf already
        // contains the header. Expand it to fit in the body as well, and
        // start async read into the body.

        if (!m_detaching)
        {
            m_readBuffer.resize (Message::kHeaderBytes + msg_len);

            boost::asio::async_read (getStream (),
                boost::asio::buffer (
                    &m_readBuffer [Message::kHeaderBytes], msg_len),
                m_strand.wrap (boost::bind (
                    &PeerImp::handleReadBody,
                    boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred)));
        }
    }

    void sendPacketForce (const Message::pointer& packet)
    {
        // must be on IO strand
        if (!m_detaching)
        {
            mSendingPacket = packet;

            boost::asio::async_write (getStream (),
                boost::asio::buffer (packet->getBuffer ()),
                m_strand.wrap (boost::bind (
                    &PeerImp::handleWrite,
                    boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred)));
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

        Message::pointer packet = boost::make_shared<Message> (
            h, protocol::mtHELLO);
        sendPacket (packet, true);

        return true;
    }

    void recvHello (protocol::TMHello& packet)
    {
        bool    bDetach = true;

        (void) m_timer.cancel ();

        std::uint32_t const ourTime (getApp().getOPs ().getNetworkTimeNC ());
        std::uint32_t const minTime (ourTime - clockToleranceDeltaSeconds);
        std::uint32_t const maxTime (ourTime + clockToleranceDeltaSeconds);

    #ifdef BEAST_DEBUG
        if (packet.has_nettime ())
        {
            std::int64_t to = ourTime;
            to -= packet.nettime ();
            m_journal.debug << "Connect: time offset " << to;
        }

    #endif

        BuildInfo::Protocol protocol (packet.protoversion());

        if (packet.has_nettime () &&
            ((packet.nettime () < minTime) || (packet.nettime () > maxTime)))
        {
            if (packet.nettime () > maxTime)
            {
                m_journal.info << "Hello: Clock for " << *this <<
                                  " is off by +" << packet.nettime () - ourTime;
            }
            else if (packet.nettime () < minTime)
            {
                m_journal.info << "Hello: Clock for " << *this <<
                                  " is off by -" << ourTime - packet.nettime ();
            }
        }
        else if (packet.protoversionmin () > BuildInfo::getCurrentProtocol().toPacked ())
        {
            std::string reqVersion (
                protocol.toStdString ());

            std::string curVersion (
                BuildInfo::getCurrentProtocol().toStdString ());

            m_journal.info << "Hello: Disconnect: Protocol mismatch [" <<
                              "Peer expects " << reqVersion <<
                              " and we run " << curVersion << "]";
        }
        else if (!m_nodePublicKey.setNodePublic (packet.nodepublic ()))
        {
            m_journal.info << "Hello: Disconnect: Bad node public key.";
        }
        else if (!m_nodePublicKey.verifyNodePublic (m_secureCookie, packet.nodeproof (), ECDSA::not_strict))
        {
            // Unable to verify they have private key for claimed public key.
            m_journal.info << "Hello: Disconnect: Failed to verify session.";
        }
        else
        {
            // Successful connection.
            m_journal.info << "Hello: Connect: " << m_nodePublicKey.humanNodePublic ();

            if ((protocol != BuildInfo::getCurrentProtocol()) &&
                m_journal.active(beast::Journal::Severity::kInfo))
            {
                m_journal.info << "Peer protocol: " << protocol.toStdString ();
            }

            mHello = packet;

            // Determine if this peer belongs to our cluster and get it's name
            m_clusterNode = getApp().getUNL().nodeInCluster (m_nodePublicKey, m_nodeName);

            if(m_clusterNode)
                m_journal.info << "Connected to cluster node " << m_nodeName;

            bassert (m_state == stateConnected);
            m_state = stateHandshaked;

            m_peerFinder.on_handshake (m_slot, RipplePublicKey(m_nodePublicKey),
                m_clusterNode);

            // XXX Set timer: connection is in grace period to be useful.
            // XXX Set timer: connection idle (idle may vary depending on connection type.)
            if ((mHello.has_ledgerclosed ()) && (mHello.ledgerclosed ().size () == (256 / 8)))
            {
                memcpy (m_closedLedgerHash.begin (), mHello.ledgerclosed ().data (), 256 / 8);

                if ((mHello.has_ledgerprevious ()) && (mHello.ledgerprevious ().size () == (256 / 8)))
                {
                    memcpy (m_previousLedgerHash.begin (), mHello.ledgerprevious ().data (), 256 / 8);
                    addLedger (m_previousLedgerHash);
                }
                else
                {
                    m_previousLedgerHash.zero ();
                }
            }

            bDetach = false;
        }

        if (bDetach)
        {
            m_nodePublicKey.clear ();
            detach ("recvh");
        }
        else
        {
            sendGetPeers ();
        }
    }

    void recvCluster (protocol::TMCluster& packet)
    {
        if (!m_clusterNode)
        {
            charge (Resource::feeUnwantedData);
            return;
        }

        for (int i = 0; i < packet.clusternodes().size(); ++i)
        {
            protocol::TMClusterNode const& node = packet.clusternodes(i);

            std::string name;
            if (node.has_nodename())
                name = node.nodename();
            ClusterNodeStatus s(name, node.nodeload(), node.reporttime());

            RippleAddress nodePub;
            nodePub.setNodePublic(node.publickey());

            getApp().getUNL().nodeUpdate(nodePub, s);
        }

        int loadSources = packet.loadsources().size();
        if (loadSources != 0)
        {
            Resource::Gossip gossip;
            gossip.items.reserve (loadSources);
            for (int i = 0; i < packet.loadsources().size(); ++i)
            {
                protocol::TMLoadSource const& node = packet.loadsources (i);
                Resource::Gossip::Item item;
                item.address = beast::IP::Endpoint::from_string (node.name());
                item.balance = node.cost();
                if (item.address != beast::IP::Endpoint())
                    gossip.items.push_back(item);
            }
            m_resourceManager.importConsumers (m_nodeName, gossip);
        }

        getApp().getFeeTrack().setClusterFee(getApp().getUNL().getClusterFee());
    }


    void recvTransaction (protocol::TMTransaction& packet)
    {

        Serializer s (packet.rawtransaction ());

    #ifndef TRUST_NETWORK
        try
        {
    #endif
            SerializerIterator sit (s);
            SerializedTransaction::pointer stx = boost::make_shared<SerializedTransaction> (boost::ref (sit));
            uint256 txID = stx->getTransactionID();

            int flags;

            if (! getApp().getHashRouter ().addSuppressionPeer (txID, m_shortId, flags))
            {
                // we have seen this transaction recently
                if (is_bit_set (flags, SF_BAD))
                {
                    charge (Resource::feeInvalidSignature);
                    return;
                }

                if (!is_bit_set (flags, SF_RETRY))
                    return;
            }

             m_journal.debug << "Got transaction from peer " << *this << ": " << txID;

            if (m_clusterNode)
                flags |= SF_TRUSTED | SF_SIGGOOD;

            if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
                m_journal.info << "Transaction queue is full";
            else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
                m_journal.trace << "No new transactions until synchronized";
            else
                getApp().getJobQueue ().addJob (jtTRANSACTION,
                    "recvTransaction->checkTransaction",
                    BIND_TYPE (
                        &PeerImp::checkTransaction, P_1, flags, stx,
                        boost::weak_ptr<Peer> (shared_from_this ())));

    #ifndef TRUST_NETWORK
        }
        catch (...)
        {
            m_journal.warning << "Transaction invalid: " <<
               s.getHex();
        }
    #endif
    }

    void recvValidation (const boost::shared_ptr<protocol::TMValidation>& packet)
    {
        std::uint32_t closeTime = getApp().getOPs().getCloseTimeNC();

        if (packet->validation ().size () < 50)
        {
            m_journal.warning << "Too small validation from peer";
            charge (Resource::feeInvalidRequest);
            return;
        }

    #ifndef TRUST_NETWORK

        try
    #endif
        {
            Serializer s (packet->validation ());
            SerializerIterator sit (s);
            SerializedValidation::pointer val = boost::make_shared<SerializedValidation> (boost::ref (sit), false);

            if (closeTime > (120 + val->getFieldU32(sfSigningTime)))
            {
                m_journal.trace << "Validation is more than two minutes old";
                charge (Resource::feeUnwantedData);
                return;
            }

            if (! getApp().getHashRouter ().addSuppressionPeer (s.getSHA512Half(), m_shortId))
            {
                m_journal.trace << "Validation is duplicate";
                return;
            }

            bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
            if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
            {
                getApp().getJobQueue ().addJob (
                    isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                    "recvValidation->checkValidation",
                    BIND_TYPE (
                        &PeerImp::checkValidation, P_1, &m_overlay, val,
                        isTrusted, m_clusterNode, packet,
                        boost::weak_ptr<Peer> (shared_from_this ())));
            }
            else
                m_journal.debug << "Dropping UNTRUSTED validation due to load";
        }

    #ifndef TRUST_NETWORK
        catch (...)
        {
             m_journal.warning << "Exception processing validation";
            charge (Resource::feeInvalidRequest);
        }

    #endif
    }

    void recvGetValidation (protocol::TMGetValidations& packet)
    {
    }

    void recvContact (protocol::TMContact& packet)
    {
    }

    void recvGetContacts (protocol::TMGetContacts& packet)
    {
    }

    // Return a list of your favorite people
    // TODO: filter out all the LAN peers
    void recvGetPeers (protocol::TMGetPeers& packet)
    {
#if 0
        protocol::TMPeers peers;

        // CODEME: This is deprecated because of PeerFinder, but populate the
        // response with some data here anyways, and send if non-empty.

        sendPacket (
            boost::make_shared<Message> (peers, protocol::mtPEERS),
            true);
#endif
    }

    // TODO: filter out all the LAN peers
    void recvPeers (protocol::TMPeers& packet)
    {
        std::vector <beast::IP::Endpoint> list;
        list.reserve (packet.nodes().size());
        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            in_addr addr;

            addr.s_addr = packet.nodes (i).ipv4 ();

            {
                beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
                beast::IP::Endpoint address (v4, packet.nodes (i).ipv4port ());
                list.push_back (address);
            }
        }

        if (! list.empty())
            m_peerFinder.on_legacy_endpoints (list);
    }

    void recvEndpoints (protocol::TMEndpoints& packet)
    {
        std::vector <PeerFinder::Endpoint> endpoints;

        endpoints.reserve (packet.endpoints().size());

        for (int i = 0; i < packet.endpoints ().size (); ++i)
        {
            PeerFinder::Endpoint endpoint;
            protocol::TMEndpoint const& tm (packet.endpoints(i));

            // hops
            endpoint.hops = tm.hops();

            // ipv4
            if (endpoint.hops > 0)
            {
                in_addr addr;
                addr.s_addr = tm.ipv4().ipv4();
                beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
                endpoint.address = beast::IP::Endpoint (v4, tm.ipv4().ipv4port ());
            }
            else
            {
                // This Endpoint describes the peer we are connected to.
                // We will take the remote address seen on the socket and
                // store that in the IP::Endpoint. If this is the first time,
                // then we'll verify that their listener can receive incoming
                // by performing a connectivity test.
                //
                endpoint.address = m_remoteAddress.at_port (
                    tm.ipv4().ipv4port ());
            }
        
            endpoints.push_back (endpoint);
        }

        if (! endpoints.empty())
            m_peerFinder.on_endpoints (m_slot, endpoints);
    }

    void recvGetObjectByHash (const boost::shared_ptr<protocol::TMGetObjectByHash>& ptr)
    {
        protocol::TMGetObjectByHash& packet = *ptr;

        if (packet.query ())
        {
            // this is a query
            if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            {
                doFetchPack (ptr);
                return;
            }

            protocol::TMGetObjectByHash reply;

            reply.set_query (false);

            if (packet.has_seq ())
                reply.set_seq (packet.seq ());

            reply.set_type (packet.type ());

            if (packet.has_ledgerhash ())
                reply.set_ledgerhash (packet.ledgerhash ());

            // This is a very minimal implementation
            for (int i = 0; i < packet.objects_size (); ++i)
            {
                uint256 hash;
                const protocol::TMIndexedObject& obj = packet.objects (i);

                if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
                {
                    memcpy (hash.begin (), obj.hash ().data (), 256 / 8);
                    NodeObject::pointer hObj = getApp().getNodeStore ().fetch (hash);

                    if (hObj)
                    {
                        protocol::TMIndexedObject& newObj = *reply.add_objects ();
                        newObj.set_hash (hash.begin (), hash.size ());
                        newObj.set_data (&hObj->getData ().front (), hObj->getData ().size ());

                        if (obj.has_nodeid ())
                            newObj.set_index (obj.nodeid ());

                        if (!reply.has_seq () && (hObj->getIndex () != 0))
                            reply.set_seq (hObj->getIndex ());
                    }
                }
            }

            m_journal.trace << "GetObjByHash had " << reply.objects_size () <<
                               " of " << packet.objects_size () <<
                               " for " << to_string (this);
            sendPacket (
                boost::make_shared<Message> (reply, protocol::mtGET_OBJECTS),
                true);
        }
        else
        {
            // this is a reply
            std::uint32_t pLSeq = 0;
            bool pLDo = true;
            bool progress = false;

            for (int i = 0; i < packet.objects_size (); ++i)
            {
                const protocol::TMIndexedObject& obj = packet.objects (i);

                if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
                {

                    if (obj.has_ledgerseq ())
                    {
                        if (obj.ledgerseq () != pLSeq)
                        {
                            if ((pLDo && (pLSeq != 0)) &&
                                m_journal.active(beast::Journal::Severity::kDebug))
                                m_journal.debug << "Received full fetch pack for " << pLSeq;

                            pLSeq = obj.ledgerseq ();
                            pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                            if (!pLDo)
                                 m_journal.debug << "Got pack for " << pLSeq << " too late";
                            else
                                progress = true;
                        }
                    }

                    if (pLDo)
                    {
                        uint256 hash;
                        memcpy (hash.begin (), obj.hash ().data (), 256 / 8);

                        boost::shared_ptr< Blob > data (
                            boost::make_shared< Blob > (
                                obj.data ().begin (), obj.data ().end ()));

                        getApp().getOPs ().addFetchPack (hash, data);
                    }
                }
            }

            if ((pLDo && (pLSeq != 0)) &&
                m_journal.active(beast::Journal::Severity::kDebug))
                m_journal.debug << "Received partial fetch pack for " << pLSeq;

            if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
                getApp().getOPs ().gotFetchPack (progress, pLSeq);
        }
    }

    void recvPing (protocol::TMPing& packet)
    {
        if (packet.type () == protocol::TMPing::ptPING)
        {
            packet.set_type (protocol::TMPing::ptPONG);
            sendPacket (boost::make_shared<Message> (packet, protocol::mtPING), true);
        }        
    }

    void recvErrorMessage (protocol::TMErrorMsg& packet)
    {
    }

    void recvSearchTransaction (protocol::TMSearchTransaction& packet)
    {
    }

    void recvGetAccount (protocol::TMGetAccount& packet)
    {
    }

    void recvAccount (protocol::TMAccount& packet)
    {
    }

    void recvGetLedger (boost::shared_ptr<protocol::TMGetLedger> const& packet)
    {
        getApp().getJobQueue().addJob (jtPACK, "recvGetLedger",
        	std::bind (&sGetLedger, boost::weak_ptr<PeerImp> (shared_from_this ()), packet));
    }

    /** A peer has sent us transaction set data */
    static void peerTXData (Job&,
        boost::weak_ptr <Peer> wPeer,
        uint256 const& hash,
        boost::shared_ptr <protocol::TMLedgerData> pPacket,
        beast::Journal journal)
    {
        boost::shared_ptr <Peer> peer = wPeer.lock ();
        if (!peer)
            return;

        protocol::TMLedgerData& packet = *pPacket;

        std::list<SHAMapNode> nodeIDs;
        std::list< Blob > nodeData;
        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata () || (node.nodeid ().size () != 33))
            {
                journal.warning << "LedgerData request with invalid node ID";
                peer->charge (Resource::feeInvalidRequest);
                return;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
        }

        SHAMapAddNode san;
        {
            Application::ScopedLockType lock (getApp ().getMasterLock ());

            san =  getApp().getOPs().gotTXData (peer, hash, nodeIDs, nodeData);
        }

        if (san.isInvalid ())
        {
            peer->charge (Resource::feeUnwantedData);
        }

    }

    void recvLedger (boost::shared_ptr<protocol::TMLedgerData> const& packet_ptr)
    {
        protocol::TMLedgerData& packet = *packet_ptr;

        if (packet.nodes ().size () <= 0)
        {
            m_journal.warning << "Ledger/TXset data with no nodes";
            return;
        }

        if (packet.has_requestcookie ())
        {
            Peer::ptr target = m_overlay.findPeerByShortID (packet.requestcookie ());

            if (target)
            {
                packet.clear_requestcookie ();
                target->sendPacket (boost::make_shared<Message> (packet, protocol::mtLEDGER_DATA), false);
            }
            else
            {
                m_journal.info << "Unable to route TX/ledger data reply";
                charge (Resource::feeUnwantedData);
            }

            return;
        }

        uint256 hash;

        if (packet.ledgerhash ().size () != 32)
        {
            m_journal.warning << "TX candidate reply with invalid hash size";
            charge (Resource::feeInvalidRequest);
            return;
        }

        memcpy (hash.begin (), packet.ledgerhash ().data (), 32);

        if (packet.type () == protocol::liTS_CANDIDATE)
        {
            // got data for a candidate transaction set

            getApp().getJobQueue().addJob (jtTXN_DATA, "recvPeerData",
                BIND_TYPE (&PeerImp::peerTXData, P_1,
                    boost::weak_ptr<Peer> (shared_from_this ()),
                        hash, packet_ptr, m_journal));

            return;
        }

        if (!getApp().getInboundLedgers ().gotLedgerData (hash, shared_from_this(), packet_ptr))
        {
            WriteLog (lsTRACE, Peer) << "Got data for unwanted ledger";
            charge (Resource::feeUnwantedData);
        }
    }

    void recvStatus (protocol::TMStatusChange& packet)
    {
        m_journal.trace << "Received status change from peer " <<
                           to_string (this);

        if (!packet.has_networktime ())
            packet.set_networktime (getApp().getOPs ().getNetworkTimeNC ());

        if (!mLastStatus.has_newstatus () || packet.has_newstatus ())
            mLastStatus = packet;
        else
        {
            // preserve old status
            protocol::NodeStatus status = mLastStatus.newstatus ();
            mLastStatus = packet;
            packet.set_newstatus (status);
        }

        if (packet.newevent () == protocol::neLOST_SYNC)
        {
            if (!m_closedLedgerHash.isZero ())
            {
                m_journal.trace << "peer has lost sync " << to_string (this);
                m_closedLedgerHash.zero ();
            }

            m_previousLedgerHash.zero ();
            return;
        }

        if (packet.has_ledgerhash () && (packet.ledgerhash ().size () == (256 / 8)))
        {
            // a peer has changed ledgers
            memcpy (m_closedLedgerHash.begin (), packet.ledgerhash ().data (), 256 / 8);
            addLedger (m_closedLedgerHash);
            m_journal.trace << "peer LCL is " << m_closedLedgerHash <<
                               " " << to_string (this);
        }
        else
        {
            m_journal.trace << "peer has no ledger hash" << to_string (this);
            m_closedLedgerHash.zero ();
        }

        if (packet.has_ledgerhashprevious () && packet.ledgerhashprevious ().size () == (256 / 8))
        {
            memcpy (m_previousLedgerHash.begin (), packet.ledgerhashprevious ().data (), 256 / 8);
            addLedger (m_previousLedgerHash);
        }
        else m_previousLedgerHash.zero ();

        if (packet.has_firstseq () && packet.has_lastseq())
        {
            m_minLedger = packet.firstseq ();
            m_maxLedger = packet.lastseq ();

            // Work around some servers that report sequences incorrectly
            if (m_minLedger == 0)
                m_maxLedger = 0;
            if (m_maxLedger == 0)
                m_minLedger = 0;
        }
    }

    void recvPropose (const boost::shared_ptr<protocol::TMProposeSet>& packet)
    {
        assert (packet);
        protocol::TMProposeSet& set = *packet;

        if ((set.closetime() + 180) < getApp().getOPs().getCloseTimeNC())
            return;

        if ((set.currenttxhash ().size () != 32) || (set.nodepubkey ().size () < 28) ||
                (set.signature ().size () < 56) || (set.nodepubkey ().size () > 128) || (set.signature ().size () > 128))
        {
            m_journal.warning << "Received proposal is malformed";
            charge (Resource::feeInvalidSignature);
            return;
        }

        if (set.has_previousledger () && (set.previousledger ().size () != 32))
        {
            m_journal.warning << "Received proposal is malformed";
            charge (Resource::feeInvalidRequest);
            return;
        }

        uint256 proposeHash, prevLedger;
        memcpy (proposeHash.begin (), set.currenttxhash ().data (), 32);

        if (set.has_previousledger ())
            memcpy (prevLedger.begin (), set.previousledger ().data (), 32);

        uint256 suppression = LedgerProposal::computeSuppressionID (proposeHash, prevLedger,
            set.proposeseq(), set.closetime (),
            Blob(set.nodepubkey ().begin (), set.nodepubkey ().end ()),
            Blob(set.signature ().begin (), set.signature ().end ()));

        if (! getApp().getHashRouter ().addSuppressionPeer (suppression, m_shortId))
        {
            m_journal.trace << "Received duplicate proposal from peer " << m_shortId;
            return;
        }

        RippleAddress signerPublic = RippleAddress::createNodePublic (strCopy (set.nodepubkey ()));

        if (signerPublic == getConfig ().VALIDATION_PUB)
        {
            m_journal.trace << "Received our own proposal from peer " << m_shortId;
            return;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
        if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
        {
             m_journal.debug << "Dropping UNTRUSTED proposal due to load";
            return;
        }

        m_journal.trace << "Received " << (isTrusted ? "trusted" : "UNTRUSTED") <<
                           " proposal from " << m_shortId;

        uint256 consensusLCL;
        
        {
            Application::ScopedLockType lock (getApp ().getMasterLock ());
            consensusLCL = getApp().getOPs ().getConsensusLCL ();
        }
        
        LedgerProposal::pointer proposal = boost::make_shared<LedgerProposal> (
            prevLedger.isNonZero () ? prevLedger : consensusLCL,
            set.proposeseq (), proposeHash, set.closetime (), signerPublic, suppression);

        getApp().getJobQueue ().addJob (isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
            "recvPropose->checkPropose", BIND_TYPE (
                &PeerImp::checkPropose, P_1, &m_overlay, packet, proposal, consensusLCL,
                m_nodePublicKey, boost::weak_ptr<Peer> (shared_from_this ()), m_clusterNode));
    }

    void recvHaveTxSet (protocol::TMHaveTransactionSet& packet)
    {
        uint256 hashes;

        if (packet.hash ().size () != (256 / 8))
        {
            charge (Resource::feeInvalidRequest);
            return;
        }

        uint256 hash;

        // VFALCO TODO There should be no use of memcpy() throughout the program.
        //        TODO Clean up this magic number
        //
        memcpy (hash.begin (), packet.hash ().data (), 32);

        if (packet.status () == protocol::tsHAVE)
            addTxSet (hash);

        {
            Application::ScopedLockType lock (getApp ().getMasterLock ());
            
            if (!getApp().getOPs ().hasTXSet (shared_from_this (), hash, packet.status ()))
                charge (Resource::feeUnwantedData);
        }
    }

    void recvProofWork (protocol::TMProofWork& packet)
    {
        if (packet.has_response ())
        {
            // this is an answer to a proof of work we requested
            if (packet.response ().size () != (256 / 8))
            {
                charge (Resource::feeInvalidRequest);
                return;
            }

            uint256 response;
            memcpy (response.begin (), packet.response ().data (), 256 / 8);

            // VFALCO TODO Use a dependency injection here
            PowResult r = getApp().getProofOfWorkFactory ().checkProof (packet.token (), response);

            if (r == powOK)
            {
                // credit peer
                // WRITEME
                return;
            }

            // return error message
            // WRITEME
            if (r != powTOOEASY)
            {
                charge (Resource::feeBadProofOfWork);
            }

            return;
        }

        if (packet.has_result ())
        {
            // this is a reply to a proof of work we sent
            // WRITEME
        }

        if (packet.has_target () && packet.has_challenge () && packet.has_iterations ())
        {
            // this is a challenge
            // WRITEME: Reject from inbound connections

            uint256 challenge, target;

            if ((packet.challenge ().size () != (256 / 8)) || (packet.target ().size () != (256 / 8)))
            {
                charge (Resource::feeInvalidRequest);
                return;
            }

            memcpy (challenge.begin (), packet.challenge ().data (), 256 / 8);
            memcpy (target.begin (), packet.target ().data (), 256 / 8);
            ProofOfWork::pointer pow = boost::make_shared<ProofOfWork> (packet.token (), packet.iterations (),
                                       challenge, target);

            if (!pow->isValid ())
            {
                charge (Resource::feeInvalidRequest);
                return;
            }

    #if 0   // Until proof of work is completed, don't do it
            getApp().getJobQueue ().addJob (
                jtPROOFWORK,
                "recvProof->doProof",
                BIND_TYPE (&PeerImp::doProofOfWork, P_1, boost::weak_ptr <Peer> (shared_from_this ()), pow));
    #endif

            return;
        }

        WriteLog (lsINFO, Peer) << "Received in valid proof of work object from peer";
    }

    void addLedger (uint256 const& hash)
    {
        boost::mutex::scoped_lock sl(m_recentLock);
        BOOST_FOREACH (uint256 const & ledger, m_recentLedgers)

        if (ledger == hash)
            return;

        if (m_recentLedgers.size () == 128)
            m_recentLedgers.pop_front ();

        m_recentLedgers.push_back (hash);
    }
    
    void getLedger (protocol::TMGetLedger& packet)
    {
            SHAMap::pointer map;
	    protocol::TMLedgerData reply;
	    bool fatLeaves = true, fatRoot = false;

	    if (packet.has_requestcookie ())
	        reply.set_requestcookie (packet.requestcookie ());

	    std::string logMe;

	    if (packet.itype () == protocol::liTS_CANDIDATE)
	    {
	        // Request is for a transaction candidate set
	        m_journal.trace << "Received request for TX candidate set data " 
	                        << to_string (this);

	        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
	        {
	            charge (Resource::feeInvalidRequest);
	            m_journal.warning << "invalid request for TX candidate set data";
	            return;
	        }

	        uint256 txHash;
	        memcpy (txHash.begin (), packet.ledgerhash ().data (), 32);

	        {
	            Application::ScopedLockType lock (getApp ().getMasterLock ());
	            map = getApp().getOPs ().getTXMap (txHash);
	        }

	        if (!map)
	        {
	            if (packet.has_querytype () && !packet.has_requestcookie ())
	            {
	                m_journal.debug << "Trying to route TX set request";

                    struct get_usable_peers
                    {
                        typedef Overlay::PeerSequence return_type;

                        Overlay::PeerSequence usablePeers;
                        uint256 const& txHash;
                        Peer const* skip;

                        get_usable_peers(uint256 const& hash, Peer const* s)
                            : txHash(hash), skip(s)
                        { }

                        void operator() (Peer::ptr const& peer)
                        {
                            if (peer->hasTxSet (txHash) && (peer.get () != skip))
                                usablePeers.push_back (peer);
                        }

                        return_type operator() ()
                        {
                            return usablePeers;
                        }
                    };

	                Overlay::PeerSequence usablePeers (m_overlay.foreach (
                        get_usable_peers (txHash, this)));

                    if (usablePeers.empty ())
                    {
                        m_journal.info << "Unable to route TX set request";
                        return;
                    }

                    Peer::ptr const& selectedPeer = usablePeers[rand () % usablePeers.size ()];
                    packet.set_requestcookie (getShortId ());
                    selectedPeer->sendPacket (
                        boost::make_shared<Message> (packet, protocol::mtGET_LEDGER),
                        false);
                    return;
	            }

	            m_journal.error << "We do not have the map our peer wants " 
	                            << to_string (this);

	            charge (Resource::feeInvalidRequest);
	            return;
	        }

            reply.set_ledgerseq (0);
	        reply.set_ledgerhash (txHash.begin (), txHash.size ());
	        reply.set_type (protocol::liTS_CANDIDATE);
	        fatLeaves = false; // We'll already have most transactions
	        fatRoot = true; // Save a pass
	    }
	    else
	    {
	        if (getApp().getFeeTrack().isLoadedLocal() && !m_clusterNode)
	        {
	            m_journal.debug << "Too busy to fetch ledger data";
	            return;
	        }

	        // Figure out what ledger they want
	        m_journal.trace << "Received request for ledger data "
	                        << to_string (this);
	        Ledger::pointer ledger;

	        if (packet.has_ledgerhash ())
	        {
	            uint256 ledgerhash;

	            if (packet.ledgerhash ().size () != 32)
	            {
	                charge (Resource::feeInvalidRequest);
	                m_journal.warning << "Invalid request";
	                return;
	            }

	            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
	            logMe += "LedgerHash:";
	            logMe += to_string (ledgerhash);
	            ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

	            if (!ledger && m_journal.trace)
	                m_journal.trace << "Don't have ledger " << ledgerhash;

	            if (!ledger && (packet.has_querytype () && !packet.has_requestcookie ()))
	            {
	                std::uint32_t seq = 0;

	                if (packet.has_ledgerseq ())
	                    seq = packet.ledgerseq ();

	                Overlay::PeerSequence peerList = m_overlay.getActivePeers ();
                        Overlay::PeerSequence usablePeers;
                        BOOST_FOREACH (Peer::ptr const& peer, peerList)
                        {
                            if (peer->hasLedger (ledgerhash, seq) && (peer.get () != this))
                                usablePeers.push_back (peer);
                        }

                        if (usablePeers.empty ())
                        {
                            m_journal.trace << "Unable to route ledger request";
                            return;
                        }

                        Peer::ptr const& selectedPeer = usablePeers[rand () % usablePeers.size ()];
                        packet.set_requestcookie (getShortId ());
                        selectedPeer->sendPacket (
                            boost::make_shared<Message> (packet, protocol::mtGET_LEDGER), false);
                        m_journal.debug << "Ledger request routed";
                        return;
                    }
	        }
	        else if (packet.has_ledgerseq ())
	        {
	            if (packet.ledgerseq() < getApp().getLedgerMaster().getEarliestFetch())
	            {
	                m_journal.debug << "Peer requests early ledger";
	                return;
	            }
	            ledger = getApp().getLedgerMaster ().getLedgerBySeq (packet.ledgerseq ());
	            if (!ledger && m_journal.debug)
	            	m_journal.debug << "Don't have ledger " << packet.ledgerseq ();
	        }
	        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCURRENT))
	        {
	            ledger = getApp().getLedgerMaster ().getCurrentLedger ();
	        }
	        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCLOSED) )
	        {
	            ledger = getApp().getLedgerMaster ().getClosedLedger ();

	            if (ledger && !ledger->isClosed ())
	                ledger = getApp().getLedgerMaster ().getLedgerBySeq (ledger->getLedgerSeq () - 1);
	        }
	        else
	        {
	            charge (Resource::feeInvalidRequest);
	            m_journal.warning << "Can't figure out what ledger they want";
	            return;
	        }

	        if ((!ledger) || (packet.has_ledgerseq () && (packet.ledgerseq () != ledger->getLedgerSeq ())))
	        {
	            charge (Resource::feeInvalidRequest);

	            if (m_journal.warning && ledger)
	            	m_journal.warning << "Ledger has wrong sequence";

	            return;
	        }

                if (!packet.has_ledgerseq() && (ledger->getLedgerSeq() < getApp().getLedgerMaster().getEarliestFetch()))
                {
                    m_journal.debug << "Peer requests early ledger";
                    return;
                }

	        // Fill out the reply
	        uint256 lHash = ledger->getHash ();
	        reply.set_ledgerhash (lHash.begin (), lHash.size ());
	        reply.set_ledgerseq (ledger->getLedgerSeq ());
	        reply.set_type (packet.itype ());

	        if (packet.itype () == protocol::liBASE)
	        {
	            // they want the ledger base data
	            m_journal.trace << "They want ledger base data";
	            Serializer nData (128);
	            ledger->addRaw (nData);
	            reply.add_nodes ()->set_nodedata (nData.getDataPtr (), nData.getLength ());

	            SHAMap::pointer map = ledger->peekAccountStateMap ();

	            if (map && map->getHash ().isNonZero ())
	            {
	                // return account state root node if possible
	                Serializer rootNode (768);

	                if (map->getRootNode (rootNode, snfWIRE))
	                {
	                    reply.add_nodes ()->set_nodedata (rootNode.getDataPtr (), rootNode.getLength ());

	                    if (ledger->getTransHash ().isNonZero ())
	                    {
	                        map = ledger->peekTransactionMap ();

	                        if (map && map->getHash ().isNonZero ())
	                        {
	                            rootNode.erase ();

	                            if (map->getRootNode (rootNode, snfWIRE))
	                                reply.add_nodes ()->set_nodedata (rootNode.getDataPtr (), rootNode.getLength ());
	                        }
	                    }
	                }
	            }

	            Message::pointer oPacket = boost::make_shared<Message> (reply, protocol::mtLEDGER_DATA);
	            sendPacket (oPacket, false);
	            return;
	        }

	        if (packet.itype () == protocol::liTX_NODE)
	        {
	            map = ledger->peekTransactionMap ();
	            logMe += " TX:";
	            logMe += to_string (map->getHash ());
	        }
	        else if (packet.itype () == protocol::liAS_NODE)
	        {
	            map = ledger->peekAccountStateMap ();
	            logMe += " AS:";
	            logMe += to_string (map->getHash ());
	        }
	    }

	    if (!map || (packet.nodeids_size () == 0))
	    {
	        m_journal.warning << "Can't find map or empty request";
	        charge (Resource::feeInvalidRequest);
	        return;
	    }

	    m_journal.trace << "Request: " << logMe;

	    for (int i = 0; i < packet.nodeids ().size (); ++i)
	    {
	        SHAMapNode mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

	        if (!mn.isValid ())
	        {
	            m_journal.warning << "Request for invalid node: " << logMe;
	            charge (Resource::feeInvalidRequest);
	            return;
	        }

	        std::vector<SHAMapNode> nodeIDs;
	        std::list< Blob > rawNodes;

	        try
	        {
	            if (map->getNodeFat (mn, nodeIDs, rawNodes, fatRoot, fatLeaves))
	            {
	                assert (nodeIDs.size () == rawNodes.size ());
	                m_journal.trace << "getNodeFat got " << rawNodes.size () << " nodes";
	                std::vector<SHAMapNode>::iterator nodeIDIterator;
	                std::list< Blob >::iterator rawNodeIterator;

	                for (nodeIDIterator = nodeIDs.begin (), rawNodeIterator = rawNodes.begin ();
	                        nodeIDIterator != nodeIDs.end (); ++nodeIDIterator, ++rawNodeIterator)
	                {
	                    Serializer nID (33);
	                    nodeIDIterator->addIDRaw (nID);
	                    protocol::TMLedgerNode* node = reply.add_nodes ();
	                    node->set_nodeid (nID.getDataPtr (), nID.getLength ());
	                    node->set_nodedata (&rawNodeIterator->front (), rawNodeIterator->size ());
	                }
	            }
	            else
	            	m_journal.warning << "getNodeFat returns false";
	        }
	        catch (std::exception&)
	        {
	            std::string info;

	            if (packet.itype () == protocol::liTS_CANDIDATE)
	                info = "TS candidate";
	            else if (packet.itype () == protocol::liBASE)
	                info = "Ledger base";
	            else if (packet.itype () == protocol::liTX_NODE)
	                info = "TX node";
	            else if (packet.itype () == protocol::liAS_NODE)
	                info = "AS node";

	            if (!packet.has_ledgerhash ())
	                info += ", no hash specified";

	            m_journal.warning << "getNodeFat( " << mn << ") throws exception: " << info;
	        }
	    }

	    Message::pointer oPacket = boost::make_shared<Message> (reply, protocol::mtLEDGER_DATA);
	    sendPacket (oPacket, false);    
    }
    
    // This is dispatched by the job queue
    static
    void
    sGetLedger (boost::weak_ptr<PeerImp> wPeer, 
        boost::shared_ptr <protocol::TMGetLedger> packet)
    {
        boost::shared_ptr<PeerImp> peer = wPeer.lock ();
        
        if (peer)
            peer->getLedger (*packet);
    }
    
    void addTxSet (uint256 const& hash)
    {
        boost::mutex::scoped_lock sl(m_recentLock);

        if(std::find (m_recentTxSets.begin (), m_recentTxSets.end (), hash) != m_recentTxSets.end ())
        	return;
        
        if (m_recentTxSets.size () == 128)
            m_recentTxSets.pop_front ();

        m_recentTxSets.push_back (hash);
    }

    void doFetchPack (const boost::shared_ptr<protocol::TMGetObjectByHash>& packet)
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
            BIND_TYPE (&NetworkOPs::makeFetchPack, &getApp().getOPs (), P_1,
                boost::weak_ptr<Peer> (shared_from_this ()), packet,
                    hash, UptimeTimer::getInstance ().getElapsedSeconds ()));
    }

    void doProofOfWork (Job&, boost::weak_ptr <Peer> peer, ProofOfWork::pointer pow)
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
                pptr->sendPacket (boost::make_shared<Message> (reply, protocol::mtPROOFOFWORK), false);
            }
            else
            {
                // WRITEME: Save solved proof of work for new connection
            }
        }
    }

    static void checkTransaction (Job&, int flags, SerializedTransaction::pointer stx, boost::weak_ptr<Peer> peer)
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

            bool needCheck = ! is_bit_set (flags, SF_SIGGOOD);
            Transaction::pointer tx =
                boost::make_shared<Transaction> (stx, needCheck);

            if (tx->getStatus () == INVALID)
            {
                getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
                charge (peer, Resource::feeInvalidSignature);
                return;
            }
            else
                getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_SIGGOOD);

            getApp().getOPs ().processTransaction (tx, is_bit_set (flags, SF_TRUSTED), false, false);

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
    static void checkPropose (Job& job, Overlay* pPeers, boost::shared_ptr<protocol::TMProposeSet> packet,
                              LedgerProposal::pointer proposal, uint256 consensusLCL, RippleAddress nodePublic,
                              boost::weak_ptr<Peer> peer, bool fromCluster)
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
                    boost::make_shared<Message> (set, protocol::mtPROPOSE_LEDGER),
                    peer_in_set(peers)));
	    }
        }
        else
        {
            WriteLog(lsDEBUG, Peer) << "Not relaying UNTRUSTED proposal";
        }
    }

    static void checkValidation (Job&, Overlay* pPeers, SerializedValidation::pointer val, bool isTrusted, bool isCluster,
                                 boost::shared_ptr<protocol::TMValidation> packet, boost::weak_ptr<Peer> peer)
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
                    boost::make_shared<Message> (*packet, protocol::mtVALIDATION),
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
