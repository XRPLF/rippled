//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (Peers)

class PeersImp
    : public Peers
    , public Stoppable
    , public LeakChecked <PeersImp>
{
public:
    enum
    {
        /** Frequency of policy enforcement.
        */
        policyIntervalSeconds = 5
    };

    PeersImp (Stoppable& parent,
        boost::asio::io_service& io_service,
            boost::asio::ssl::context& ssl_context)
        : Stoppable ("Peers", parent)
        , m_io_service (io_service)
        , m_ssl_context (ssl_context)
        , mPeerLock (this, "PeersImp", __FILE__, __LINE__)
        , mLastPeer (0)
        , mPhase (0)
        , mScanTimer (io_service)
        , mPolicyTimer (io_service)
    {
    }

    // Begin enforcing connection policy.
    void start ();

    // Send message to network.
    int relayMessage (Peer* fromPeer, const PackedMessage::pointer& msg);
    int relayMessageCluster (Peer* fromPeer, const PackedMessage::pointer& msg);
    void relayMessageTo (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg);
    void relayMessageBut (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg);

    // Manual connection request.
    // Queue for immediate scanning.
    void connectTo (const std::string& strIp, int iPort);

    //
    // Peer connectivity notification.
    //
    bool getTopNAddrs (int n, std::vector<std::string>& addrs);
    bool savePeer (const std::string& strIp, int iPort, char code);

    // We know peers node public key.
    // <-- bool: false=reject
    bool peerConnected (Peer::ref peer, const RippleAddress& naPeer, const std::string& strIP, int iPort);

    // No longer connected.
    void peerDisconnected (Peer::ref peer, const RippleAddress& naPeer);

    // As client accepted.
    void peerVerified (Peer::ref peer);

    // As client failed connect and be accepted.
    void peerClosed (Peer::ref peer, const std::string& strIp, int iPort);

    int getPeerCount ();
    Json::Value getPeersJson ();
    std::vector<Peer::pointer> getPeerVector ();

    // Peer 64-bit ID function
    uint64 assignPeerId ();
    Peer::pointer getPeerById (const uint64& id);
    bool hasPeer (const uint64& id);

    //
    // Scanning
    //

    void scanRefresh ();

    //
    // Connection policy
    //
    void policyLowWater ();
    void policyEnforce ();

    // configured connections
    void makeConfigured ();

private:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    typedef std::pair<RippleAddress, Peer::pointer>     naPeer;
    typedef std::pair<IPAndPortNumber, Peer::pointer>            pipPeer;
    typedef std::map<IPAndPortNumber, Peer::pointer>::value_type vtPeer;

    boost::asio::io_service& m_io_service;
    boost::asio::ssl::context& m_ssl_context;

    LockType mPeerLock;

    uint64                  mLastPeer;
    int                     mPhase;

    // PeersImp we are connecting with and non-thin peers we are connected to.
    // Only peers we know the connection ip for are listed.
    // We know the ip and port for:
    // - All outbound connections
    // - Some inbound connections (which we figured out).
    boost::unordered_map<IPAndPortNumber, Peer::pointer>         mIpMap;

    // Non-thin peers which we are connected to.
    // PeersImp we have the public key for.
    typedef boost::unordered_map<RippleAddress, Peer::pointer>::value_type vtConMap;
    boost::unordered_map<RippleAddress, Peer::pointer>  mConnectedMap;

    // Connections with have a 64-bit identifier
    boost::unordered_map<uint64, Peer::pointer>         mPeerIdMap;

    Peer::pointer                                       mScanning;
    boost::asio::deadline_timer                         mScanTimer;
    std::string                                         mScanIp;
    int                                                 mScanPort;

    void            scanHandler (const boost::system::error_code& ecResult);

    boost::asio::deadline_timer                         mPolicyTimer;

    void            policyHandler (const boost::system::error_code& ecResult);

    // PeersImp we are establishing a connection with as a client.
    // int                                              miConnectStarting;

    bool            peerAvailable (std::string& strIp, int& iPort);
    bool            peerScanSet (const std::string& strIp, int iPort);

    Peer::pointer   peerConnect (const std::string& strIp, int iPort);
};

void splitIpPort (const std::string& strIpPort, std::string& strIp, int& iPort)
{
    std::vector<std::string>    vIpPort;
    boost::split (vIpPort, strIpPort, boost::is_any_of (" :"));

    strIp   = vIpPort[0];
    iPort   = lexicalCastThrow <int> (vIpPort[1]);
}

void PeersImp::start ()
{
    if (getConfig ().RUN_STANDALONE)
        return;

    // Start running policy.
    policyEnforce ();

    // Start scanning.
    scanRefresh ();
}

bool PeersImp::getTopNAddrs (int n, std::vector<std::string>& addrs)
{

    // Try current connections first
    std::vector<Peer::pointer> peers = getPeerVector();
    BOOST_FOREACH(Peer::ref peer, peers)
    {
        if (peer->isConnected())
        {
            std::string connectString;
            if (peer->getConnectString(connectString))
                addrs.push_back(connectString);
        }
    }

    if (addrs.size() < n)
    {
        // XXX Filter out other local addresses (like ipv6)
        Database*   db = getApp().getWalletDB ()->getDB ();
        DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());

        SQL_FOREACH (db, str (boost::format ("SELECT IpPort FROM PeerIps LIMIT %d") % n))
        {
            std::string str;

            db->getStr (0, str);

            addrs.push_back (str);
        }
    }

    // FIXME: Should uniqify addrs

    return true;
}

bool PeersImp::savePeer (const std::string& strIp, int iPort, char code)
{
    bool    bNew    = false;

    Database* db = getApp().getWalletDB ()->getDB ();

    std::string ipAndPort  = sqlEscape (str (boost::format ("%s %d") % strIp % iPort));

    DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());
    std::string sql = str (boost::format ("SELECT COUNT(*) FROM PeerIps WHERE IpPort=%s;") % ipAndPort);

    if (db->executeSQL (sql) && db->startIterRows ())
    {
        if (!db->getInt (0))
        {
            db->executeSQL (str (boost::format ("INSERT INTO PeerIps (IpPort,Score,Source) values (%s,0,'%c');") % ipAndPort % code));
            bNew    = true;
        }
        else
        {
            // We already had this peer.
            // We will eventually verify its address if it is possible.
            // YYY If it is vsInbound, then we might make verification immediate so we can connect back sooner if the connection
            // is lost.
            nothing ();
        }

        db->endIterRows ();
    }
    else
    {
        Log::out() << "Error saving Peer";
    }

    if (bNew)
        scanRefresh ();

    return bNew;
}

Peer::pointer PeersImp::getPeerById (const uint64& id)
{
    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);
    const boost::unordered_map<uint64, Peer::pointer>::iterator& it = mPeerIdMap.find (id);

    if (it == mPeerIdMap.end ())
        return Peer::pointer ();

    return it->second;
}

bool PeersImp::hasPeer (const uint64& id)
{
    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);
    return mPeerIdMap.find (id) != mPeerIdMap.end ();
}

// An available peer is one we had no trouble connect to last time and that we are not currently knowingly connected or connecting
// too.
//
// <-- true, if a peer is available to connect to
bool PeersImp::peerAvailable (std::string& strIp, int& iPort)
{
    Database*                   db = getApp().getWalletDB ()->getDB ();
    std::vector<std::string>    vstrIpPort;

    // Convert mIpMap (list of open connections) to a vector of "<ip> <port>".
    {
        ScopedLockType sl (mPeerLock, __FILE__, __LINE__);

        vstrIpPort.reserve (mIpMap.size ());

        BOOST_FOREACH (const vtPeer & ipPeer, mIpMap)
        {
            const std::string&  strIp   = ipPeer.first.first;
            int                 iPort   = ipPeer.first.second;

            vstrIpPort.push_back (sqlEscape (str (boost::format ("%s %d") % strIp % iPort)));
        }
    }

    // Get the first IpPort entry which is not in vector and which is not scheduled for scanning.
    std::string strIpPort;

    {
        DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());

        if (db->executeSQL (str (boost::format ("SELECT IpPort FROM PeerIps WHERE ScanNext IS NULL AND IpPort NOT IN (%s) LIMIT 1;")
                                 % strJoin (vstrIpPort.begin (), vstrIpPort.end (), ",")))
                && db->startIterRows ())
        {
            strIpPort   = db->getStrBinary ("IpPort");
            db->endIterRows ();
        }
    }

    bool        bAvailable  = !strIpPort.empty ();

    if (bAvailable)
        splitIpPort (strIpPort, strIp, iPort);

    return bAvailable;
}

// Make sure we have at least low water connections.
void PeersImp::policyLowWater ()
{
    std::string strIp;
    int         iPort;

    // Find an entry to connect to.
    if (getPeerCount () > getConfig ().PEER_CONNECT_LOW_WATER)
    {
        // Above low water mark, don't need more connections.
        WriteLog (lsTRACE, Peers) << "Pool: Low water: sufficient connections: " << mConnectedMap.size () << "/" << getConfig ().PEER_CONNECT_LOW_WATER;

        nothing ();
    }

#if 0
    else if (miConnectStarting == getConfig ().PEER_START_MAX)
    {
        // Too many connections starting to start another.
        nothing ();
    }

#endif
    else if (!peerAvailable (strIp, iPort))
    {
        // No more connections available to start.
        WriteLog (lsTRACE, Peers) << "Pool: Low water: no peers available.";

        // XXX Might ask peers for more ips.
        nothing ();
    }
    else
    {
        // Try to start connection.
        WriteLog (lsTRACE, Peers) << "Pool: Low water: start connection.";

        if (!peerConnect (strIp, iPort))
        {
            WriteLog (lsINFO, Peers) << "Pool: Low water: already connected.";
        }

        // Check if we need more.
        policyLowWater ();
    }
}

void PeersImp::policyEnforce ()
{
    // Cancel any in progress timer.
    (void) mPolicyTimer.cancel ();

    // Enforce policies.
    policyLowWater ();

    if (((++mPhase) % 12) == 0)
    {
        WriteLog (lsTRACE, Peers) << "Making configured connections";
        makeConfigured ();
    }

    // Schedule next enforcement.
    mPolicyTimer.expires_at (boost::posix_time::second_clock::universal_time () + boost::posix_time::seconds (policyIntervalSeconds));
    mPolicyTimer.async_wait (BIND_TYPE (&PeersImp::policyHandler, this, P_1));
}

void PeersImp::policyHandler (const boost::system::error_code& ecResult)
{
    if (ecResult == boost::asio::error::operation_aborted)
    {
        nothing ();
    }
    else if (!ecResult)
    {
        policyEnforce ();
    }
    else
    {
        throw std::runtime_error ("Internal error: unexpected deadline error.");
    }
}

// YYY: Should probably do this in the background.
// YYY: Might end up sending to disconnected peer?
int PeersImp::relayMessage (Peer* fromPeer, const PackedMessage::pointer& msg)
{
    int sentTo = 0;
    std::vector<Peer::pointer> peerVector = getPeerVector ();
    BOOST_FOREACH (Peer::ref peer, peerVector)
    {
        if ((!fromPeer || ! (peer.get () == fromPeer)) && peer->isConnected ())
        {
            ++sentTo;
            peer->sendPacket (msg, false);
        }
    }

    return sentTo;
}

int PeersImp::relayMessageCluster (Peer* fromPeer, const PackedMessage::pointer& msg)
{
    int sentTo = 0;
    std::vector<Peer::pointer> peerVector = getPeerVector ();
    BOOST_FOREACH (Peer::ref peer, peerVector)
    {
        if ((!fromPeer || ! (peer.get () == fromPeer)) && peer->isConnected () && peer->isInCluster ())
        {
            ++sentTo;
            peer->sendPacket (msg, false);
        }
    }

    return sentTo;
}

void PeersImp::relayMessageBut (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg)
{
    // Relay message to all but the specified peers
    std::vector<Peer::pointer> peerVector = getPeerVector ();
    BOOST_FOREACH (Peer::ref peer, peerVector)
    {
        if (peer->isConnected () && (fromPeers.count (peer->getPeerId ()) == 0))
            peer->sendPacket (msg, false);
    }

}

void PeersImp::relayMessageTo (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg)
{
    // Relay message to the specified peers
    std::vector<Peer::pointer> peerVector = getPeerVector ();
    BOOST_FOREACH (Peer::ref peer, peerVector)
    {
        if (peer->isConnected () && (fromPeers.count (peer->getPeerId ()) != 0))
            peer->sendPacket (msg, false);
    }

}

// Schedule a connection via scanning.
//
// Add or modify into PeerIps as a manual entry for immediate scanning.
// Requires sane IP and port.
void PeersImp::connectTo (const std::string& strIp, int iPort)
{
    {
        Database*   db  = getApp().getWalletDB ()->getDB ();
        DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());

        db->executeSQL (str (boost::format ("REPLACE INTO PeerIps (IpPort,Score,Source,ScanNext) values (%s,%d,'%c',0);")
                             % sqlEscape (str (boost::format ("%s %d") % strIp % iPort))
                             % getApp().getUNL ().iSourceScore (UniqueNodeList::vsManual)
                             % char (UniqueNodeList::vsManual)));
    }

    scanRefresh ();
}

// Start a connection, if not already known connected or connecting.
//
// <-- true, if already connected.
Peer::pointer PeersImp::peerConnect (const std::string& strIp, int iPort)
{
    IPAndPortNumber          pipPeer     = make_pair (strIp, iPort);
    Peer::pointer   ppResult;

    {
        ScopedLockType sl (mPeerLock, __FILE__, __LINE__);

        if (mIpMap.find (pipPeer) == mIpMap.end ())
        {
            bool const isInbound (false);
            bool const requirePROXYHandshake (false);

            ppResult = Peer::New (m_io_service, m_ssl_context,
                ++mLastPeer, isInbound, requirePROXYHandshake);

            mIpMap [pipPeer] = ppResult;
        }
    }

    if (ppResult)
    {
        ppResult->connect (strIp, iPort);
        WriteLog (lsDEBUG, Peers) << "Pool: Connecting: " << strIp << " " << iPort;
    }
    else
    {
        WriteLog (lsTRACE, Peers) << "Pool: Already connected: " << strIp << " " << iPort;
    }

    return ppResult;
}

// Returns information on verified peers.
Json::Value PeersImp::getPeersJson ()
{
    Json::Value                 ret (Json::arrayValue);
    std::vector<Peer::pointer>  vppPeers    = getPeerVector ();

    BOOST_FOREACH (Peer::ref peer, vppPeers)
    {
        ret.append (peer->getJson ());
    }

    return ret;
}

int PeersImp::getPeerCount ()
{
    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);

    return mConnectedMap.size ();
}

std::vector<Peer::pointer> PeersImp::getPeerVector ()
{
    std::vector<Peer::pointer> ret;

    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);

    ret.reserve (mConnectedMap.size ());

    BOOST_FOREACH (const vtConMap & pair, mConnectedMap)
    {
        assert (!!pair.second);
        ret.push_back (pair.second);
    }

    return ret;
}

uint64 PeersImp::assignPeerId ()
{
    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);
    return ++mLastPeer;
}

// Now know peer's node public key.  Determine if we want to stay connected.
// <-- bNew: false = redundant
bool PeersImp::peerConnected (Peer::ref peer, const RippleAddress& naPeer,
                           const std::string& strIP, int iPort)
{
    bool    bNew    = false;

    assert (!!peer);

    if (naPeer == getApp().getLocalCredentials ().getNodePublic ())
    {
        WriteLog (lsINFO, Peers) << "Pool: Connected: self: " << addressToString (peer.get()) << ": " << naPeer.humanNodePublic () << " " << strIP << " " << iPort;
    }
    else
    {
        ScopedLockType sl (mPeerLock, __FILE__, __LINE__);
        const boost::unordered_map<RippleAddress, Peer::pointer>::iterator& itCm    = mConnectedMap.find (naPeer);

        if (itCm == mConnectedMap.end ())
        {
            // New connection.
            //WriteLog (lsINFO, Peers) << "Pool: Connected: new: " << addressToString (peer.get()) << ": " << naPeer.humanNodePublic() << " " << strIP << " " << iPort;

            mConnectedMap[naPeer]   = peer;
            bNew                    = true;

            assert (peer->getPeerId () != 0);
            mPeerIdMap.insert (std::make_pair (peer->getPeerId (), peer));
        }
        // Found in map, already connected.
        else if (!strIP.empty ())
        {
            // Was an outbound connection, we know IP and port.
            // Note in previous connection how to reconnect.
            if (itCm->second->getIP ().empty ())
            {
                // Old peer did not know it's IP.
                //WriteLog (lsINFO, Peers) << "Pool: Connected: redundant: outbound: " << addressToString (peer.get()) << " discovered: " << addressToString(itCm->second) << ": " << strIP << " " << iPort;

                itCm->second->setIpPort (strIP, iPort);

                // Add old connection to identified connection list.
                mIpMap[make_pair (strIP, iPort)] = itCm->second;
            }
            else
            {
                // Old peer knew its IP.  Do nothing.
                //WriteLog (lsINFO, Peers) << "Pool: Connected: redundant: outbound: rediscovered: " << addressToString (peer.get()) << " " << strIP << " " << iPort;

                nothing ();
            }
        }
        else
        {
            //WriteLog (lsINFO, Peers) << "Pool: Connected: redundant: inbound: " << addressToString (peer.get()) << " " << strIP << " " << iPort;

            nothing ();
        }
    }

    return bNew;
}

// We maintain a map of public key to peer for connected and verified peers.  Maintain it.
void PeersImp::peerDisconnected (Peer::ref peer, const RippleAddress& naPeer)
{
    ScopedLockType sl (mPeerLock, __FILE__, __LINE__);

    if (naPeer.isValid ())
    {
        const boost::unordered_map<RippleAddress, Peer::pointer>::iterator& itCm = mConnectedMap.find (naPeer);

        if (itCm == mConnectedMap.end ())
        {
            // Did not find it.  Not already connecting or connected.
            WriteLog (lsWARNING, Peers) << "Pool: disconnected: Internal Error: mConnectedMap was inconsistent.";
            // XXX Maybe bad error, considering we have racing connections, may not so bad.
        }
        else if (itCm->second != peer)
        {
            WriteLog (lsWARNING, Peers) << "Pool: disconected: non canonical entry";

            nothing ();
        }
        else
        {
            // Found it. Delete it.
            mConnectedMap.erase (itCm);

            //WriteLog (lsINFO, Peers) << "Pool: disconnected: " << naPeer.humanNodePublic() << " " << peer->getIP() << " " << peer->getPort();
        }
    }
    else
    {
        //WriteLog (lsINFO, Peers) << "Pool: disconnected: anonymous: " << peer->getIP() << " " << peer->getPort();
    }

    assert (peer->getPeerId () != 0);
    mPeerIdMap.erase (peer->getPeerId ());
}

// Schedule for immediate scanning, if not already scheduled.
//
// <-- true, scanRefresh needed.
bool PeersImp::peerScanSet (const std::string& strIp, int iPort)
{
    std::string strIpPort   = str (boost::format ("%s %d") % strIp % iPort);
    bool        bScanDirty  = false;

    DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());
    Database*   db = getApp().getWalletDB ()->getDB ();

    if (db->executeSQL (str (boost::format ("SELECT ScanNext FROM PeerIps WHERE IpPort=%s;")
                             % sqlEscape (strIpPort)))
            && db->startIterRows ())
    {
        if (db->getNull ("ScanNext"))
        {
            // Non-scanning connection terminated.  Schedule for scanning.
            int                         iInterval   = getConfig ().PEER_SCAN_INTERVAL_MIN;
            boost::posix_time::ptime    tpNow       = boost::posix_time::second_clock::universal_time ();
            boost::posix_time::ptime    tpNext      = tpNow + boost::posix_time::seconds (iInterval);

            //WriteLog (lsINFO, Peers) << str(boost::format("Pool: Scan: schedule create: %s %s (next %s, delay=%d)")
            //  % mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());

            db->executeSQL (str (boost::format ("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IpPort=%s;")
                                 % iToSeconds (tpNext)
                                 % iInterval
                                 % sqlEscape (strIpPort)));

            bScanDirty  = true;
        }
        else
        {
            // Scan connection terminated, already scheduled for retry.
            // boost::posix_time::ptime tpNow       = boost::posix_time::second_clock::universal_time();
            // boost::posix_time::ptime tpNext      = ptFromSeconds(db->getInt("ScanNext"));

            //WriteLog (lsINFO, Peers) << str(boost::format("Pool: Scan: schedule exists: %s %s (next %s, delay=%d)")
            //  % mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());
        }

        db->endIterRows ();
    }
    else
    {
        //WriteLog (lsWARNING, Peers) << "Pool: Scan: peer wasn't in PeerIps: " << strIp << " " << iPort;
    }

    return bScanDirty;
}

// --> strIp: not empty
void PeersImp::peerClosed (Peer::ref peer, const std::string& strIp, int iPort)
{
    IPAndPortNumber      ipPeer          = make_pair (strIp, iPort);
    bool        bScanRefresh    = false;

    // If the connection was our scan, we are no longer scanning.
    if (mScanning && mScanning == peer)
    {
        //WriteLog (lsINFO, Peers) << "Pool: Scan: scan fail: " << strIp << " " << iPort;

        mScanning.reset ();                 // No longer scanning.
        bScanRefresh    = true;             // Look for more to scan.
    }

    // Determine if closed peer was redundant.
    bool    bRedundant  = true;
    {
        ScopedLockType sl (mPeerLock, __FILE__, __LINE__);
        const boost::unordered_map<IPAndPortNumber, Peer::pointer>::iterator&    itIp = mIpMap.find (ipPeer);

        if (itIp == mIpMap.end ())
        {
            // Did not find it.  Not already connecting or connected.
            WriteLog (lsWARNING, Peers) << "Pool: Closed: UNEXPECTED: " << addressToString (peer.get()) << ": " << strIp << " " << iPort;
            // XXX Internal error.
        }
        else if (mIpMap[ipPeer] == peer)
        {
            // We were the identified connection.
            //WriteLog (lsINFO, Peers) << "Pool: Closed: identified: " << addressToString (peer.get()) << ": " << strIp << " " << iPort;

            // Delete our entry.
            mIpMap.erase (itIp);

            bRedundant  = false;
        }
        else
        {
            // Found it.  But, we were redundant.
            //WriteLog (lsINFO, Peers) << "Pool: Closed: redundant: " << addressToString (peer.get()) << ": " << strIp << " " << iPort;
        }
    }

    if (!bRedundant)
    {
        // If closed was not redundant schedule if not already scheduled.
        bScanRefresh    = peerScanSet (ipPeer.first, ipPeer.second) || bScanRefresh;
    }

    if (bScanRefresh)
        scanRefresh ();
}

void PeersImp::peerVerified (Peer::ref peer)
{
    if (mScanning && mScanning == peer)
    {
        // Scan completed successfully.
        std::string strIp   = peer->getIP ();
        int         iPort   = peer->getPort ();

        std::string strIpPort   = str (boost::format ("%s %d") % strIp % iPort);

        //WriteLog (lsINFO, Peers) << str(boost::format("Pool: Scan: connected: %s %s %s (scanned)") % addressToString (peer.get()) % strIp % iPort);

        if (peer->getNodePublic () == getApp().getLocalCredentials ().getNodePublic ())
        {
            // Talking to ourself.  We will just back off.  This lets us maybe advertise our outside address.

            nothing (); // Do nothing, leave scheduled scanning.
        }
        else
        {
            // Talking with a different peer.
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            db->executeSQL (boost::str (boost::format ("UPDATE PeerIps SET ScanNext=NULL,ScanInterval=0 WHERE IpPort=%s;")
                                        % sqlEscape (strIpPort)));
            // XXX Check error.
        }

        mScanning.reset ();

        scanRefresh (); // Continue scanning.
    }
}

void PeersImp::scanHandler (const boost::system::error_code& ecResult)
{
    if (ecResult == boost::asio::error::operation_aborted)
    {
        nothing ();
    }
    else if (!ecResult)
    {
        scanRefresh ();
    }
    else
    {
        throw std::runtime_error ("Internal error: unexpected deadline error.");
    }
}

void PeersImp::makeConfigured ()
{
    if (getConfig ().RUN_STANDALONE)
        return;

    BOOST_FOREACH (const std::string & strPeer, getConfig ().IPS)
    {
        std::string strIP;
        int iPort;

        if (parseIpPort (strPeer, strIP, iPort))
            peerConnect (strIP, iPort);
    }
}

// Scan ips as per db entries.
void PeersImp::scanRefresh ()
{
    if (getConfig ().RUN_STANDALONE)
    {
        nothing ();
    }
    else if (mScanning)
    {
        // Currently scanning, will scan again after completion.
        WriteLog (lsTRACE, Peers) << "Pool: Scan: already scanning";

        nothing ();
    }
    else
    {
        // Discover if there are entries that need scanning.
        boost::posix_time::ptime    tpNext;
        boost::posix_time::ptime    tpNow;
        std::string                 strIpPort;
        int                         iInterval;

        {
            DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());
            Database*   db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL ("SELECT * FROM PeerIps INDEXED BY PeerScanIndex WHERE ScanNext NOT NULL ORDER BY ScanNext LIMIT 1;")
                    && db->startIterRows ())
            {
                // Have an entry to scan.
                int         iNext   = db->getInt ("ScanNext");

                tpNext  = ptFromSeconds (iNext);
                tpNow   = boost::posix_time::second_clock::universal_time ();

                db->getStr ("IpPort", strIpPort);
                iInterval   = db->getInt ("ScanInterval");
                db->endIterRows ();
            }
            else
            {
                // No entries to scan.
                tpNow   = boost::posix_time::ptime (boost::posix_time::not_a_date_time);
            }
        }

        if (tpNow.is_not_a_date_time ())
        {
            //WriteLog (lsINFO, Peers) << "Pool: Scan: stop.";

            (void) mScanTimer.cancel ();
        }
        else if (tpNext <= tpNow)
        {
            // Scan it.
            splitIpPort (strIpPort, mScanIp, mScanPort);

            (void) mScanTimer.cancel ();

            iInterval   = std::max (iInterval, getConfig ().PEER_SCAN_INTERVAL_MIN);

            tpNext      = tpNow + boost::posix_time::seconds (iInterval);

            //WriteLog (lsINFO, Peers) << str(boost::format("Pool: Scan: Now: %s %s (next %s, delay=%d)")
            //  % mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());

            iInterval   *= 2;

            {
                DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
                Database* db = getApp().getWalletDB ()->getDB ();

                db->executeSQL (boost::str (boost::format ("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IpPort=%s;")
                                            % iToSeconds (tpNext)
                                            % iInterval
                                            % sqlEscape (strIpPort)));
                // XXX Check error.
            }

            mScanning   = peerConnect (mScanIp, mScanPort);

            if (!mScanning)
            {
                // Already connected. Try again.
                scanRefresh ();
            }
        }
        else
        {
            //WriteLog (lsINFO, Peers) << str(boost::format("Pool: Scan: Next: %s (next %s, delay=%d)")
            //  % strIpPort % tpNext % (tpNext-tpNow).total_seconds());

            mScanTimer.expires_at (tpNext);
            mScanTimer.async_wait (BIND_TYPE (&PeersImp::scanHandler, this, P_1));
        }
    }
}

//------------------------------------------------------------------------------

Peers* Peers::New (Stoppable& parent,
    boost::asio::io_service& io_service,
        boost::asio::ssl::context& ssl_context)
{
    return new PeersImp (parent, io_service, ssl_context);
}

