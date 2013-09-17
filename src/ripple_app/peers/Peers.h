//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PEERS_H_INCLUDED
#define RIPPLE_PEERS_H_INCLUDED

/** Manages the set of connected peers.
*/
class Peers
{
public:
    static Peers* New (boost::asio::io_service& io_service,
        boost::asio::ssl::context& context);

    virtual ~Peers () { }

    // Begin enforcing connection policy.
    virtual void start () = 0;

    // Send message to network.
    virtual int relayMessage (Peer* fromPeer, const PackedMessage::pointer& msg) = 0;
    virtual int relayMessageCluster (Peer* fromPeer, const PackedMessage::pointer& msg) = 0;
    virtual void relayMessageTo (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg) = 0;
    virtual void relayMessageBut (const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg) = 0;

    // Manual connection request.
    // Queue for immediate scanning.
    virtual void connectTo (const std::string& strIp, int iPort) = 0;

    //
    // Peer connectivity notification.
    //
    virtual bool getTopNAddrs (int n, std::vector<std::string>& addrs) = 0;
    virtual bool savePeer (const std::string& strIp, int iPort, char code) = 0;

    // We know peers node public key.
    // <-- bool: false=reject
    virtual bool peerConnected (Peer::ref peer, const RippleAddress& naPeer, const std::string& strIP, int iPort) = 0;

    // No longer connected.
    virtual void peerDisconnected (Peer::ref peer, const RippleAddress& naPeer) = 0;

    // As client accepted.
    virtual void peerVerified (Peer::ref peer) = 0;

    // As client failed connect and be accepted.
    virtual void peerClosed (Peer::ref peer, const std::string& strIp, int iPort) = 0;

    virtual int getPeerCount () = 0;
    virtual Json::Value getPeersJson () = 0;
    virtual std::vector<Peer::pointer> getPeerVector () = 0;

    // Peer 64-bit ID function
    virtual uint64 assignPeerId () = 0;
    virtual Peer::pointer getPeerById (const uint64& id) = 0;
    virtual bool hasPeer (const uint64& id) = 0;

    //
    // Scanning
    //

    virtual void scanRefresh () = 0;

    //
    // Connection policy
    //
    virtual void policyLowWater () = 0;
    virtual void policyEnforce () = 0; // VFALCO This and others can be made private

    // configured connections
    virtual void makeConfigured () = 0;
};

// VFALCO TODO Put this in some group of utilities
extern void splitIpPort (const std::string& strIpPort, std::string& strIp, int& iPort);

#endif
// vim:ts=4
