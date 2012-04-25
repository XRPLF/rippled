#ifndef __CONNECTION_POOL__
#define __CONNECTION_POOL__

#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

#include "Peer.h"
#include "PackedMessage.h"
#include "types.h"

/*
This is the list of all the Peers we are currently connected to
*/
class ConnectionPool
{
    boost::mutex peerLock;
    std::vector<Peer::pointer> mPeers; // FIXME
    std::map<uint160, Peer::pointer> peerMap;

public:
	ConnectionPool();
	void connectToNetwork(boost::asio::io_service& io_service);
	void relayMessage(Peer* fromPeer, PackedMessage::pointer msg);

	// Manual connection request.
	// Queue for immediate scanning.
	bool connectTo(const std::string& host, const std::string& port);

	// Peer notification routines.
	void peerDisconnected(NewcoinAddress naPeer);

	Json::Value getPeersJson();

#if 0
	//std::vector<std::pair<PackedMessage::pointer,int> > mBroadcastMessages;

	bool isMessageKnown(PackedMessage::pointer msg);

	// hanko->peer mapping functions
	bool addToMap(const uint160& hanko, Peer::pointer peer);
	bool delFromMap(const uint160& hanko);

	bool inMap(const uint160& hanko);
	std::map<uint160, Peer::pointer> getAllConnected();
	Peer::pointer findInMap(const uint160& hanko);
#endif
};

#endif
// vim:ts=4
