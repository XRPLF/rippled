#include "Peer.h"
#include "PackedMessage.h"
#include "types.h"
#include <boost/asio.hpp>
class KnownNodeList;
/*
This is the list of all the Peers we are currently connected to
*/
class ConnectionPool
{
	std::vector<Peer::pointer> mPeers;
	std::vector<std::pair<PackedMessage::pointer,int> > mBroadcastMessages;

public:
	ConnectionPool();
	void connectToNetwork(KnownNodeList& nodeList,boost::asio::io_service& io_service);
	void relayMessage(Peer* fromPeer,PackedMessage::pointer msg,uint64 ledgerIndex);
	bool isMessageKnown(PackedMessage::pointer msg);

	
};