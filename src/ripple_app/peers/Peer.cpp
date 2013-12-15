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

SETUP_LOG (Peer)

class PeerImp;

// Don't try to run past receiving nonsense from a peer
// #define TRUST_NETWORK

// Node has this long to verify its identity from connection accepted or connection attempt.
#define NODE_VERIFY_SECONDS     15

// Idle nodes are probed this often
#define NODE_IDLE_SECONDS       120

class PeerImp : public Peer
    , public CountedObject <PeerImp>
{
private:
    // These is up here to prevent warnings about order of initializations
    //
    Resource::Manager& m_resourceManager;
    bool m_isInbound;

public:

    ScopedPointer <MultiSocket> m_socket;
    boost::asio::io_service::strand m_strand;

    NativeSocketType& getNativeSocket ()
    {
        return m_socket->next_layer <NativeSocketType> ();
    }

    MultiSocket& getHandshakeStream ()
    {
        return *m_socket;
    }

    MultiSocket& getStream ()
    {
        return *m_socket;
    }

    PeerImp (Resource::Manager& resourceManager,
        boost::asio::io_service& io_service,
            boost::asio::ssl::context& ssl_context,
                uint64 peerID, bool inbound,
                    MultiSocket::Flag flags)
        : m_resourceManager (resourceManager)
        , m_isInbound (inbound)
        , m_socket (MultiSocket::New (
            io_service, ssl_context, flags.asBits ()))
        , m_strand (io_service)
        , mHelloed (false)
        , mDetaching (false)
        , mActive (2)
        , mCluster (false)
        , mPeerId (peerID)
        , mPrivate (false)
        , mMinLedger (0)
        , mMaxLedger (0)
        , mActivityTimer (io_service)
        , m_remoteAddressSet (false)
    {
        WriteLog (lsDEBUG, Peer) << "CREATING PEER: " << addressToString (this);
    }
    
    //---------------------------------------------------------------------------
private:
    bool            mClientConnect;     // In process of connecting as client.
    bool            mHelloed;           // True, if hello accepted.
    bool            mDetaching;         // True, if detaching.
    int             mActive;            // 0=idle, 1=pingsent, 2=active
    bool            mCluster;           // Node in our cluster
    RippleAddress   mNodePublic;        // Node public key of peer.
    std::string     mNodeName;
    IPAndPortNumber          mIpPort;
    IPAndPortNumber          mIpPortConnect;
    uint256         mCookieHash;
    uint64          mPeerId;
    bool            mPrivate;           // Keep peer IP private.
    uint32          mMinLedger, mMaxLedger;

    uint256         mClosedLedgerHash;
    uint256         mPreviousLedgerHash;

    std::list<uint256>    mRecentLedgers;
    std::list<uint256>    mRecentTxSets;
    mutable boost::mutex  mRecentLock;


    boost::asio::deadline_timer                                 mActivityTimer;

    std::vector<uint8_t>                mReadbuf;
    std::list<PackedMessage::pointer>   mSendQ;
    PackedMessage::pointer              mSendingPacket;
    protocol::TMStatusChange              mLastStatus;
    protocol::TMHello                     mHello;

    bool            m_remoteAddressSet;
    IPAddress      m_remoteAddress;
    Resource::Consumer m_usage;
    
public:
    static char const* getCountedObjectName () { return "Peer"; }

    void handleConnect (const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator it);

    std::string const& getIP ()
    {
        return mIpPort.first;
    }

    std::string getDisplayName ()
    {
        return mCluster ? mNodeName : mIpPort.first;
    }
    int getPort ()
    {
        return mIpPort.second;
    }
    bool getConnectString(std::string& connect) const
    {
        if (!mHello.has_ipv4port() || mIpPortConnect.first.empty())
            return false;
        connect = boost::str(boost::format("%s %d") % mIpPortConnect.first % mHello.ipv4port());
        return true;
    }


    void setIpPort (const std::string & strIP, int iPort);

    void connect (const std::string & strIp, int iPort);
    void connected (const boost::system::error_code & error);
    void detach (const char*, bool onIOStrand);

    // VFALCO Seems no one is using these
    //bool samePeer (Peer::ref p)           { return samePeer(*p); }
    //bool samePeer (const Peer& p)     { return this == &p; }

    void sendPacket (const PackedMessage::pointer & packet, bool onStrand);

    void sendGetPeers ();

    void charge (Resource::Charge const& fee)
    {
        if ((m_usage.charge (fee) == Resource::drop) && m_usage.disconnect ())
            detach ("resource", false);
    }

    Json::Value getJson ();
    bool isConnected () const
    {
        return mHelloed && !mDetaching;
    }
    bool isInCluster () const
    {
        return mCluster;
    }
    bool isInbound () const
    {
        return m_isInbound;
    }
    bool isOutbound () const
    {
        return !m_isInbound;
    }

    uint256 const& getClosedLedgerHash () const
    {
        return mClosedLedgerHash;
    }
    bool hasLedger (uint256 const & hash, uint32 seq) const;
    void ledgerRange (uint32& minSeq, uint32& maxSeq) const;
    bool hasTxSet (uint256 const & hash) const;
    uint64 getPeerId () const
    {
        return mPeerId;
    }

    const RippleAddress& getNodePublic () const
    {
        return mNodePublic;
    }
    void cycleStatus ()
    {
        mPreviousLedgerHash = mClosedLedgerHash;
        mClosedLedgerHash.zero ();
    }
    bool hasProto (int version);
    bool hasRange (uint32 uMin, uint32 uMax)
    {
        return (uMin >= mMinLedger) && (uMax <= mMaxLedger);
    }

    IPAddress getPeerEndpoint() const
    {
        return m_remoteAddress;
    }

private:
    void handleShutdown (const boost::system::error_code & error)
    {
        ;
    }
    void handleWrite (const boost::system::error_code & error, size_t bytes_transferred);

    void handleReadHeader (boost::system::error_code const& error,
                           std::size_t bytes_transferred)
    {
        if (mDetaching)
        {
            // Drop data or error if detaching.
            nothing ();
        }
        else if (!error)
        {
            unsigned msg_len = PackedMessage::getLength (mReadbuf);

            // WRITEME: Compare to maximum message length, abort if too large
            if ((msg_len > (32 * 1024 * 1024)) || (msg_len == 0))
            {
                detach ("hrh", true);
                return;
            }

            startReadBody (msg_len);
        }
        else
        {
            if (mCluster)
            {
                WriteLog (lsINFO, Peer) << "Peer: Cluster connection lost to \"" << mNodeName << "\": " <<
                                        error.category ().name () << ": " << error.message () << ": " << error;
            }
            else
            {
                WriteLog (lsINFO, Peer) << "Peer: Header: Error: " << getIP () << ": " << error.category ().name () << ": " << error.message () << ": " << error;
            }

            detach ("hrh2", true);
        }
    }

    void handleReadBody (boost::system::error_code const& error,
                         std::size_t bytes_transferred)
    {
        if (mDetaching)
        {
            return;
        }
        else if (error)
        {
            if (mCluster)
            {
                WriteLog (lsINFO, Peer) << "Peer: Cluster connection lost to \"" << mNodeName << "\": " <<
                                        error.category ().name () << ": " << error.message () << ": " << error;
            }
            else
            {
                WriteLog (lsINFO, Peer) << "Peer: Body: Error: " << getIP () << ": " << error.category ().name () << ": " << error.message () << ": " << error;
            }

            {
                Application::ScopedLockType lock (getApp ().getMasterLock (), __FILE__, __LINE__);

                detach ("hrb", true);
            }
            return;
        }

        processReadBuffer ();
        startReadHeader ();
    }

    // We have an encrypted connection to the peer.
    // Have it say who it is so we know to avoid redundant connections.
    // Establish that it really who we are talking to by having it sign a connection detail.
    // Also need to establish no man in the middle attack is in progress.
    void handleStart (const boost::system::error_code& error)
    {
        if (error)
        {
            WriteLog (lsINFO, Peer) << "Peer: Handshake: Error: " << error.category ().name () << ": " << error.message () << ": " << error;
            detach ("hs", true);
        }
        else
        {
            bool valid = false;

            try
            {
                if (m_socket->getFlags ().set (MultiSocket::Flag::proxy) && m_isInbound)
                {
                    MultiSocket::ProxyInfo const proxyInfo (m_socket->getProxyInfo ());

                    if (proxyInfo.protocol == "TCP4")
                    {
                        m_remoteAddressSet = true;
                        m_remoteAddress = IPAddress (IPAddress::V4 (
                            proxyInfo.sourceAddress.value [0],
                            proxyInfo.sourceAddress.value [1],
                            proxyInfo.sourceAddress.value [2],
                            proxyInfo.sourceAddress.value [3]),
                            proxyInfo.sourcePort);

                        // Set remote IP and port number from PROXY handshake
                        mIpPort.first = proxyInfo.sourceAddress.toString ().toStdString ();
                        mIpPort.second = proxyInfo.sourcePort;

                        if (m_isInbound)
                            m_usage = m_resourceManager.newInboundEndpoint (m_remoteAddress);
                        else
                            m_usage = m_resourceManager.newOutboundEndpoint (m_remoteAddress);

                        valid = true;

                        WriteLog (lsINFO, Peer) << "Peer: PROXY handshake from " << mIpPort.first;
                    }
                    else
                    {
                        if (proxyInfo.protocol != String::empty)
                        {
                            WriteLog (lsINFO, Peer) << "Peer: Unknown PROXY protocol " <<
                                proxyInfo.protocol.toStdString ();
                        }
                        else
                        {
                            WriteLog (lsINFO, Peer) << "Peer: Missing PROXY handshake";
                        }

                        detach ("pi", true);
                    }
                }
                else
                {
                    boost::asio::ip::address addr (getNativeSocket().remote_endpoint().address());

                    if (addr.is_v4())
                    {
                        boost::asio::ip::address_v4::bytes_type bytes (addr.to_v4().to_bytes());
                        m_remoteAddress = IPAddress (IPAddress::V4 (
                            bytes[0], bytes[1], bytes[2], bytes[3]), 0);
                        if (! m_isInbound)
                            m_remoteAddress = m_remoteAddress.withPort (
                                getNativeSocket().remote_endpoint().port());
                    }
                    else
                    {
                        // TODO: Support ipv6
                        bassertfalse;
                    }
                    m_remoteAddressSet = true;

                    if (m_isInbound)
                        m_usage = m_resourceManager.newInboundEndpoint (m_remoteAddress);
                    else
                        m_usage = m_resourceManager.newOutboundEndpoint (m_remoteAddress);

                    valid = true;
                }
            }
            catch (...)
            {
                WriteLog (lsDEBUG, Peer) << "exception accepting peer";
                detach ("ex", true);
            }

            if (valid)
            {
                if (m_usage.disconnect ())
                {
                    detach ("resource", true);
                }
                else
                {
                    getApp ().getPeers ().peerConnected(m_remoteAddress, m_isInbound);

                    // Must compute mCookieHash before receiving a hello.
                    sendHello ();
                    startReadHeader ();
                }
            }

        }
    }

    void handleVerifyTimer (const boost::system::error_code & ecResult);
    void handlePingTimer (const boost::system::error_code & ecResult);

    void processReadBuffer ();
    void startReadHeader ();
    void startReadBody (unsigned msg_len);

    void sendPacketForce (const PackedMessage::pointer & packet);

    void sendHello ();

    void recvHello (protocol::TMHello & packet);
    void recvCluster (protocol::TMCluster & packet);
    void recvTransaction (protocol::TMTransaction & packet, Application::ScopedLockType& masterLockHolder);
    void recvValidation (const boost::shared_ptr<protocol::TMValidation>& packet, Application::ScopedLockType& masterLockHolder);
    void recvGetValidation (protocol::TMGetValidations & packet);
    void recvContact (protocol::TMContact & packet);
    void recvGetContacts (protocol::TMGetContacts & packet);
    void recvGetPeers (protocol::TMGetPeers & packet, Application::ScopedLockType& masterLockHolder);
    void recvPeers (protocol::TMPeers & packet);
    void recvEndpoints (protocol::TMEndpoints & packet);
    void recvGetObjectByHash (const boost::shared_ptr<protocol::TMGetObjectByHash>& packet);
    void recvPing (protocol::TMPing & packet);
    void recvErrorMessage (protocol::TMErrorMsg & packet);
    void recvSearchTransaction (protocol::TMSearchTransaction & packet);
    void recvGetAccount (protocol::TMGetAccount & packet);
    void recvAccount (protocol::TMAccount & packet);
    void recvGetLedger (protocol::TMGetLedger & packet, Application::ScopedLockType& masterLockHolder);
    void recvLedger (const boost::shared_ptr<protocol::TMLedgerData>& packet, Application::ScopedLockType& masterLockHolder);
    void recvStatus (protocol::TMStatusChange & packet);
    void recvPropose (const boost::shared_ptr<protocol::TMProposeSet>& packet);
    void recvHaveTxSet (protocol::TMHaveTransactionSet & packet);
    void recvProofWork (protocol::TMProofWork & packet);

    void getSessionCookie (std::string & strDst);

    void addLedger (uint256 const & ledger);
    void addTxSet (uint256 const & TxSet);

    void doFetchPack (const boost::shared_ptr<protocol::TMGetObjectByHash>& packet);

    static void doProofOfWork (Job&, boost::weak_ptr <Peer>, ProofOfWork::pointer);
};

void PeerImp::handleWrite (const boost::system::error_code& error, size_t bytes_transferred)
{
    // Call on IO strand
#ifdef BEAST_DEBUG
    //  if (!error)
    //      Log::out() << "Peer::handleWrite bytes: "<< bytes_transferred;
#endif

    mSendingPacket.reset ();

    if (mDetaching)
    {
        // Ignore write requests when detatching.
        nothing ();
    }
    else if (error)
    {
        WriteLog (lsINFO, Peer) << "Peer: Write: Error: " << addressToString (this) << ": bytes=" << bytes_transferred << ": " << error.category ().name () << ": " << error.message () << ": " << error;

        detach ("hw", true);
    }
    else if (!mSendQ.empty ())
    {
        PackedMessage::pointer packet = mSendQ.front ();

        if (packet)
        {
            sendPacketForce (packet);
            mSendQ.pop_front ();
        }
    }
}

void PeerImp::setIpPort (const std::string& strIP, int iPort)
{
    mIpPort = make_pair (strIP, iPort);

    WriteLog (lsDEBUG, Peer) << "Peer: Set: "
                             << addressToString (this) << "> "
                             << (mNodePublic.isValid () ? mNodePublic.humanNodePublic () : "-") << " " << getIP () << " " << getPort ();
}

void PeerImp::detach (const char* rsn, bool onIOStrand)
{
    // VFALCO NOTE So essentially, detach() is really two different functions
    //              depending on the value of onIOStrand.
    //        TODO Clean this up.
    //
    if (!onIOStrand)
    {
        m_strand.post (BIND_TYPE (&Peer::detach, shared_from_this (), rsn, true));
        return;
    }

    if (!mDetaching)
    {
        mDetaching  = true;         // Race is ok.

        CondLog (mCluster, lsWARNING, Peer) << "Cluster peer detach \"" << mNodeName << "\": " << rsn;
        /*
        WriteLog (lsDEBUG, Peer) << "Peer: Detach: "
            << addressToString(this) << "> "
            << rsn << ": "
            << (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
            */

        mSendQ.clear ();

        (void) mActivityTimer.cancel ();
        getHandshakeStream ().async_shutdown (m_strand.wrap (boost::bind
                                   (&PeerImp::handleShutdown, boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                    boost::asio::placeholders::error)));

        if (mNodePublic.isValid ())
        {
            getApp().getPeers ().peerDisconnected (shared_from_this (), mNodePublic);

            mNodePublic.clear ();       // Be idempotent.
        }

        if (!mIpPort.first.empty ())
        {
            // Connection might be part of scanning.  Inform connect failed.
            // Might need to scan. Inform connection closed.
            getApp().getPeers ().peerClosed (shared_from_this (), mIpPort.first, mIpPort.second);

            mIpPort.first.clear ();     // Be idempotent.
        }

        /*
        WriteLog (lsDEBUG, Peer) << "Peer: Detach: "
            << addressToString(this) << "< "
            << rsn << ": "
            << (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
            */
    }
}

void PeerImp::handlePingTimer (const boost::system::error_code& ecResult)
{
    // called on IO strand
    if (ecResult || mDetaching)
        return;

    if (mActive == 1)
    {
        // ping out
        detach ("pto", true);
        return;
    }

    if (mActive == 0)
    {
        // idle->pingsent
        mActive = 1;
        protocol::TMPing packet;
        packet.set_type (protocol::TMPing::ptPING);
        sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtPING), true);
    }
    else // active->idle
        mActive = 0;

    mActivityTimer.expires_from_now (boost::posix_time::seconds (NODE_IDLE_SECONDS));
    mActivityTimer.async_wait (m_strand.wrap (boost::bind (
                                   &PeerImp::handlePingTimer,
                                   boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                   boost::asio::placeholders::error)));
}


void PeerImp::handleVerifyTimer (const boost::system::error_code& ecResult)
{
    if (ecResult == boost::asio::error::operation_aborted)
    {
        // Timer canceled because deadline no longer needed.
        // Log::out() << "Deadline cancelled.";

        nothing (); // Aborter is done.
    }
    else if (ecResult)
    {
        WriteLog (lsINFO, Peer) << "Peer verify timer error";
    }
    else
    {
        //WriteLog (lsINFO, Peer) << "Peer: Verify: Peer failed to verify in time.";

        detach ("hvt", true);
    }
}

// Begin trying to connect. We are not connected till we know and accept peer's public key.
// Only takes IP addresses (not domains).
void PeerImp::connect (const std::string& strIp, int iPort)
{
    int iPortAct    = (iPort <= 0) ? SYSTEM_PEER_PORT : iPort;

    mClientConnect  = true;

    mIpPort         = make_pair (strIp, iPort);
    mIpPortConnect  = mIpPort;
    assert (!mIpPort.first.empty ());

    boost::asio::ip::tcp::resolver::query   query (strIp, lexicalCastThrow <std::string> (iPortAct),
            boost::asio::ip::resolver_query_base::numeric_host | boost::asio::ip::resolver_query_base::numeric_service);
    boost::asio::ip::tcp::resolver              resolver (getApp().getIOService ());
    boost::system::error_code                   err;
    boost::asio::ip::tcp::resolver::iterator    itrEndpoint = resolver.resolve (query, err);

    if (err || itrEndpoint == boost::asio::ip::tcp::resolver::iterator ())
    {
        WriteLog (lsWARNING, Peer) << "Peer: Connect: Bad IP: " << strIp;
        detach ("c", false);
        return;
    }
    else
    {
        mActivityTimer.expires_from_now (boost::posix_time::seconds (NODE_VERIFY_SECONDS), err);

        mActivityTimer.async_wait (m_strand.wrap (boost::bind (
                                       &PeerImp::handleVerifyTimer,
                                       boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                       boost::asio::placeholders::error)));

        if (err)
        {
            WriteLog (lsWARNING, Peer) << "Peer: Connect: Failed to set timer.";
            detach ("c2", false);
            return;
        }
    }

    if (!err)
    {
        WriteLog (lsINFO, Peer) << "Peer: Connect: Outbound: " <<
            addressToString (this) << ": " <<
            mIpPort.first << " " << mIpPort.second;

        // Notify peer finder that we have a connection attempt in-progress
        getApp ().getPeers ().getPeerFinder ().onPeerConnectAttemptBegins(
            IPAddress::from_string(strIp).withPort(iPortAct) );

        boost::asio::async_connect (
            getNativeSocket (),
            itrEndpoint,
            m_strand.wrap (boost::bind (
                                &PeerImp::handleConnect,
                                boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::iterator)));
    }
}

// Connect ssl as client.
void PeerImp::handleConnect (const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it)
{
    // Notify peer finder about the status of this in-progress connection attempt
#if RIPPLE_USE_PEERFINDER
    getApp ().getPeers ().getPeerFinder ().onPeerConnectAttemptCompletes(
        IPAddress::from_string(getIP()).withPort(getPort()), !error );
#endif

    if (error)
    {
        WriteLog (lsINFO, Peer) << "Peer: Connect: Error: " <<
            getIP() << ":" << getPort() <<
            " (" << error.category ().name () <<
            ": " << error.message () <<
            ": " << error << ")";
        detach ("hc", true);
    }
    else
    {
        WriteLog (lsINFO, Peer) << "Peer: Connect: Success: " <<
            getIP() << ":" << getPort();

        getHandshakeStream ().set_verify_mode (boost::asio::ssl::verify_none);

        getHandshakeStream ().async_handshake (boost::asio::ssl::stream_base::client,
            m_strand.wrap (boost::bind (&PeerImp::handleStart,
                boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                    boost::asio::placeholders::error)));
    }
}

// Connect ssl as server to an inbound connection.
// - We don't bother remembering the inbound IP or port.  Only useful for debugging.
void PeerImp::connected (const boost::system::error_code& error)
{
    boost::asio::ip::tcp::endpoint  ep;
    int                             iPort;
    std::string                     strIp;

    try
    {
        ep      = getNativeSocket ().remote_endpoint ();
        iPort   = ep.port ();
        strIp   = ep.address ().to_string ();
    }
    catch (...)
    {
        detach ("edc", false);
        return;
    }

    mClientConnect  = false;
    mIpPortConnect  = make_pair (strIp, iPort);

    if (iPort == SYSTEM_PEER_PORT)      //TODO: Why are you doing this?
        iPort   = -1;

    if (!error)
    {
        // Not redundant ip and port, handshake, and start.

        WriteLog (lsINFO, Peer) << "Peer: Inbound: Accepted: " << addressToString (this) << ": " << strIp << " " << iPort;

        getHandshakeStream ().set_verify_mode (boost::asio::ssl::verify_none);

        getHandshakeStream ().async_handshake (boost::asio::ssl::stream_base::server, m_strand.wrap (boost::bind (
            &PeerImp::handleStart, boost::static_pointer_cast <PeerImp> (shared_from_this ()),
            boost::asio::placeholders::error)));
    }
    else if (!mDetaching)
    {
        WriteLog (lsINFO, Peer) << "Peer: Inbound: Error: " << addressToString (this) << ": " << strIp << " " << iPort << " : " << error.category ().name () << ": " << error.message () << ": " << error;

        detach ("ctd", false);
    }
}

void PeerImp::sendPacketForce (const PackedMessage::pointer& packet)
{
    // must be on IO strand
    if (!mDetaching)
    {
        mSendingPacket = packet;

        boost::asio::async_write (getStream (), boost::asio::buffer (packet->getBuffer ()),
                                  m_strand.wrap (boost::bind (&PeerImp::handleWrite,
                                          boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                          boost::asio::placeholders::error,
                                          boost::asio::placeholders::bytes_transferred)));
    }
}

void PeerImp::sendPacket (const PackedMessage::pointer& packet, bool onStrand)
{
    if (packet)
    {
        if (!onStrand)
        {
            m_strand.post (BIND_TYPE (&Peer::sendPacket, shared_from_this (), packet, true));
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

void PeerImp::startReadHeader ()
{
    if (!mDetaching)
    {
        mReadbuf.clear ();
        mReadbuf.resize (PackedMessage::kHeaderBytes);

        boost::asio::async_read (getStream (),
                                 boost::asio::buffer (mReadbuf),
                                 m_strand.wrap (boost::bind (&PeerImp::handleReadHeader,
                                         boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred)));
    }
}

void PeerImp::startReadBody (unsigned msg_len)
{
    // m_readbuf already contains the header in its first PackedMessage::kHeaderBytes
    // bytes. Expand it to fit in the body as well, and start async
    // read into the body.

    if (!mDetaching)
    {
        mReadbuf.resize (PackedMessage::kHeaderBytes + msg_len);

        boost::asio::async_read (getStream (),
                                 boost::asio::buffer (&mReadbuf [PackedMessage::kHeaderBytes], msg_len),
                                 m_strand.wrap (boost::bind (&PeerImp::handleReadBody,
                                         boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred)));
    }
}

void PeerImp::processReadBuffer ()
{
    // must not hold peer lock
    int type = PackedMessage::getType (mReadbuf);
#ifdef BEAST_DEBUG
    //  Log::out() << "PRB(" << type << "), len=" << (mReadbuf.size()-PackedMessage::kHeaderBytes);
#endif

    //  Log::out() << "Peer::processReadBuffer: " << mIpPort.first << " " << mIpPort.second;

    LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtPEER, "Peer::read"));

    {
        Application::ScopedLockType lock (getApp ().getMasterLock (), __FILE__, __LINE__);

        // If connected and get a mtHELLO or if not connected and get a non-mtHELLO, wrong message was sent.
        if (mHelloed == (type == protocol::mtHELLO))
        {
            WriteLog (lsWARNING, Peer) << "Wrong message type: " << type;
            detach ("prb1", true);
        }
        else
        {
            switch (type)
            {
            case protocol::mtHELLO:
            {
                event->reName ("Peer::hello");
                protocol::TMHello msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvHello (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtCLUSTER:
            {
                event->reName ("Peer::cluster");
                protocol::TMCluster msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvCluster (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }

            case protocol::mtERROR_MSG:
            {
                event->reName ("Peer::errormessage");
                protocol::TMErrorMsg msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvErrorMessage (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtPING:
            {
                event->reName ("Peer::ping");
                protocol::TMPing msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvPing (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtGET_CONTACTS:
            {
                event->reName ("Peer::getcontacts");
                protocol::TMGetContacts msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvGetContacts (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtCONTACT:
            {
                event->reName ("Peer::contact");
                protocol::TMContact msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvContact (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtGET_PEERS:
            {
                event->reName ("Peer::getpeers");
                protocol::TMGetPeers msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvGetPeers (msg, lock);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtPEERS:
            {
                event->reName ("Peer::peers");
                protocol::TMPeers msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvPeers (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtENDPOINTS:
            {
                event->reName ("Peer::endpoints");
                protocol::TMEndpoints msg;

                if(msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size() - PackedMessage::kHeaderBytes))
                    recvEndpoints (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;;
            }
            break;
            
            case protocol::mtSEARCH_TRANSACTION:
            {
                event->reName ("Peer::searchtransaction");
                protocol::TMSearchTransaction msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvSearchTransaction (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtGET_ACCOUNT:
            {
                event->reName ("Peer::getaccount");
                protocol::TMGetAccount msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvGetAccount (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtACCOUNT:
            {
                event->reName ("Peer::account");
                protocol::TMAccount msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvAccount (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtTRANSACTION:
            {
                event->reName ("Peer::transaction");
                protocol::TMTransaction msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvTransaction (msg, lock);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtSTATUS_CHANGE:
            {
                event->reName ("Peer::statuschange");
                protocol::TMStatusChange msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvStatus (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtPROPOSE_LEDGER:
            {
                event->reName ("Peer::propose");
                boost::shared_ptr<protocol::TMProposeSet> msg = boost::make_shared<protocol::TMProposeSet> ();

                if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvPropose (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtGET_LEDGER:
            {
                event->reName ("Peer::getledger");
                protocol::TMGetLedger msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvGetLedger (msg, lock);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtLEDGER_DATA:
            {
                event->reName ("Peer::ledgerdata");
                boost::shared_ptr<protocol::TMLedgerData> msg = boost::make_shared<protocol::TMLedgerData> ();

                if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvLedger (msg, lock);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtHAVE_SET:
            {
                event->reName ("Peer::haveset");
                protocol::TMHaveTransactionSet msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvHaveTxSet (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtVALIDATION:
            {
                event->reName ("Peer::validation");
                boost::shared_ptr<protocol::TMValidation> msg = boost::make_shared<protocol::TMValidation> ();

                if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvValidation (msg, lock);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;
    #if 0

            case protocol::mtGET_VALIDATION:
            {
                protocol::TM msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recv (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

    #endif

            case protocol::mtGET_OBJECTS:
            {
                event->reName ("Peer::getobjects");
                boost::shared_ptr<protocol::TMGetObjectByHash> msg = boost::make_shared<protocol::TMGetObjectByHash> ();

                if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvGetObjectByHash (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;

            case protocol::mtPROOFOFWORK:
            {
                event->reName ("Peer::proofofwork");
                protocol::TMProofWork msg;

                if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                    recvProofWork (msg);
                else
                    WriteLog (lsWARNING, Peer) << "parse error: " << type;
            }
            break;


            default:
                event->reName ("Peer::unknown");
                WriteLog (lsWARNING, Peer) << "Unknown Msg: " << type;
                WriteLog (lsWARNING, Peer) << strHex (&mReadbuf[0], mReadbuf.size ());
            }
        }
    }
}

void PeerImp::recvHello (protocol::TMHello& packet)
{
    bool    bDetach = true;

    (void) mActivityTimer.cancel ();
    mActivityTimer.expires_from_now (boost::posix_time::seconds (NODE_IDLE_SECONDS));
    mActivityTimer.async_wait (m_strand.wrap (boost::bind (&PeerImp::handlePingTimer, boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                               boost::asio::placeholders::error)));

    uint32 ourTime = getApp().getOPs ().getNetworkTimeNC ();
    uint32 minTime = ourTime - 20;
    uint32 maxTime = ourTime + 20;

#ifdef BEAST_DEBUG
    if (packet.has_nettime ())
    {
        int64 to = ourTime;
        to -= packet.nettime ();
        WriteLog (lsDEBUG, Peer) << "Connect: time offset " << to;
    }

#endif

    if ((packet.has_testnet () && packet.testnet ()) != getConfig ().TESTNET)
    {
        // Format: actual/requested.
        WriteLog (lsINFO, Peer) << boost::str (boost::format ("Recv(Hello): Network mismatch: %d/%d")
                                               % packet.testnet ()
                                               % getConfig ().TESTNET);
    }
    else if (packet.has_nettime () && ((packet.nettime () < minTime) || (packet.nettime () > maxTime)))
    {
        if (packet.nettime () > maxTime)
        {
            WriteLog (lsINFO, Peer) << "Recv(Hello): " << getIP () << " :Clock far off +" << packet.nettime () - ourTime;
        }
        else if (packet.nettime () < minTime)
        {
            WriteLog (lsINFO, Peer) << "Recv(Hello): " << getIP () << " :Clock far off -" << ourTime - packet.nettime ();
        }
    }
    else if (packet.protoversionmin () > BuildInfo::getCurrentProtocol().toPacked ())
    {
        WriteLog (lsINFO, Peer) <<
            "Recv(Hello): Server requires protocol version " << BuildInfo::Protocol (packet.protoversion()).toStdString () <<
            ", we run " << BuildInfo::getCurrentProtocol().toStdString ();
    }
    else if (!mNodePublic.setNodePublic (packet.nodepublic ()))
    {
        WriteLog (lsINFO, Peer) << "Recv(Hello): Disconnect: Bad node public key.";
    }
    else if (!mNodePublic.verifyNodePublic (mCookieHash, packet.nodeproof ()))
    {
        // Unable to verify they have private key for claimed public key.
        WriteLog (lsINFO, Peer) << "Recv(Hello): Disconnect: Failed to verify session.";
    }
    else
    {
        // Successful connection.
        WriteLog (lsINFO, Peer) << "Recv(Hello): Connect: " << mNodePublic.humanNodePublic ();
        CondLog (BuildInfo::Protocol (packet.protoversion()) != BuildInfo::getCurrentProtocol(), lsINFO, Peer) <<
                "Peer speaks version " << BuildInfo::Protocol (packet.protoversion()).toStdString ();
        mHello = packet;

        if (getApp().getUNL ().nodeInCluster (mNodePublic, mNodeName))
        {
            mCluster = true;
            WriteLog (lsINFO, Peer) << "Cluster connection to \"" << (mNodeName.empty () ? getIP () : mNodeName)
                                    << "\" established";
        }

        if (mClientConnect)
        {
            // If we connected due to scan, no longer need to scan.
            getApp().getPeers ().peerVerified (shared_from_this ());
        }

        if (! getApp().getPeers ().peerHandshake (shared_from_this (), mNodePublic, getIP (), getPort ()))
        {
            // Already connected, self, or some other reason.
            WriteLog (lsINFO, Peer) << "Recv(Hello): Disconnect: Extraneous connection.";
        }
        else
        {
            if (mClientConnect)
            {
                // No longer connecting as client.
                mClientConnect  = false;
            }
            else
            {
                try
                {
                    // Take a guess at remotes address.
                    std::string strIP   = getNativeSocket ().remote_endpoint ().address ().to_string ();
                    int         iPort   = packet.ipv4port ();

                    if (mHello.nodeprivate ())
                    {
                        WriteLog (lsINFO, Peer) << boost::str (boost::format ("Recv(Hello): Private connection: %s %s") % strIP % iPort);
                    }
                    else
                    {
                        // Don't save IP address if the node wants privacy.
                        // Note: We don't go so far as to delete it.  If a node which has previously announced itself now wants
                        // privacy, it should at least change its port.
                        getApp().getPeers ().savePeer (strIP, iPort, UniqueNodeList::vsInbound);
                    }
                }
                catch (boost::system::system_error const&)
                {
                }
            }

            // Consider us connected.  No longer accepting mtHELLO.
            mHelloed        = true;

            // XXX Set timer: connection is in grace period to be useful.
            // XXX Set timer: connection idle (idle may vary depending on connection type.)

            if ((packet.has_ledgerclosed ()) && (packet.ledgerclosed ().size () == (256 / 8)))
            {
                memcpy (mClosedLedgerHash.begin (), packet.ledgerclosed ().data (), 256 / 8);

                if ((packet.has_ledgerprevious ()) && (packet.ledgerprevious ().size () == (256 / 8)))
                {
                    memcpy (mPreviousLedgerHash.begin (), packet.ledgerprevious ().data (), 256 / 8);
                    addLedger (mPreviousLedgerHash);
                }
                else mPreviousLedgerHash.zero ();
            }

            bDetach = false;
        }
    }

    if (bDetach)
    {
        mNodePublic.clear ();
        detach ("recvh", true);
    }
    else
    {
        sendGetPeers ();
    }
}

static void checkTransaction (Job&, int flags, SerializedTransaction::pointer stx, boost::weak_ptr<Peer> peer)
{

#ifndef TRUST_NETWORK

    try
    {
#endif
        Transaction::pointer tx;

        if (isSetBit (flags, SF_SIGGOOD))
            tx = boost::make_shared<Transaction> (stx, false);
        else
            tx = boost::make_shared<Transaction> (stx, true);

        if (tx->getStatus () == INVALID)
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            Peer::charge (peer, Resource::feeInvalidSignature);
            return;
        }
        else
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_SIGGOOD);

        getApp().getOPs ().processTransaction (tx, isSetBit (flags, SF_TRUSTED), false);

#ifndef TRUST_NETWORK
    }
    catch (...)
    {
        getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
        Peer::charge (peer, Resource::feeInvalidRequest);
    }

#endif
}

void PeerImp::recvTransaction (protocol::TMTransaction& packet, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    Transaction::pointer tx;
#ifndef TRUST_NETWORK

    try
    {
#endif
        Serializer s (packet.rawtransaction ());
        SerializerIterator sit (s);
        SerializedTransaction::pointer stx = boost::make_shared<SerializedTransaction> (boost::ref (sit));
        uint256 txID = stx->getTransactionID();

        int flags;

        if (! getApp().getHashRouter ().addSuppressionPeer (txID, mPeerId, flags))
        {
            // we have seen this transaction recently
            if (isSetBit (flags, SF_BAD))
            {
                charge (Resource::feeInvalidSignature);
                return;
            }

            if (!isSetBit (flags, SF_RETRY))
                return;
        }

        WriteLog (lsDEBUG, Peer) << "Got transaction from peer " << getDisplayName () << " : " << txID;

        if (mCluster)
            flags |= SF_TRUSTED | SF_SIGGOOD;

        if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
            WriteLog(lsINFO, Peer) << "Transaction queue is full";
        else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
            WriteLog(lsINFO, Peer) << "No new transactions until synchronized";
        else
            getApp().getJobQueue ().addJob (jtTRANSACTION, "recvTransaction->checkTransaction",
                                       BIND_TYPE (&checkTransaction, P_1, flags, stx, boost::weak_ptr<Peer> (shared_from_this ())));

#ifndef TRUST_NETWORK
    }
    catch (...)
    {
#ifdef BEAST_DEBUG
        Log::out() << "Transaction from peer fails validity tests";
        Json::StyledStreamWriter w;
        // VFALCO NOTE This bypasses the Log bottleneck
        w.write (std::cerr, tx->getJson (0));
#endif
        return;
    }

#endif

}

// Called from our JobQueue
static void checkPropose (Job& job, boost::shared_ptr<protocol::TMProposeSet> packet,
                          LedgerProposal::pointer proposal, uint256 consensusLCL, RippleAddress nodePublic,
                          boost::weak_ptr<Peer> peer, bool fromCluster)
{
    bool sigGood = false;
    bool isTrusted = (job.getType () == jtPROPOSAL_t);

    WriteLog (lsTRACE, Peer) << "Checking " << (isTrusted ? "trusted" : "UNtrusted") << " proposal";

    assert (packet);
    protocol::TMProposeSet& set = *packet;

    uint256 prevLedger;

    if (set.has_previousledger ())
    {
        // proposal includes a previous ledger
        WriteLog (lsTRACE, Peer) << "proposal with previous ledger";
        memcpy (prevLedger.begin (), set.previousledger ().data (), 256 / 8);

        if (!fromCluster && !proposal->checkSign (set.signature ()))
        {
            Peer::pointer p = peer.lock ();
            WriteLog (lsWARNING, Peer) << "proposal with previous ledger fails signature check: " <<
                                       (p ? p->getIP () : std::string ("???"));
            Peer::charge (peer, Resource::feeInvalidSignature);
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
            WriteLog (lsWARNING, Peer) << "Ledger proposal fails signature check"; // Could be mismatched prev ledger
            proposal->setSignature (set.signature ());
        }
    }

    if (isTrusted)
        getApp().getOPs ().processTrustedProposal (proposal, packet, nodePublic, prevLedger, sigGood);
    else if (sigGood && (prevLedger == consensusLCL))
    {
        // relay untrusted proposal
        WriteLog (lsTRACE, Peer) << "relaying untrusted proposal";
        std::set<uint64> peers;
        getApp().getHashRouter ().swapSet (proposal->getHashRouter (), peers, SF_RELAYED);
        PackedMessage::pointer message = boost::make_shared<PackedMessage> (set, protocol::mtPROPOSE_LEDGER);
        getApp().getPeers ().relayMessageBut (peers, message);
    }
    else
        WriteLog (lsDEBUG, Peer) << "Not relaying untrusted proposal";
}

void PeerImp::recvPropose (const boost::shared_ptr<protocol::TMProposeSet>& packet)
{
    assert (packet);
    protocol::TMProposeSet& set = *packet;

    if ((set.currenttxhash ().size () != 32) || (set.nodepubkey ().size () < 28) ||
            (set.signature ().size () < 56) || (set.nodepubkey ().size () > 128) || (set.signature ().size () > 128))
    {
        WriteLog (lsWARNING, Peer) << "Received proposal is malformed";
        charge (Resource::feeInvalidSignature);
        return;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        WriteLog (lsWARNING, Peer) << "Received proposal is malformed";
        charge (Resource::feeInvalidRequest);
        return;
    }

    uint256 proposeHash, prevLedger;
    memcpy (proposeHash.begin (), set.currenttxhash ().data (), 32);

    if (set.has_previousledger ())
        memcpy (prevLedger.begin (), set.previousledger ().data (), 32);

    Serializer s (512);
    s.add256 (proposeHash);
    s.add32 (set.proposeseq ());
    s.add32 (set.closetime ());
    s.addVL (set.nodepubkey ());
    s.addVL (set.signature ());

    if (set.has_previousledger ())
        s.add256 (prevLedger);

    uint256 suppression = s.getSHA512Half ();

    if (! getApp().getHashRouter ().addSuppressionPeer (suppression, mPeerId))
    {
        WriteLog (lsTRACE, Peer) << "Received duplicate proposal from peer " << mPeerId;
        return;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (strCopy (set.nodepubkey ()));

    if (signerPublic == getConfig ().VALIDATION_PUB)
    {
        WriteLog (lsTRACE, Peer) << "Received our own proposal from peer " << mPeerId;
        return;
    }

    bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
    {
        WriteLog (lsDEBUG, Peer) << "Dropping untrusted proposal due to load";
        return;
    }

    WriteLog (lsTRACE, Peer) << "Received " << (isTrusted ? "trusted" : "UNtrusted") << " proposal from " << mPeerId;

    uint256 consensusLCL = getApp().getOPs ().getConsensusLCL ();
    LedgerProposal::pointer proposal = boost::make_shared<LedgerProposal> (
                                           prevLedger.isNonZero () ? prevLedger : consensusLCL,
                                           set.proposeseq (), proposeHash, set.closetime (), signerPublic, suppression);

    getApp().getJobQueue ().addJob (isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut, "recvPropose->checkPropose",
                                   BIND_TYPE (&checkPropose, P_1, packet, proposal, consensusLCL,
                                           mNodePublic, boost::weak_ptr<Peer> (shared_from_this ()), mCluster));
}

void PeerImp::recvHaveTxSet (protocol::TMHaveTransactionSet& packet)
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

    if (!getApp().getOPs ().hasTXSet (shared_from_this (), hash, packet.status ()))
    {
        charge (Resource::feeUnwantedData);
    }
}

static void checkValidation (Job&, SerializedValidation::pointer val, bool isTrusted, bool isCluster,
                             boost::shared_ptr<protocol::TMValidation> packet, boost::weak_ptr<Peer> peer)
{
#ifndef TRUST_NETWORK

    try
#endif
    {
        uint256 signingHash = val->getSigningHash();
        if (!isCluster && !val->isValid (signingHash))
        {
            WriteLog (lsWARNING, Peer) << "Validation is invalid";
            Peer::charge (peer, Resource::feeInvalidRequest);
            return;
        }

        std::string source;
        Peer::pointer lp = peer.lock ();

        if (lp)
            source = lp->getDisplayName ();
        else
            source = "unknown";

        std::set<uint64> peers;

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
            PackedMessage::pointer message = boost::make_shared<PackedMessage> (*packet, protocol::mtVALIDATION);
            getApp().getPeers ().relayMessageBut (peers, message);
        }
    }

#ifndef TRUST_NETWORK
    catch (...)
    {
        WriteLog (lsWARNING, Peer) << "Exception processing validation";
        Peer::charge (peer, Resource::feeInvalidRequest);
    }

#endif
}

void PeerImp::recvValidation (const boost::shared_ptr<protocol::TMValidation>& packet, Application::ScopedLockType& masterLockHolder)
{
    uint32 closeTime = getApp().getOPs().getCloseTimeNC();
    masterLockHolder.unlock ();

    if (packet->validation ().size () < 50)
    {
        WriteLog (lsWARNING, Peer) << "Too small validation from peer";
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
            WriteLog (lsTRACE, Peer) << "Validation is more than two minutes old";
            charge (Resource::feeUnwantedData);
            return;
        }

        if (! getApp().getHashRouter ().addSuppressionPeer (s.getSHA512Half(), mPeerId))
        {
            WriteLog (lsTRACE, Peer) << "Validation is duplicate";
            return;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
            getApp().getJobQueue ().addJob (isTrusted ? jtVALIDATION_t : jtVALIDATION_ut, "recvValidation->checkValidation",
                                       BIND_TYPE (&checkValidation, P_1, val, isTrusted, mCluster, packet,
                                               boost::weak_ptr<Peer> (shared_from_this ())));
        else
            WriteLog(lsDEBUG, Peer) << "Dropping untrusted validation due to load";
    }

#ifndef TRUST_NETWORK
    catch (...)
    {
        WriteLog (lsWARNING, Peer) << "Exception processing validation";
        charge (Resource::feeInvalidRequest);
    }

#endif
}

void PeerImp::recvCluster (protocol::TMCluster& packet)
{
    if (!mCluster)
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
            item.address = IPAddress::from_string (node.name());
            item.balance = node.cost();
            if (item.address != IPAddress())
                gossip.items.push_back(item);
        }
        m_resourceManager.importConsumers (mNodeName, gossip);
    }

    getApp().getFeeTrack().setClusterFee(getApp().getUNL().getClusterFee());
}

void PeerImp::recvGetValidation (protocol::TMGetValidations& packet)
{
}

void PeerImp::recvContact (protocol::TMContact& packet)
{
}

void PeerImp::recvGetContacts (protocol::TMGetContacts& packet)
{
}

// Return a list of your favorite people
// TODO: filter out all the LAN peers
void PeerImp::recvGetPeers (protocol::TMGetPeers& packet, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    std::vector<std::string> addrs;

    getApp().getPeers ().getTopNAddrs (30, addrs);

    if (!addrs.empty ())
    {
        protocol::TMPeers peers;

        for (unsigned int n = 0; n < addrs.size (); n++)
        {
            std::string strIP;
            int         iPort;

            try
            {
                splitIpPort (addrs[n], strIP, iPort);
                // XXX This should also ipv6
                protocol::TMIPv4EndPoint* addr = peers.add_nodes ();
                addr->set_ipv4 (inet_addr (strIP.c_str ()));
                addr->set_ipv4port (iPort);
            }
            catch (...)
            {
                WriteLog (lsWARNING, Peer) << "Bad peer in list: " << addrs[n];
            }



            //WriteLog (lsINFO, Peer) << "Peer: Teaching: " << addressToString(this) << ": " << n << ": " << strIP << " " << iPort;
        }

        PackedMessage::pointer message = boost::make_shared<PackedMessage> (peers, protocol::mtPEERS);
        sendPacket (message, true);
    }
}

// TODO: filter out all the LAN peers
void PeerImp::recvPeers (protocol::TMPeers& packet)
{
    for (int i = 0; i < packet.nodes ().size (); ++i)
    {
        in_addr addr;

        addr.s_addr = packet.nodes (i).ipv4 ();

        {
            IPAddress::V4 v4 (ntohl (addr.s_addr));
            IPAddress ep (v4, packet.nodes (i).ipv4port ());
            getApp().getPeers().getPeerFinder().onPeerLegacyEndpoint (ep);
        }

        std::string strIP (inet_ntoa (addr));
        int         iPort   = packet.nodes (i).ipv4port ();

        if (strIP != "0.0.0.0" && strIP != "127.0.0.1")
        {
            WriteLog (lsDEBUG, Peer) << "Peer: Learning: " << addressToString(this) << ": " << i << ": " << strIP << " " << iPort;

            getApp().getPeers ().savePeer (strIP, iPort, UniqueNodeList::vsTold);
        }
    }
}

void PeerImp::recvEndpoints (protocol::TMEndpoints& packet)
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
            IPAddress::V4 v4 (ntohl (addr.s_addr));
            endpoint.address = IPAddress (v4, tm.ipv4().ipv4port ());
        }
        else
        {
            // This Endpoint describes the peer we are connected to.
            // We will take the remote address seen on the socket and
            // store that in the Endpoint. If this is the first time,
            // then we'll verify that their listener can receive incoming
            // by performing a connectivity test.
            //
            bassert (m_remoteAddressSet);
            endpoint.address = m_remoteAddress.withPort (
                tm.ipv4().ipv4port ());
        }
        
        // slots
        endpoint.incomingSlotsAvailable = tm.slots();

        // maxSlots
        endpoint.incomingSlotsMax = tm.maxslots();

        // uptimeSeconds
        endpoint.uptimeSeconds = tm.uptimeseconds();

        endpoints.push_back (endpoint);
    }

    getApp().getPeers().getPeerFinder().onPeerEndpoints (
        PeerFinder::PeerID (mNodePublic), endpoints);
}

void PeerImp::recvGetObjectByHash (const boost::shared_ptr<protocol::TMGetObjectByHash>& ptr)
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

        WriteLog (lsTRACE, Peer) << "GetObjByHash had " << reply.objects_size () << " of " << packet.objects_size ()
                                 << " for " << getIP ();
        sendPacket (boost::make_shared<PackedMessage> (reply, protocol::mtGET_OBJECTS), true);
    }
    else
    {
        // this is a reply
        uint32 pLSeq = 0;
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
                        CondLog (pLDo && (pLSeq != 0), lsDEBUG, Peer) << "Received full fetch pack for " << pLSeq;
                        pLSeq = obj.ledgerseq ();
                        pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                        if (!pLDo)
                        {
                            WriteLog (lsDEBUG, Peer) << "Got pack for " << pLSeq << " too late";
                        }
                        else
                            progress = true;
                    }
                }

                if (pLDo)
                {
                    uint256 hash;
                    memcpy (hash.begin (), obj.hash ().data (), 256 / 8);

                    boost::shared_ptr< Blob > data = boost::make_shared< Blob >
                                                     (obj.data ().begin (), obj.data ().end ());

                    getApp().getOPs ().addFetchPack (hash, data);
                }
            }
        }

        CondLog (pLDo && (pLSeq != 0), lsDEBUG, Peer) << "Received partial fetch pack for " << pLSeq;

        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            getApp().getOPs ().gotFetchPack (progress, pLSeq);
    }
}

void PeerImp::recvPing (protocol::TMPing& packet)
{
    if (packet.type () == protocol::TMPing::ptPING)
    {
        packet.set_type (protocol::TMPing::ptPONG);
        sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtPING), true);
    }
    else if (packet.type () == protocol::TMPing::ptPONG)
    {
        mActive = 2;
    }
}

void PeerImp::recvErrorMessage (protocol::TMErrorMsg& packet)
{
}

void PeerImp::recvSearchTransaction (protocol::TMSearchTransaction& packet)
{
}

void PeerImp::recvGetAccount (protocol::TMGetAccount& packet)
{
}

void PeerImp::recvAccount (protocol::TMAccount& packet)
{
}

void PeerImp::recvProofWork (protocol::TMProofWork& packet)
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

void PeerImp::recvStatus (protocol::TMStatusChange& packet)
{
    WriteLog (lsTRACE, Peer) << "Received status change from peer " << getIP ();

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
        if (!mClosedLedgerHash.isZero ())
        {
            WriteLog (lsTRACE, Peer) << "peer has lost sync " << getIP ();
            mClosedLedgerHash.zero ();
        }

        mPreviousLedgerHash.zero ();
        return;
    }

    if (packet.has_ledgerhash () && (packet.ledgerhash ().size () == (256 / 8)))
    {
        // a peer has changed ledgers
        memcpy (mClosedLedgerHash.begin (), packet.ledgerhash ().data (), 256 / 8);
        addLedger (mClosedLedgerHash);
        WriteLog (lsTRACE, Peer) << "peer LCL is " << mClosedLedgerHash << " " << getIP ();
    }
    else
    {
        WriteLog (lsTRACE, Peer) << "peer has no ledger hash" << getIP ();
        mClosedLedgerHash.zero ();
    }

    if (packet.has_ledgerhashprevious () && packet.ledgerhashprevious ().size () == (256 / 8))
    {
        memcpy (mPreviousLedgerHash.begin (), packet.ledgerhashprevious ().data (), 256 / 8);
        addLedger (mPreviousLedgerHash);
    }
    else mPreviousLedgerHash.zero ();

    if (packet.has_firstseq () && packet.has_lastseq())
    {
        mMinLedger = packet.firstseq ();
        mMaxLedger = packet.lastseq ();

        // Work around some servers that report sequences incorrectly
        if (mMinLedger == 0)
            mMaxLedger = 0;
        if (mMaxLedger == 0)
            mMinLedger = 0;
    }
}

void PeerImp::recvGetLedger (protocol::TMGetLedger& packet, Application::ScopedLockType& masterLockHolder)
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
        WriteLog (lsTRACE, Peer) << "Received request for TX candidate set data " << getIP ();

        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
        {
            charge (Resource::feeInvalidRequest);
            WriteLog (lsWARNING, Peer) << "invalid request for TX candidate set data";
            return;
        }

        uint256 txHash;
        memcpy (txHash.begin (), packet.ledgerhash ().data (), 32);
        map = getApp().getOPs ().getTXMap (txHash);
        masterLockHolder.unlock();

        if (!map)
        {
            if (packet.has_querytype () && !packet.has_requestcookie ())
            {
                WriteLog (lsDEBUG, Peer) << "Trying to route TX set request";
                std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();
                std::vector<Peer::pointer> usablePeers;
                BOOST_FOREACH (Peer::ref peer, peerList)
                {
                    if (peer->hasTxSet (txHash) && (peer.get () != this))
                        usablePeers.push_back (peer);
                }

                if (usablePeers.empty ())
                {
                    WriteLog (lsINFO, Peer) << "Unable to route TX set request";
                    return;
                }

                Peer::ref selectedPeer = usablePeers[rand () % usablePeers.size ()];
                packet.set_requestcookie (getPeerId ());
                selectedPeer->sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtGET_LEDGER), false);
                return;
            }

            WriteLog (lsERROR, Peer) << "We do not have the map our peer wants " << getIP ();
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
        if (getApp().getFeeTrack().isLoadedLocal() && !mCluster)
        {
            WriteLog (lsDEBUG, Peer) << "Too busy to fetch ledger data";
            return;
        }

        // Figure out what ledger they want
        WriteLog (lsTRACE, Peer) << "Received request for ledger data " << getIP ();
        Ledger::pointer ledger;

        if (packet.has_ledgerhash ())
        {
            uint256 ledgerhash;

            if (packet.ledgerhash ().size () != 32)
            {
                charge (Resource::feeInvalidRequest);
                WriteLog (lsWARNING, Peer) << "Invalid request";
                return;
            }

            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
            logMe += "LedgerHash:";
            logMe += ledgerhash.GetHex ();
            ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

            CondLog (!ledger, lsTRACE, Peer) << "Don't have ledger " << ledgerhash;

            if (!ledger && (packet.has_querytype () && !packet.has_requestcookie ()))
            {
                uint32 seq = 0;

                if (packet.has_ledgerseq ())
                    seq = packet.ledgerseq ();

                std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();
                std::vector<Peer::pointer> usablePeers;
                BOOST_FOREACH (Peer::ref peer, peerList)
                {
                    if (peer->hasLedger (ledgerhash, seq) && (peer.get () != this))
                        usablePeers.push_back (peer);
                }

                if (usablePeers.empty ())
                {
                    WriteLog (lsTRACE, Peer) << "Unable to route ledger request";
                    return;
                }

                Peer::ref selectedPeer = usablePeers[rand () % usablePeers.size ()];
                packet.set_requestcookie (getPeerId ());
                selectedPeer->sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtGET_LEDGER), false);
                WriteLog (lsDEBUG, Peer) << "Ledger request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq ())
        {
            ledger = getApp().getLedgerMaster ().getLedgerBySeq (packet.ledgerseq ());
            CondLog (!ledger, lsDEBUG, Peer) << "Don't have ledger " << packet.ledgerseq ();
        }
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCURRENT))
            ledger = getApp().getLedgerMaster ().getCurrentLedger ();
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCLOSED) )
        {
            ledger = getApp().getLedgerMaster ().getClosedLedger ();

            if (ledger && !ledger->isClosed ())
                ledger = getApp().getLedgerMaster ().getLedgerBySeq (ledger->getLedgerSeq () - 1);
        }
        else
        {
            charge (Resource::feeInvalidRequest);
            WriteLog (lsWARNING, Peer) << "Can't figure out what ledger they want";
            return;
        }

        if ((!ledger) || (packet.has_ledgerseq () && (packet.ledgerseq () != ledger->getLedgerSeq ())))
        {
            charge (Resource::feeInvalidRequest);

            if (ShouldLog (lsWARNING, Peer))
            {
                if (ledger)
                    Log (lsWARNING) << "Ledger has wrong sequence";
            }

            return;
        }

        if (ledger->isImmutable ())
            masterLockHolder.unlock ();
        else
        {
            WriteLog (lsWARNING, Peer) << "Request for data from mutable ledger";
        }

        // Fill out the reply
        uint256 lHash = ledger->getHash ();
        reply.set_ledgerhash (lHash.begin (), lHash.size ());
        reply.set_ledgerseq (ledger->getLedgerSeq ());
        reply.set_type (packet.itype ());

        if (packet.itype () == protocol::liBASE)
        {
            // they want the ledger base data
            WriteLog (lsTRACE, Peer) << "They want ledger base data";
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

            PackedMessage::pointer oPacket = boost::make_shared<PackedMessage> (reply, protocol::mtLEDGER_DATA);
            sendPacket (oPacket, true);
            return;
        }

        if (packet.itype () == protocol::liTX_NODE)
        {
            map = ledger->peekTransactionMap ();
            logMe += " TX:";
            logMe += map->getHash ().GetHex ();
        }
        else if (packet.itype () == protocol::liAS_NODE)
        {
            map = ledger->peekAccountStateMap ();
            logMe += " AS:";
            logMe += map->getHash ().GetHex ();
        }
    }

    if ((!map) || (packet.nodeids_size () == 0))
    {
        WriteLog (lsWARNING, Peer) << "Can't find map or empty request";
        charge (Resource::feeInvalidRequest);
        return;
    }

    WriteLog (lsTRACE, Peer) << "Request: " << logMe;

    for (int i = 0; i < packet.nodeids ().size (); ++i)
    {
        SHAMapNode mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

        if (!mn.isValid ())
        {
            WriteLog (lsWARNING, Peer) << "Request for invalid node: " << logMe;
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
                WriteLog (lsTRACE, Peer) << "getNodeFat got " << rawNodes.size () << " nodes";
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
                WriteLog (lsWARNING, Peer) << "getNodeFat returns false";
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

            WriteLog (lsWARNING, Peer) << "getNodeFat( " << mn << ") throws exception: " << info;
        }
    }

    PackedMessage::pointer oPacket = boost::make_shared<PackedMessage> (reply, protocol::mtLEDGER_DATA);
    sendPacket (oPacket, true);
}

void PeerImp::recvLedger (const boost::shared_ptr<protocol::TMLedgerData>& packet_ptr, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    protocol::TMLedgerData& packet = *packet_ptr;

    if (packet.nodes ().size () <= 0)
    {
        WriteLog (lsWARNING, Peer) << "Ledger/TXset data with no nodes";
        charge (Resource::feeInvalidRequest);
        return;
    }

    if (packet.has_requestcookie ())
    {
        Peer::pointer target = getApp().getPeers ().getPeerById (packet.requestcookie ());

        if (target)
        {
            packet.clear_requestcookie ();
            target->sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtLEDGER_DATA), false);
        }
        else
        {
            WriteLog (lsINFO, Peer) << "Unable to route TX/ledger data reply";
            charge (Resource::feeUnwantedData);
        }

        return;
    }

    uint256 hash;

    if (packet.ledgerhash ().size () != 32)
    {
        WriteLog (lsWARNING, Peer) << "TX candidate reply with invalid hash size";
        charge (Resource::feeInvalidRequest);
        return;
    }

    memcpy (hash.begin (), packet.ledgerhash ().data (), 32);

    if (packet.type () == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set
        std::list<SHAMapNode> nodeIDs;
        std::list< Blob > nodeData;

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata () || (node.nodeid ().size () != 33))
            {
                WriteLog (lsWARNING, Peer) << "LedgerData request with invalid node ID";
                charge (Resource::feeInvalidRequest);
                return;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
        }

        SHAMapAddNode san =  getApp().getOPs ().gotTXData (shared_from_this (), hash, nodeIDs, nodeData);

        if (san.isInvalid ())
        {
            charge (Resource::feeUnwantedData);
        }

        return;
    }

    if (!getApp().getInboundLedgers ().gotLedgerData (hash, shared_from_this(), packet_ptr))
    {
        WriteLog (lsINFO, Peer) << "Got data for unwanted ledger";
        charge (Resource::feeUnwantedData);
    }
}

void PeerImp::ledgerRange (uint32& minSeq, uint32& maxSeq) const
{
    boost::mutex::scoped_lock sl(mRecentLock);

    minSeq = mMinLedger;
    maxSeq = mMaxLedger;
}

bool PeerImp::hasLedger (uint256 const& hash, uint32 seq) const
{
    boost::mutex::scoped_lock sl(mRecentLock);

    if ((seq != 0) && (seq >= mMinLedger) && (seq <= mMaxLedger))
        return true;

    BOOST_FOREACH (uint256 const & ledger, mRecentLedgers)
    {
        if (ledger == hash)
            return true;
    }

    return false;
}

void PeerImp::addLedger (uint256 const& hash)
{
    boost::mutex::scoped_lock sl(mRecentLock);
    BOOST_FOREACH (uint256 const & ledger, mRecentLedgers)
    {
        if (ledger == hash)
            return;
    }

    if (mRecentLedgers.size () == 128)
        mRecentLedgers.pop_front ();

    mRecentLedgers.push_back (hash);
}

bool PeerImp::hasTxSet (uint256 const& hash) const
{
    boost::mutex::scoped_lock sl(mRecentLock);
    BOOST_FOREACH (uint256 const & set, mRecentTxSets)
    {
        if (set == hash)
            return true;
	}

    return false;
}

void PeerImp::addTxSet (uint256 const& hash)
{
    boost::mutex::scoped_lock sl(mRecentLock);
    BOOST_FOREACH (uint256 const & set, mRecentTxSets)
    {
        if (set == hash)
            return;
	}

    if (mRecentTxSets.size () == 128)
        mRecentTxSets.pop_front ();

    mRecentTxSets.push_back (hash);
}

// Get session information we can sign to prevent man in the middle attack.
// (both sides get the same information, neither side controls it)
void PeerImp::getSessionCookie (std::string& strDst)
{
    SSL* ssl (getHandshakeStream ().ssl_handle ());

    if (!ssl) throw std::runtime_error ("No underlying connection");

    // Get both finished messages
    unsigned char s1[1024], s2[1024];
    int l1 = SSL_get_finished (ssl, s1, sizeof (s1));
    int l2 = SSL_get_peer_finished (ssl, s2, sizeof (s2));

    if ((l1 < 12) || (l2 < 12))
        throw std::runtime_error (str (boost::format ("Connection setup not complete: %d %d") % l1 % l2));

    // Hash them and XOR the results
    unsigned char sha1[64], sha2[64];

    SHA512 (s1, l1, sha1);
    SHA512 (s2, l2, sha2);

    if (memcmp (s1, s2, sizeof (sha1)) == 0)
        throw std::runtime_error ("Identical finished messages");

    for (int i = 0; i < sizeof (sha1); ++i)
        sha1[i] ^= sha2[i];

    strDst.assign ((char*) &sha1[0], sizeof (sha1));
}

void PeerImp::sendHello ()
{
    std::string                 strCookie;
    Blob    vchSig;

    getSessionCookie (strCookie);
    mCookieHash = Serializer::getSHA512Half (strCookie);

    getApp().getLocalCredentials ().getNodePrivate ().signNodePrivate (mCookieHash, vchSig);

    protocol::TMHello h;

    h.set_protoversion (BuildInfo::getCurrentProtocol().toPacked ());
    h.set_protoversionmin (BuildInfo::getMinimumProtocol().toPacked ());
    h.set_fullversion (BuildInfo::getFullVersionString ());
    h.set_nettime (getApp().getOPs ().getNetworkTimeNC ());
    h.set_nodepublic (getApp().getLocalCredentials ().getNodePublic ().humanNodePublic ());
    h.set_nodeproof (&vchSig[0], vchSig.size ());
    h.set_ipv4port (getConfig ().peerListeningPort);
    h.set_nodeprivate (getConfig ().PEER_PRIVATE);
    h.set_testnet (getConfig ().TESTNET);

    Ledger::pointer closedLedger = getApp().getLedgerMaster ().getClosedLedger ();

    if (closedLedger && closedLedger->isClosed ())
    {
        uint256 hash = closedLedger->getHash ();
        h.set_ledgerclosed (hash.begin (), hash.GetSerializeSize ());
        hash = closedLedger->getParentHash ();
        h.set_ledgerprevious (hash.begin (), hash.GetSerializeSize ());
    }

    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (h, protocol::mtHELLO);
    sendPacket (packet, true);
}

void PeerImp::sendGetPeers ()
{
    // Ask peer for known other peers.
    protocol::TMGetPeers getPeers;

    getPeers.set_doweneedthis (1);

    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (getPeers, protocol::mtGET_PEERS);

    sendPacket (packet, true);
}

void PeerImp::doProofOfWork (Job&, boost::weak_ptr <Peer> peer, ProofOfWork::pointer pow)
{
    if (peer.expired ())
        return;

    uint256 solution = pow->solve ();

    if (solution.isZero ())
    {
        WriteLog (lsWARNING, Peer) << "Failed to solve proof of work";
    }
    else
    {
        Peer::pointer pptr (peer.lock ());

        if (pptr)
        {
            protocol::TMProofWork reply;
            reply.set_token (pow->getToken ());
            reply.set_response (solution.begin (), solution.size ());
            pptr->sendPacket (boost::make_shared<PackedMessage> (reply, protocol::mtPROOFOFWORK), false);
        }
        else
        {
            // WRITEME: Save solved proof of work for new connection
        }
    }
}

void PeerImp::doFetchPack (const boost::shared_ptr<protocol::TMGetObjectByHash>& packet)
{
    // VFALCO TODO Invert this dependency using an observer and shared state object.
    if (getApp().getFeeTrack ().isLoadedLocal ())
    {
        WriteLog (lsINFO, Peer) << "Too busy to make fetch pack";
        return;
    }

    if (packet->ledgerhash ().size () != 32)
    {
        WriteLog (lsWARNING, Peer) << "FetchPack hash size malformed";
        charge (Resource::feeInvalidRequest);
        return;
    }

    uint256 hash;
    memcpy (hash.begin (), packet->ledgerhash ().data (), 32);

    Ledger::pointer haveLedger = getApp().getOPs ().getLedgerByHash (hash);

    if (!haveLedger)
    {
        WriteLog (lsINFO, Peer) << "Peer requests fetch pack for ledger we don't have: " << hash;
        charge (Resource::feeRequestNoReply);
        return;
    }

    if (!haveLedger->isClosed ())
    {
        WriteLog (lsWARNING, Peer) << "Peer requests fetch pack from open ledger: " << hash;
        charge (Resource::feeInvalidRequest);
        return;
    }

    Ledger::pointer wantLedger = getApp().getOPs ().getLedgerByHash (haveLedger->getParentHash ());

    if (!wantLedger)
    {
        WriteLog (lsINFO, Peer) << "Peer requests fetch pack for ledger whose predecessor we don't have: " << hash;
        charge (Resource::feeRequestNoReply);
        return;
    }

    getApp().getJobQueue ().addJob (jtPACK, "MakeFetchPack",
                                   BIND_TYPE (&NetworkOPs::makeFetchPack, &getApp().getOPs (), P_1,
                                           boost::weak_ptr<Peer> (shared_from_this ()), packet, wantLedger, haveLedger, UptimeTimer::getInstance ().getElapsedSeconds ()));
}

bool PeerImp::hasProto (int version)
{
    return mHello.has_protoversion () && (mHello.protoversion () >= version);
}

Json::Value PeerImp::getJson ()
{
    Json::Value ret (Json::objectValue);

    //ret["this"]           = addressToString(this);
    ret["public_key"]   = mNodePublic.ToString ();
    ret["ip"]           = mIpPortConnect.first;
    //ret["port"]           = mIpPortConnect.second;
    ret["port"]         = mIpPort.second;

    if (m_isInbound)
        ret["inbound"]      = true;

    if (mCluster)
    {
        ret["cluster"]      = true;

        if (!mNodeName.empty ())
            ret["name"]     = mNodeName;
    }

    if (mHello.has_fullversion ())
        ret["version"] = mHello.fullversion ();

    if (mHello.has_protoversion () &&
            (mHello.protoversion () != BuildInfo::getCurrentProtocol().toPacked ()))
    {
        ret["protocol"] = BuildInfo::Protocol (mHello.protoversion ()).toStdString ();
    }

    uint32 minSeq, maxSeq;
    ledgerRange(minSeq, maxSeq);
    if ((minSeq != 0) || (maxSeq != 0))
        ret["complete_ledgers"] = boost::lexical_cast<std::string>(minSeq) + " - " +
                                  boost::lexical_cast<std::string>(maxSeq);

    if (!!mClosedLedgerHash)
        ret["ledger"] = mClosedLedgerHash.GetHex ();

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
            WriteLog (lsWARNING, Peer) << "Peer has unknown status: " << mLastStatus.newstatus ();
        }
    }

    /*
    if (!mIpPort.first.empty())
    {
        ret["verified_ip"]      = mIpPort.first;
        ret["verified_port"]    = mIpPort.second;
    }*/

    return ret;
}

//------------------------------------------------------------------------------

Peer::pointer Peer::New (Resource::Manager& resourceManager,
    boost::asio::io_service& io_service,
        boost::asio::ssl::context& ssl_context, uint64 id,
            bool inbound, bool requirePROXYHandshake)
{
    MultiSocket::Flag flags;

    if (inbound)
    {
        flags = MultiSocket::Flag::server_role | MultiSocket::Flag::ssl_required;

        if (requirePROXYHandshake)
        {
            flags = flags.with (MultiSocket::Flag::proxy);
        }
    }
    else
    {
        flags = MultiSocket::Flag::client_role | MultiSocket::Flag::ssl;

        bassert (! requirePROXYHandshake);
    }

    return Peer::pointer (new PeerImp (resourceManager,
        io_service, ssl_context, id, inbound, flags));
}

//------------------------------------------------------------------------------

void Peer::charge (boost::weak_ptr <Peer>& peer, Resource::Charge const& fee)
{
    Peer::pointer p (peer.lock());
    if (p != nullptr)
        p->charge (fee);
}
