
#include <boost/foreach.hpp>
#include <boost/asio.hpp>

#include "ConnectionPool.h"
#include "Config.h"
#include "KnownNodeList.h"
#include "Peer.h"
#include "Application.h"


ConnectionPool::ConnectionPool() 
{ ; }


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


void ConnectionPool::relayMessage(Peer* fromPeer, PackedMessage::pointer msg)
{
	BOOST_FOREACH(Peer::pointer peer, mPeers)
	{
		if(!fromPeer || !(peer.get() == fromPeer))
			peer->sendPacket(msg);
	}
}

bool ConnectionPool::addToMap(const uint160& hanko, Peer::pointer peer)
{
	boost::mutex::scoped_lock sl(peerLock);
	return peerMap.insert(std::make_pair(hanko, peer)).second;
}

bool ConnectionPool::delFromMap(const uint160& hanko, Peer::pointer peer)
{
	boost::mutex::scoped_lock sl(peerLock);
	std::map<uint160, Peer::pointer>::iterator it=peerMap.find(hanko);
	if((it==peerMap.end()) || (it->first!=hanko)) return false;
	peerMap.erase(it);
	return true;
}

Peer::pointer ConnectionPool::findInMap(const uint160& hanko)
{
	boost::mutex::scoped_lock sl(peerLock);
	std::map<uint160, Peer::pointer>::iterator it=peerMap.find(hanko);
	if(it==peerMap.end()) return Peer::pointer();
	return it->second;
}

bool ConnectionPool::inMap(const uint160& hanko)
{
	boost::mutex::scoped_lock sl(peerLock);
	return peerMap.find(hanko) != peerMap.end();
}

std::map<uint160, Peer::pointer> ConnectionPool::getAllConnected()
{
	boost::mutex::scoped_lock sl(peerLock);
	return peerMap;
}

bool ConnectionPool::connectTo(const std::string& host, const std::string& port)
{
	boost::asio::ip::tcp::resolver res(theApp->getIOService());
	boost::asio::ip::tcp::resolver::query query(host.c_str(), port.c_str());
	boost::asio::ip::tcp::resolver::iterator it(res.resolve(query)), end;

	Peer::pointer peer(Peer::create(theApp->getIOService()));
	boost::system::error_code error = boost::asio::error::host_not_found;
	while (error && (it!=end))
	{
		peer->getSocket().close();
		peer->getSocket().connect(*it++, error);
	}
	if(error) return false;
	boost::mutex::scoped_lock sl(peerLock);
	mPeers.push_back(peer);
	peer->connected(boost::system::error_code());
	return true;
}
