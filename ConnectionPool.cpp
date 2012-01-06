#include "ConnectionPool.h"
#include "Config.h"
#include "KnownNodeList.h"
#include "Peer.h"
#include <boost/foreach.hpp>

using namespace boost;
using boost::asio::ip::tcp;

ConnectionPool::ConnectionPool() 
{
	

}


void ConnectionPool::connectToNetwork(KnownNodeList& nodeList,boost::asio::io_service& io_service)
{
	for(int n=0; n<theConfig.NUMBER_CONNECTIONS; n++)
	{
		KnownNode* node=nodeList.getNextNode();
		if(!node) return;
		
		Peer::pointer peer=Peer::create(io_service);
//		peer->connectTo(*node); // FIXME
		mPeers.push_back(peer);
		
	}
}
/*
bool ConnectionPool::isMessageKnown(PackedMessage::pointer msg)
{
	for(unsigned int n=0; n<mBroadcastMessages.size(); n++)
	{
		if(msg==mBroadcastMessages[n].first) return(false);
	}
	return(false);
}
*/


void ConnectionPool::relayMessage(Peer* fromPeer,PackedMessage::pointer msg)
{
	BOOST_FOREACH(Peer::pointer peer, mPeers)
	{
		if(!fromPeer || !(peer.get() == fromPeer))
			peer->sendPacket(msg);
	}
}