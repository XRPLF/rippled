//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO make this an inline function
#define ADDRESS(p)          strHex(uint64( ((char*) p) - ((char*) 0)))

SETUP_LOG (Peer)

class PeerImp;

// Don't try to run past receiving nonsense from a peer
#define TRUST_NETWORK

// Node has this long to verify its identity from connection accepted or connection attempt.
#define NODE_VERIFY_SECONDS     15

// Idle nodes are probed this often
#define NODE_IDLE_SECONDS       120

class PeerImp : public Peer
    , public CountedObject <PeerImp>
{
public:
    PeerImp (boost::asio::io_service & io_service,
             boost::asio::ssl::context & ctx,
             uint64 peerId,
             bool inbound);

    void handleConnect (const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator it);

    std::string& getIP ()
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

    void setIpPort (const std::string & strIP, int iPort);

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type& getSocket ()
    {
        return mSocketSsl.lowest_layer ();
    }

    void connect (const std::string & strIp, int iPort);
    void connected (const boost::system::error_code & error);
    void detach (const char*, bool onIOStrand);

    // VFALCO Seems no one is using these
    //bool samePeer (Peer::ref p)           { return samePeer(*p); }
    //bool samePeer (const Peer& p)     { return this == &p; }

    void sendPacket (const PackedMessage::pointer & packet, bool onStrand);

    void sendGetPeers ();

    void applyLoadCharge (LoadType);

    Json::Value getJson ();
    bool isConnected () const
    {
        return mHelloed && !mDetaching;
    }
    bool isInbound () const
    {
        return mInbound;
    }
    bool isOutbound () const
    {
        return !mInbound;
    }

    uint256 const& getClosedLedgerHash () const
    {
        return mClosedLedgerHash;
    }
    bool hasLedger (uint256 const & hash, uint32 seq) const;
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

private:
    bool            mInbound;           // Connection is inbound
    bool            mClientConnect;     // In process of connecting as client.
    bool            mHelloed;           // True, if hello accepted.
    bool            mDetaching;         // True, if detaching.
    int             mActive;            // 0=idle, 1=pingsent, 2=active
    bool            mCluster;           // Node in our cluster
    RippleAddress   mNodePublic;        // Node public key of peer.
    std::string     mNodeName;
    ipPort          mIpPort;
    ipPort          mIpPortConnect;
    uint256         mCookieHash;
    uint64          mPeerId;
    bool            mPrivate;           // Keep peer IP private.
    LoadSource      mLoad;
    uint32          mMinLedger, mMaxLedger;

    uint256         mClosedLedgerHash;
    uint256         mPreviousLedgerHash;
    std::list<uint256>  mRecentLedgers;
    std::list<uint256>  mRecentTxSets;

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>      mSocketSsl;

    boost::asio::deadline_timer                                 mActivityTimer;

    boost::asio::io_service::strand     mIOStrand;
    std::vector<uint8_t>                mReadbuf;
    std::list<PackedMessage::pointer>   mSendQ;
    PackedMessage::pointer              mSendingPacket;
    protocol::TMStatusChange              mLastStatus;
    protocol::TMHello                     mHello;

private:
    void handleShutdown (const boost::system::error_code & error)
    {
        ;
    }
    void handleWrite (const boost::system::error_code & error, size_t bytes_transferred);
    void handleReadHeader (const boost::system::error_code & error);
    void handleReadBody (const boost::system::error_code & error);

    void handleStart (const boost::system::error_code & ecResult);
    void handleVerifyTimer (const boost::system::error_code & ecResult);
    void handlePingTimer (const boost::system::error_code & ecResult);

    void processReadBuffer ();
    void startReadHeader ();
    void startReadBody (unsigned msg_len);

    void sendPacketForce (const PackedMessage::pointer & packet);

    void sendHello ();

    void recvHello (protocol::TMHello & packet);
    void recvTransaction (protocol::TMTransaction & packet, ScopedLock & MasterLockHolder);
    void recvValidation (const boost::shared_ptr<protocol::TMValidation>& packet, ScopedLock & MasterLockHolder);
    void recvGetValidation (protocol::TMGetValidations & packet);
    void recvContact (protocol::TMContact & packet);
    void recvGetContacts (protocol::TMGetContacts & packet);
    void recvGetPeers (protocol::TMGetPeers & packet, ScopedLock & MasterLockHolder);
    void recvPeers (protocol::TMPeers & packet);
    void recvGetObjectByHash (const boost::shared_ptr<protocol::TMGetObjectByHash>& packet);
    void recvPing (protocol::TMPing & packet);
    void recvErrorMessage (protocol::TMErrorMsg & packet);
    void recvSearchTransaction (protocol::TMSearchTransaction & packet);
    void recvGetAccount (protocol::TMGetAccount & packet);
    void recvAccount (protocol::TMAccount & packet);
    void recvGetLedger (protocol::TMGetLedger & packet, ScopedLock & MasterLockHolder);
    void recvLedger (const boost::shared_ptr<protocol::TMLedgerData>& packet, ScopedLock & MasterLockHolder);
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

PeerImp::PeerImp (boost::asio::io_service& io_service, boost::asio::ssl::context& ctx, uint64 peerID, bool inbound) :
    mInbound (inbound),
    mHelloed (false),
    mDetaching (false),
    mActive (2),
    mCluster (false),
    mPeerId (peerID),
    mPrivate (false),
    mLoad (std::string()),
    mMinLedger (0),
    mMaxLedger (0),
    mSocketSsl (io_service, ctx),
    mActivityTimer (io_service),
    mIOStrand (io_service)
{
    WriteLog (lsDEBUG, Peer) << "CREATING PEER: " << ADDRESS (this);
}

void PeerImp::handleWrite (const boost::system::error_code& error, size_t bytes_transferred)
{
    // Call on IO strand
#ifdef DEBUG
    //  if (!error)
    //      std::cerr << "PeerImp::handleWrite bytes: "<< bytes_transferred << std::endl;
#endif

    mSendingPacket.reset ();

    if (mDetaching)
    {
        // Ignore write requests when detatching.
        nothing ();
    }
    else if (error)
    {
        WriteLog (lsINFO, Peer) << "Peer: Write: Error: " << ADDRESS (this) << ": bytes=" << bytes_transferred << ": " << error.category ().name () << ": " << error.message () << ": " << error;

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
    mLoad.rename (strIP);

    WriteLog (lsDEBUG, Peer) << "Peer: Set: "
                             << ADDRESS (this) << "> "
                             << (mNodePublic.isValid () ? mNodePublic.humanNodePublic () : "-") << " " << getIP () << " " << getPort ();
}

void PeerImp::detach (const char* rsn, bool onIOStrand)
{
    if (!onIOStrand)
    {
        mIOStrand.post (boost::bind (&Peer::detach, shared_from_this (), rsn, true));
        return;
    }

    if (!mDetaching)
    {
        mDetaching  = true;         // Race is ok.

        CondLog (mCluster, lsWARNING, Peer) << "Cluster peer detach \"" << mNodeName << "\": " << rsn;
        /*
        WriteLog (lsDEBUG, Peer) << "Peer: Detach: "
            << ADDRESS(this) << "> "
            << rsn << ": "
            << (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
            */

        mSendQ.clear ();

        (void) mActivityTimer.cancel ();
        mSocketSsl.async_shutdown (mIOStrand.wrap (boost::bind
                                   (&PeerImp::handleShutdown, boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                    boost::asio::placeholders::error)));

        if (mNodePublic.isValid ())
        {
            theApp->getPeers ().peerDisconnected (shared_from_this (), mNodePublic);

            mNodePublic.clear ();       // Be idempotent.
        }

        if (!mIpPort.first.empty ())
        {
            // Connection might be part of scanning.  Inform connect failed.
            // Might need to scan. Inform connection closed.
            theApp->getPeers ().peerClosed (shared_from_this (), mIpPort.first, mIpPort.second);

            mIpPort.first.clear ();     // Be idempotent.
        }

        /*
        WriteLog (lsDEBUG, Peer) << "Peer: Detach: "
            << ADDRESS(this) << "< "
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
    mActivityTimer.async_wait (mIOStrand.wrap (boost::bind (
                                   &PeerImp::handlePingTimer,
                                   boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                   boost::asio::placeholders::error)));
}


void PeerImp::handleVerifyTimer (const boost::system::error_code& ecResult)
{
    if (ecResult == boost::asio::error::operation_aborted)
    {
        // Timer canceled because deadline no longer needed.
        // std::cerr << "Deadline cancelled." << std::endl;

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

    boost::asio::ip::tcp::resolver::query   query (strIp, boost::lexical_cast<std::string> (iPortAct),
            boost::asio::ip::resolver_query_base::numeric_host | boost::asio::ip::resolver_query_base::numeric_service);
    boost::asio::ip::tcp::resolver              resolver (theApp->getIOService ());
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

        mActivityTimer.async_wait (mIOStrand.wrap (boost::bind (
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
        WriteLog (lsINFO, Peer) << "Peer: Connect: Outbound: " << ADDRESS (this) << ": " << mIpPort.first << " " << mIpPort.second;

        boost::asio::async_connect (
            getSocket (),
            itrEndpoint,
            mIOStrand.wrap (boost::bind (
                                &PeerImp::handleConnect,
                                boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::iterator)));
    }
}

// We have an encrypted connection to the peer.
// Have it say who it is so we know to avoid redundant connections.
// Establish that it really who we are talking to by having it sign a connection detail.
// Also need to establish no man in the middle attack is in progress.
void PeerImp::handleStart (const boost::system::error_code& error)
{
    if (error)
    {
        WriteLog (lsINFO, Peer) << "Peer: Handshake: Error: " << error.category ().name () << ": " << error.message () << ": " << error;
        detach ("hs", true);
    }
    else
    {
        sendHello ();           // Must compute mCookieHash before receiving a hello.
        startReadHeader ();
    }
}

// Connect ssl as client.
void PeerImp::handleConnect (const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it)
{
    if (error)
    {
        WriteLog (lsINFO, Peer) << "Peer: Connect: Error: " << error.category ().name () << ": " << error.message () << ": " << error;
        detach ("hc", true);
    }
    else
    {
        WriteLog (lsINFO, Peer) << "Connect peer: success.";

        mSocketSsl.set_verify_mode (boost::asio::ssl::verify_none);

        mSocketSsl.async_handshake (boost::asio::ssl::stream <boost::asio::ip::tcp::socket>::client,
                                    mIOStrand.wrap (boost::bind (
                                                &PeerImp::handleStart,
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
        ep      = getSocket ().remote_endpoint ();
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

        WriteLog (lsINFO, Peer) << "Peer: Inbound: Accepted: " << ADDRESS (this) << ": " << strIp << " " << iPort;


        mSocketSsl.set_verify_mode (boost::asio::ssl::verify_none);

        mSocketSsl.async_handshake (boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::server,
                                    mIOStrand.wrap (boost::bind (&PeerImp::handleStart, boost::static_pointer_cast <PeerImp> (shared_from_this ()), boost::asio::placeholders::error)));
    }
    else if (!mDetaching)
    {
        WriteLog (lsINFO, Peer) << "Peer: Inbound: Error: " << ADDRESS (this) << ": " << strIp << " " << iPort << " : " << error.category ().name () << ": " << error.message () << ": " << error;

        detach ("ctd", false);
    }
}

void PeerImp::sendPacketForce (const PackedMessage::pointer& packet)
{
    // must be on IO strand
    if (!mDetaching)
    {
        mSendingPacket = packet;

        boost::asio::async_write (mSocketSsl, boost::asio::buffer (packet->getBuffer ()),
                                  mIOStrand.wrap (boost::bind (&PeerImp::handleWrite,
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
            mIOStrand.post (boost::bind (&Peer::sendPacket, shared_from_this (), packet, true));
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

        boost::asio::async_read (mSocketSsl,
                                 boost::asio::buffer (mReadbuf),
                                 mIOStrand.wrap (boost::bind (&PeerImp::handleReadHeader,
                                         boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                         boost::asio::placeholders::error)));
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

        boost::asio::async_read (mSocketSsl,
                                 boost::asio::buffer (&mReadbuf [PackedMessage::kHeaderBytes], msg_len),
                                 mIOStrand.wrap (boost::bind (&PeerImp::handleReadBody,
                                         boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                                         boost::asio::placeholders::error)));
    }
}

void PeerImp::handleReadHeader (const boost::system::error_code& error)
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

void PeerImp::handleReadBody (const boost::system::error_code& error)
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

        boost::recursive_mutex::scoped_lock sl (theApp->getMasterLock ());
        detach ("hrb", true);
        return;
    }

    processReadBuffer ();
    startReadHeader ();
}

void PeerImp::processReadBuffer ()
{
    // must not hold peer lock
    int type = PackedMessage::getType (mReadbuf);
#ifdef DEBUG
    //  std::cerr << "PRB(" << type << "), len=" << (mReadbuf.size()-PackedMessage::kHeaderBytes) << std::endl;
#endif

    //  std::cerr << "PeerImp::processReadBuffer: " << mIpPort.first << " " << mIpPort.second << std::endl;

    LoadEvent::autoptr event (theApp->getJobQueue ().getLoadEventAP (jtPEER, "PeerImp::read"));

    ScopedLock sl (theApp->getMasterLock ());

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
            event->reName ("PeerImp::hello");
            protocol::TMHello msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvHello (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtERROR_MSG:
        {
            event->reName ("PeerImp::errormessage");
            protocol::TMErrorMsg msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvErrorMessage (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtPING:
        {
            event->reName ("PeerImp::ping");
            protocol::TMPing msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvPing (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtGET_CONTACTS:
        {
            event->reName ("PeerImp::getcontacts");
            protocol::TMGetContacts msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvGetContacts (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtCONTACT:
        {
            event->reName ("PeerImp::contact");
            protocol::TMContact msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvContact (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtGET_PEERS:
        {
            event->reName ("PeerImp::getpeers");
            protocol::TMGetPeers msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvGetPeers (msg, sl);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtPEERS:
        {
            event->reName ("PeerImp::peers");
            protocol::TMPeers msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvPeers (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtSEARCH_TRANSACTION:
        {
            event->reName ("PeerImp::searchtransaction");
            protocol::TMSearchTransaction msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvSearchTransaction (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtGET_ACCOUNT:
        {
            event->reName ("PeerImp::getaccount");
            protocol::TMGetAccount msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvGetAccount (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtACCOUNT:
        {
            event->reName ("PeerImp::account");
            protocol::TMAccount msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvAccount (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtTRANSACTION:
        {
            event->reName ("PeerImp::transaction");
            protocol::TMTransaction msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvTransaction (msg, sl);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtSTATUS_CHANGE:
        {
            event->reName ("PeerImp::statuschange");
            protocol::TMStatusChange msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvStatus (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtPROPOSE_LEDGER:
        {
            event->reName ("PeerImp::propose");
            boost::shared_ptr<protocol::TMProposeSet> msg = boost::make_shared<protocol::TMProposeSet> ();

            if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvPropose (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtGET_LEDGER:
        {
            event->reName ("PeerImp::getledger");
            protocol::TMGetLedger msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvGetLedger (msg, sl);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtLEDGER_DATA:
        {
            event->reName ("PeerImp::ledgerdata");
            boost::shared_ptr<protocol::TMLedgerData> msg = boost::make_shared<protocol::TMLedgerData> ();

            if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvLedger (msg, sl);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtHAVE_SET:
        {
            event->reName ("PeerImp::haveset");
            protocol::TMHaveTransactionSet msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvHaveTxSet (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtVALIDATION:
        {
            event->reName ("PeerImp::validation");
            boost::shared_ptr<protocol::TMValidation> msg = boost::make_shared<protocol::TMValidation> ();

            if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvValidation (msg, sl);
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
            event->reName ("PeerImp::getobjects");
            boost::shared_ptr<protocol::TMGetObjectByHash> msg = boost::make_shared<protocol::TMGetObjectByHash> ();

            if (msg->ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvGetObjectByHash (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;

        case protocol::mtPROOFOFWORK:
        {
            event->reName ("PeerImp::proofofwork");
            protocol::TMProofWork msg;

            if (msg.ParseFromArray (&mReadbuf[PackedMessage::kHeaderBytes], mReadbuf.size () - PackedMessage::kHeaderBytes))
                recvProofWork (msg);
            else
                WriteLog (lsWARNING, Peer) << "parse error: " << type;
        }
        break;


        default:
            event->reName ("PeerImp::unknown");
            if (type != 5)
            { // TEMPORARY: Don't warn on cluster message
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
    mActivityTimer.async_wait (mIOStrand.wrap (boost::bind (&PeerImp::handlePingTimer, boost::static_pointer_cast <PeerImp> (shared_from_this ()),
                               boost::asio::placeholders::error)));

    uint32 ourTime = theApp->getOPs ().getNetworkTimeNC ();
    uint32 minTime = ourTime - 20;
    uint32 maxTime = ourTime + 20;

#ifdef DEBUG

    if (packet.has_nettime ())
    {
        int64 to = ourTime;
        to -= packet.nettime ();
        WriteLog (lsDEBUG, Peer) << "Connect: time offset " << to;
    }

#endif

    if ((packet.has_testnet () && packet.testnet ()) != theConfig.TESTNET)
    {
        // Format: actual/requested.
        WriteLog (lsINFO, Peer) << boost::str (boost::format ("Recv(Hello): Network mismatch: %d/%d")
                                               % packet.testnet ()
                                               % theConfig.TESTNET);
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
    else if (packet.protoversionmin () > MAKE_VERSION_INT (PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR))
    {
        WriteLog (lsINFO, Peer) << "Recv(Hello): Server requires protocol version " <<
                                GET_VERSION_MAJOR (packet.protoversion ()) << "." << GET_VERSION_MINOR (packet.protoversion ())
                                << " we run " << PROTO_VERSION_MAJOR << "." << PROTO_VERSION_MINOR;
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
        CondLog (packet.protoversion () != MAKE_VERSION_INT (PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR), lsINFO, Peer)
                << "Peer speaks version " <<
                (packet.protoversion () >> 16) << "." << (packet.protoversion () & 0xFF);
        mHello = packet;

        if (theApp->getUNL ().nodeInCluster (mNodePublic, mNodeName))
        {
            mCluster = true;
            mLoad.setPrivileged ();
            if (!mNodeName.empty())
                mLoad.rename (mNodeName);
            WriteLog (lsINFO, Peer) << "Cluster connection to \"" << (mNodeName.empty () ? getIP () : mNodeName)
                                    << "\" established";
        }

        if (isOutbound ())
            mLoad.setOutbound ();

        if (mClientConnect)
        {
            // If we connected due to scan, no longer need to scan.
            theApp->getPeers ().peerVerified (shared_from_this ());
        }

        if (! theApp->getPeers ().peerConnected (shared_from_this (), mNodePublic, getIP (), getPort ()))
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
                // Take a guess at remotes address.
                std::string strIP   = getSocket ().remote_endpoint ().address ().to_string ();
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
                    theApp->getPeers ().savePeer (strIP, iPort, IUniqueNodeList::vsInbound);
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
            theApp->getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            Peer::applyLoadCharge (peer, LT_InvalidSignature);
            return;
        }
        else
            theApp->getHashRouter ().setFlag (stx->getTransactionID (), SF_SIGGOOD);

        theApp->getOPs ().processTransaction (tx, isSetBit (flags, SF_TRUSTED), false);

#ifndef TRUST_NETWORK
    }
    catch (...)
    {
        theApp->getHashRouter ().setFlags (stx->getTransactionID (), SF_BAD);
        applyLoadCharge (peer, LT_InvalidRequest);
    }

#endif
}

void PeerImp::recvTransaction (protocol::TMTransaction& packet, ScopedLock& MasterLockHolder)
{
    MasterLockHolder.unlock ();
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

        if (! theApp->getHashRouter ().addSuppressionPeer (txID, mPeerId, flags))
        {
            // we have seen this transaction recently
            if (isSetBit (flags, SF_BAD))
            {
                applyLoadCharge (LT_InvalidSignature);
                return;
            }

            if (!isSetBit (flags, SF_RETRY))
                return;
        }

        if (theApp->getMasterTransaction().fetch(txID, true))
        {
            WriteLog (lsDEBUG, Peer) << "Peer " << getDisplayName() << " send old TX " << txID;
            applyLoadCharge (LT_InvalidRequest);
            return;
        }

        WriteLog (lsDEBUG, Peer) << "Got new transaction from peer " << getDisplayName () << " : " << txID;

        if (mCluster)
            flags |= SF_TRUSTED | SF_SIGGOOD;

        if (theApp->getJobQueue().getJobCount(jtTRANSACTION) > 100)
            WriteLog(lsINFO, Peer) << "Transaction queue is full";
        else if (theApp->getLedgerMaster().getValidatedLedgerAge() > 240)
            WriteLog(lsINFO, Peer) << "No new transactions until synchronized";
        else
            theApp->getJobQueue ().addJob (jtTRANSACTION, "recvTransction->checkTransaction",
                                       BIND_TYPE (&checkTransaction, P_1, flags, stx, boost::weak_ptr<Peer> (shared_from_this ())));

#ifndef TRUST_NETWORK
    }
    catch (...)
    {
#ifdef DEBUG
        std::cerr << "Transaction from peer fails validity tests" << std::endl;
        Json::StyledStreamWriter w;
        w.write (std::cerr, tx->getJson (0));
#endif
        return;
    }

#endif

}

// Called from our JobQueue
static void checkPropose (Job& job, boost::shared_ptr<protocol::TMProposeSet> packet,
                          LedgerProposal::pointer proposal, uint256 consensusLCL, RippleAddress nodePublic, boost::weak_ptr<Peer> peer)
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

        if (!proposal->checkSign (set.signature ()))
        {
            Peer::pointer p = peer.lock ();
            WriteLog (lsWARNING, Peer) << "proposal with previous ledger fails signature check: " <<
                                       (p ? p->getIP () : std::string ("???"));
            Peer::applyLoadCharge (peer, LT_InvalidSignature);
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
        theApp->getOPs ().processTrustedProposal (proposal, packet, nodePublic, prevLedger, sigGood);
    else if (sigGood && (prevLedger == consensusLCL))
    {
        // relay untrusted proposal
        WriteLog (lsTRACE, Peer) << "relaying untrusted proposal";
        std::set<uint64> peers;
        theApp->getHashRouter ().swapSet (proposal->getHashRouter (), peers, SF_RELAYED);
        PackedMessage::pointer message = boost::make_shared<PackedMessage> (set, protocol::mtPROPOSE_LEDGER);
        theApp->getPeers ().relayMessageBut (peers, message);
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
        applyLoadCharge (LT_InvalidSignature);
        return;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        WriteLog (lsWARNING, Peer) << "Received proposal is malformed";
        applyLoadCharge (LT_InvalidRequest);
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

    if (! theApp->getHashRouter ().addSuppressionPeer (suppression, mPeerId))
    {
        WriteLog (lsTRACE, Peer) << "Received duplicate proposal from peer " << mPeerId;
        return;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (strCopy (set.nodepubkey ()));

    if (signerPublic == theConfig.VALIDATION_PUB)
    {
        WriteLog (lsTRACE, Peer) << "Received our own proposal from peer " << mPeerId;
        return;
    }

    bool isTrusted = theApp->getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && theApp->getFeeTrack ().isLoaded ())
    {
        WriteLog (lsDEBUG, Peer) << "Dropping untrusted proposal due to load";
        return;
    }

    WriteLog (lsTRACE, Peer) << "Received " << (isTrusted ? "trusted" : "UNtrusted") << " proposal from " << mPeerId;

    uint256 consensusLCL = theApp->getOPs ().getConsensusLCL ();
    LedgerProposal::pointer proposal = boost::make_shared<LedgerProposal> (
                                           prevLedger.isNonZero () ? prevLedger : consensusLCL,
                                           set.proposeseq (), proposeHash, set.closetime (), signerPublic, suppression);

    theApp->getJobQueue ().addJob (isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut, "recvPropose->checkPropose",
                                   BIND_TYPE (&checkPropose, P_1, packet, proposal, consensusLCL,
                                           mNodePublic, boost::weak_ptr<Peer> (shared_from_this ())));
}

void PeerImp::recvHaveTxSet (protocol::TMHaveTransactionSet& packet)
{
    uint256 hashes;

    if (packet.hash ().size () != (256 / 8))
    {
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

    uint256 hash;
    memcpy (hash.begin (), packet.hash ().data (), 32);

    if (packet.status () == protocol::tsHAVE)
        addTxSet (hash);

    if (!theApp->getOPs ().hasTXSet (shared_from_this (), hash, packet.status ()))
        applyLoadCharge (LT_UnwantedData);
}

static void checkValidation (Job&, SerializedValidation::pointer val, uint256 signingHash,
                             bool isTrusted, bool isCluster, boost::shared_ptr<protocol::TMValidation> packet, boost::weak_ptr<Peer> peer)
{
#ifndef TRUST_NETWORK

    try
#endif
    {
        if (!isCluster && !val->isValid (signingHash))
        {
            WriteLog (lsWARNING, Peer) << "Validation is invalid";
            Peer::applyLoadCharge (peer, LT_InvalidRequest);
            return;
        }

        std::string source;
        Peer::pointer lp = peer.lock ();

        if (lp)
            source = lp->getDisplayName ();
        else
            source = "unknown";

        std::set<uint64> peers;

        if (theApp->getOPs ().recvValidation (val, source) &&
                theApp->getHashRouter ().swapSet (signingHash, peers, SF_RELAYED))
        {
            PackedMessage::pointer message = boost::make_shared<PackedMessage> (*packet, protocol::mtVALIDATION);
            theApp->getPeers ().relayMessageBut (peers, message);
        }
    }

#ifndef TRUST_NETWORK
    catch (...)
    {
        WriteLog (lsWARNING, Peer) << "Exception processing validation";
        Peer::applyLoadCharge (peer, LT_InvalidRequest);
    }

#endif
}

void PeerImp::recvValidation (const boost::shared_ptr<protocol::TMValidation>& packet, ScopedLock& MasterLockHolder)
{
    MasterLockHolder.unlock ();

    if (packet->validation ().size () < 50)
    {
        WriteLog (lsWARNING, Peer) << "Too small validation from peer";
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

#ifndef TRUST_NETWORK

    try
#endif
    {
        Serializer s (packet->validation ());
        SerializerIterator sit (s);
        SerializedValidation::pointer val = boost::make_shared<SerializedValidation> (boost::ref (sit), false);

        uint256 signingHash = val->getSigningHash ();

        if (! theApp->getHashRouter ().addSuppressionPeer (signingHash, mPeerId))
        {
            WriteLog (lsTRACE, Peer) << "Validation is duplicate";
            return;
        }

        bool isTrusted = theApp->getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !theApp->getFeeTrack ().isLoaded ())
	        theApp->getJobQueue ().addJob (isTrusted ? jtVALIDATION_t : jtVALIDATION_ut, "recvValidation->checkValidation",
                                       BIND_TYPE (&checkValidation, P_1, val, signingHash, isTrusted, mCluster, packet,
                                               boost::weak_ptr<Peer> (shared_from_this ())));
	else
	    WriteLog(lsDEBUG, Peer) << "Dropping untrusted validation due to load";
    }

#ifndef TRUST_NETWORK
    catch (...)
    {
        WriteLog (lsWARNING, Peer) << "Exception processing validation";
        applyLoadCharge (LT_InvalidRequest);
    }

#endif
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
// TODO: filter out the peer you are talking to
void PeerImp::recvGetPeers (protocol::TMGetPeers& packet, ScopedLock& MasterLockHolder)
{
    MasterLockHolder.unlock ();
    std::vector<std::string> addrs;

    theApp->getPeers ().getTopNAddrs (30, addrs);

    if (!addrs.empty ())
    {
        protocol::TMPeers peers;

        for (unsigned int n = 0; n < addrs.size (); n++)
        {
            std::string strIP;
            int         iPort;

            splitIpPort (addrs[n], strIP, iPort);

            // XXX This should also ipv6
            protocol::TMIPv4EndPoint* addr = peers.add_nodes ();
            addr->set_ipv4 (inet_addr (strIP.c_str ()));
            addr->set_ipv4port (iPort);

            //WriteLog (lsINFO, Peer) << "Peer: Teaching: " << ADDRESS(this) << ": " << n << ": " << strIP << " " << iPort;
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

        std::string strIP (inet_ntoa (addr));
        int         iPort   = packet.nodes (i).ipv4port ();

        if (strIP != "0.0.0.0" && strIP != "127.0.0.1")
        {
            //WriteLog (lsINFO, Peer) << "Peer: Learning: " << ADDRESS(this) << ": " << i << ": " << strIP << " " << iPort;

            theApp->getPeers ().savePeer (strIP, iPort, IUniqueNodeList::vsTold);
        }
    }
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
                HashedObject::pointer hObj = theApp->getHashedObjectStore ().retrieve (hash);

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
                        CondLog (pLDo && (pLSeq != 0), lsDEBUG, Peer) << "Recevied full fetch pack for " << pLSeq;
                        pLSeq = obj.ledgerseq ();
                        pLDo = !theApp->getOPs ().haveLedger (pLSeq);

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

                    theApp->getOPs ().addFetchPack (hash, data);
                }
            }
        }

        CondLog (pLDo && (pLSeq != 0), lsDEBUG, Peer) << "Received partial fetch pack for " << pLSeq;

        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            theApp->getOPs ().gotFetchPack (progress, pLSeq);
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
            applyLoadCharge (LT_InvalidRequest);
            return;
        }

        uint256 response;
        memcpy (response.begin (), packet.response ().data (), 256 / 8);
        POWResult r = theApp->getProofOfWorkFactory ().checkProof (packet.token (), response);

        if (r == powOK)
        {
            // credit peer
            // WRITEME
            return;
        }

        // return error message
        // WRITEME
        if (r != powTOOEASY)
            applyLoadCharge (LT_BadPoW);

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
            applyLoadCharge (LT_InvalidRequest);
            return;
        }

        memcpy (challenge.begin (), packet.challenge ().data (), 256 / 8);
        memcpy (target.begin (), packet.target ().data (), 256 / 8);
        ProofOfWork::pointer pow = boost::make_shared<ProofOfWork> (packet.token (), packet.iterations (),
                                   challenge, target);

        if (!pow->isValid ())
        {
            applyLoadCharge (LT_InvalidRequest);
            return;
        }

#if 0   // Until proof of work is completed, don't do it
        theApp->getJobQueue ().addJob (
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
        packet.set_networktime (theApp->getOPs ().getNetworkTimeNC ());

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

    if (packet.has_firstseq ())
        mMinLedger = packet.firstseq ();

    if (packet.has_lastseq ())
        mMaxLedger = packet.lastseq ();
}

void PeerImp::recvGetLedger (protocol::TMGetLedger& packet, ScopedLock& MasterLockHolder)
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
        WriteLog (lsDEBUG, Peer) << "Received request for TX candidate set data " << getIP ();

        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
        {
            applyLoadCharge (LT_InvalidRequest);
            WriteLog (lsWARNING, Peer) << "invalid request for TX candidate set data";
            return;
        }

        uint256 txHash;
        memcpy (txHash.begin (), packet.ledgerhash ().data (), 32);
        map = theApp->getOPs ().getTXMap (txHash);

        if (!map)
        {
            if (packet.has_querytype () && !packet.has_requestcookie ())
            {
                WriteLog (lsDEBUG, Peer) << "Trying to route TX set request";
                std::vector<Peer::pointer> peerList = theApp->getPeers ().getPeerVector ();
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
            applyLoadCharge (LT_InvalidRequest);
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
        // Figure out what ledger they want
        WriteLog (lsTRACE, Peer) << "Received request for ledger data " << getIP ();
        Ledger::pointer ledger;

        if (packet.has_ledgerhash ())
        {
            uint256 ledgerhash;

            if (packet.ledgerhash ().size () != 32)
            {
                applyLoadCharge (LT_InvalidRequest);
                WriteLog (lsWARNING, Peer) << "Invalid request";
                return;
            }

            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
            logMe += "LedgerHash:";
            logMe += ledgerhash.GetHex ();
            ledger = theApp->getLedgerMaster ().getLedgerByHash (ledgerhash);

            CondLog (!ledger, lsTRACE, Peer) << "Don't have ledger " << ledgerhash;

            if (!ledger && (packet.has_querytype () && !packet.has_requestcookie ()))
            {
                uint32 seq = 0;

                if (packet.has_ledgerseq ())
                    seq = packet.ledgerseq ();

                std::vector<Peer::pointer> peerList = theApp->getPeers ().getPeerVector ();
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
            ledger = theApp->getLedgerMaster ().getLedgerBySeq (packet.ledgerseq ());
            CondLog (!ledger, lsDEBUG, Peer) << "Don't have ledger " << packet.ledgerseq ();
        }
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCURRENT))
            ledger = theApp->getLedgerMaster ().getCurrentLedger ();
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCLOSED) )
        {
            ledger = theApp->getLedgerMaster ().getClosedLedger ();

            if (ledger && !ledger->isClosed ())
                ledger = theApp->getLedgerMaster ().getLedgerBySeq (ledger->getLedgerSeq () - 1);
        }
        else
        {
            applyLoadCharge (LT_InvalidRequest);
            WriteLog (lsWARNING, Peer) << "Can't figure out what ledger they want";
            return;
        }

        if ((!ledger) || (packet.has_ledgerseq () && (packet.ledgerseq () != ledger->getLedgerSeq ())))
        {
            applyLoadCharge (LT_InvalidRequest);

            if (ShouldLog (lsWARNING, Peer))
            {
                if (ledger)
                    Log (lsWARNING) << "Ledger has wrong sequence";
            }

            return;
        }

        if (ledger->isImmutable ())
            MasterLockHolder.unlock ();
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
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

    WriteLog (lsTRACE, Peer) << "Request: " << logMe;

    for (int i = 0; i < packet.nodeids ().size (); ++i)
    {
        SHAMapNode mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

        if (!mn.isValid ())
        {
            WriteLog (lsWARNING, Peer) << "Request for invalid node: " << logMe;
            applyLoadCharge (LT_InvalidRequest);
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

void PeerImp::recvLedger (const boost::shared_ptr<protocol::TMLedgerData>& packet_ptr, ScopedLock& MasterLockHolder)
{
    MasterLockHolder.unlock ();
    protocol::TMLedgerData& packet = *packet_ptr;

    if (packet.nodes ().size () <= 0)
    {
        WriteLog (lsWARNING, Peer) << "Ledger/TXset data with no nodes";
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

    if (packet.has_requestcookie ())
    {
        Peer::pointer target = theApp->getPeers ().getPeerById (packet.requestcookie ());

        if (target)
        {
            packet.clear_requestcookie ();
            target->sendPacket (boost::make_shared<PackedMessage> (packet, protocol::mtLEDGER_DATA), false);
        }
        else
        {
            WriteLog (lsINFO, Peer) << "Unable to route TX/ledger data reply";
            applyLoadCharge (LT_UnwantedData);
        }

        return;
    }

    uint256 hash;

    if (packet.ledgerhash ().size () != 32)
    {
        WriteLog (lsWARNING, Peer) << "TX candidate reply with invalid hash size";
        applyLoadCharge (LT_InvalidRequest);
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
                applyLoadCharge (LT_InvalidRequest);
                return;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
        }

        SHAMapAddNode san =  theApp->getOPs ().gotTXData (shared_from_this (), hash, nodeIDs, nodeData);

        if (san.isInvalid ())
            applyLoadCharge (LT_UnwantedData);

        return;
    }

    if (theApp->getInboundLedgers ().awaitLedgerData (hash))
        theApp->getJobQueue ().addJob (jtLEDGER_DATA, "gotLedgerData",
                                       BIND_TYPE (&InboundLedgers::gotLedgerData, &theApp->getInboundLedgers (),
                                               P_1, hash, packet_ptr, boost::weak_ptr<Peer> (shared_from_this ())));
    else
        applyLoadCharge (LT_UnwantedData);
}

bool PeerImp::hasLedger (uint256 const& hash, uint32 seq) const
{
    if ((seq != 0) && (seq >= mMinLedger) && (seq <= mMaxLedger))
        return true;

    BOOST_FOREACH (uint256 const & ledger, mRecentLedgers)

    if (ledger == hash)
        return true;

    return false;
}

void PeerImp::addLedger (uint256 const& hash)
{
    BOOST_FOREACH (uint256 const & ledger, mRecentLedgers)

    if (ledger == hash)
        return;

    if (mRecentLedgers.size () == 128)
        mRecentLedgers.pop_front ();

    mRecentLedgers.push_back (hash);
}

bool PeerImp::hasTxSet (uint256 const& hash) const
{
    BOOST_FOREACH (uint256 const & set, mRecentTxSets)

    if (set == hash)
        return true;

    return false;
}

void PeerImp::addTxSet (uint256 const& hash)
{
    BOOST_FOREACH (uint256 const & set, mRecentTxSets)

    if (set == hash)
        return;

    if (mRecentTxSets.size () == 128)
        mRecentTxSets.pop_front ();

    mRecentTxSets.push_back (hash);
}

// Get session information we can sign to prevent man in the middle attack.
// (both sides get the same information, neither side controls it)
void PeerImp::getSessionCookie (std::string& strDst)
{
    SSL* ssl = mSocketSsl.native_handle ();

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

    theApp->getLocalCredentials ().getNodePrivate ().signNodePrivate (mCookieHash, vchSig);

    protocol::TMHello h;

    h.set_protoversion (MAKE_VERSION_INT (PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR));
    h.set_protoversionmin (MAKE_VERSION_INT (MIN_PROTO_MAJOR, MIN_PROTO_MINOR));
    h.set_fullversion (SERVER_VERSION);
    h.set_nettime (theApp->getOPs ().getNetworkTimeNC ());
    h.set_nodepublic (theApp->getLocalCredentials ().getNodePublic ().humanNodePublic ());
    h.set_nodeproof (&vchSig[0], vchSig.size ());
    h.set_ipv4port (theConfig.PEER_PORT);
    h.set_nodeprivate (theConfig.PEER_PRIVATE);
    h.set_testnet (theConfig.TESTNET);

    Ledger::pointer closedLedger = theApp->getLedgerMaster ().getClosedLedger ();

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

void PeerImp::applyLoadCharge (LoadType loadType)
{
    // IMPLEMENATION IS INCOMPLETE

    // VFALCO TODO This needs to implemented before open sourcing.

    if (theApp->getLoadManager ().applyLoadCharge (mLoad, loadType))
    {
        if (mCluster)
        {
            WriteLog (lsWARNING, Peer) << "aLC: " << getDisplayName() << " load from cluster";
        }
        else if (theApp->getLoadManager ().shouldCutoff(mLoad))
        {
            WriteLog (lsWARNING, Peer) << "aLC: " << getDisplayName() << " should cutoff";
        }
        else if (theApp->getLoadManager ().shouldWarn (mLoad))
        {
            WriteLog (lsWARNING, Peer) << "aLC: " << getDisplayName() << " load warning";
        }
        else
        {
            WriteLog (lsWARNING, Peer) << "aLC: " << getDisplayName() << " cannot figure out";
        }
    }
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
    if (theApp->getFeeTrack ().isLoaded ())
    {
        WriteLog (lsINFO, Peer) << "Too busy to make fetch pack";
        return;
    }

    if (packet->ledgerhash ().size () != 32)
    {
        WriteLog (lsWARNING, Peer) << "FetchPack hash size malformed";
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

    uint256 hash;
    memcpy (hash.begin (), packet->ledgerhash ().data (), 32);

    Ledger::pointer haveLedger = theApp->getOPs ().getLedgerByHash (hash);

    if (!haveLedger)
    {
        WriteLog (lsINFO, Peer) << "Peer requests fetch pack for ledger we don't have: " << hash;
        applyLoadCharge (LT_RequestNoReply);
        return;
    }

    if (!haveLedger->isClosed ())
    {
        WriteLog (lsWARNING, Peer) << "Peer requests fetch pack from open ledger: " << hash;
        applyLoadCharge (LT_InvalidRequest);
        return;
    }

    Ledger::pointer wantLedger = theApp->getOPs ().getLedgerByHash (haveLedger->getParentHash ());

    if (!wantLedger)
    {
        WriteLog (lsINFO, Peer) << "Peer requests fetch pack for ledger whose predecessor we don't have: " << hash;
        applyLoadCharge (LT_RequestNoReply);
        return;
    }

    theApp->getJobQueue ().addJob (jtPACK, "MakeFetchPack",
                                   BIND_TYPE (&NetworkOPs::makeFetchPack, &theApp->getOPs (), P_1,
                                           boost::weak_ptr<Peer> (shared_from_this ()), packet, wantLedger, haveLedger, UptimeTimer::getInstance ().getElapsedSeconds ()));
}

bool PeerImp::hasProto (int version)
{
    return mHello.has_protoversion () && (mHello.protoversion () >= version);
}

Json::Value PeerImp::getJson ()
{
    Json::Value ret (Json::objectValue);

    //ret["this"]           = ADDRESS(this);
    ret["public_key"]   = mNodePublic.ToString ();
    ret["ip"]           = mIpPortConnect.first;
    //ret["port"]           = mIpPortConnect.second;
    ret["port"]         = mIpPort.second;

    if (mInbound)
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
            (mHello.protoversion () != MAKE_VERSION_INT (PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR)))
        ret["protocol"] = boost::lexical_cast<std::string> (GET_VERSION_MAJOR (mHello.protoversion ())) + "." +
                          boost::lexical_cast<std::string> (GET_VERSION_MINOR (mHello.protoversion ()));

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

Peer::pointer Peer::New (boost::asio::io_service& io_service,
                         boost::asio::ssl::context& ctx,
                         uint64 id,
                         bool inbound)
{
    return Peer::pointer (new PeerImp (io_service, ctx, id, inbound));
}

void Peer::applyLoadCharge (const boost::weak_ptr<Peer>& wp, LoadType l)
{
    Peer::pointer p = wp.lock ();

    if (p)
        p->applyLoadCharge (l);
}

// vim:ts=4
