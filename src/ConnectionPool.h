#ifndef __CONNECTION_POOL__
#define __CONNECTION_POOL__

#include <boost/thread/mutex.hpp>

#include "Peer.h"
#include "PackedMessage.h"
#include "types.h"

//
// Access to the Newcoin network.
//
class ConnectionPool
{
private:
    boost::mutex mPeerLock;

	typedef std::pair<NewcoinAddress, Peer::pointer> naPeer;

	// Count of peers we are in progress of connecting to.
	// We are in progress until we know their network public key.
	int			iConnecting;

	// Peers we are connecting with and non-thin peers we are connected to.
    boost::unordered_map<ipPort, Peer::pointer> mIpMap;

	// Non-thin peers which we are connected to.
    boost::unordered_map<NewcoinAddress, Peer::pointer> mConnectedMap;

public:
	ConnectionPool();

	// Begin enforcing connection policy.
	void start();

	// Send message to network.
	void relayMessage(Peer* fromPeer, PackedMessage::pointer msg);

	// Manual connection request.
	// Queue for immediate scanning.
	bool connectTo(const std::string& strIp, int iPort);

	//
	// Peer connectivity notification.
	//

	// Inbound connection, false=reject
	bool peerAccepted(Peer::pointer peer, const std::string& strIp, int iPort);

	// We know peers node public key.
	void peerConnected(Peer::pointer peer);

	// No longer connected.
	void peerDisconnected(Peer::pointer peer);

	Json::Value getPeersJson();

#if 0
	//std::vector<std::pair<PackedMessage::pointer,int> > mBroadcastMessages;

	bool isMessageKnown(PackedMessage::pointer msg);
#endif
};

#endif
// vim:ts=4
