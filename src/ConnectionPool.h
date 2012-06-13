#ifndef __CONNECTION_POOL__
#define __CONNECTION_POOL__

#include <boost/asio/ssl.hpp>
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

	typedef std::pair<NewcoinAddress, Peer::pointer>	naPeer;
	typedef std::pair<ipPort, Peer::pointer>			pipPeer;

	// Peers we are connecting with and non-thin peers we are connected to.
    boost::unordered_map<ipPort, Peer::pointer>			mIpMap;

	// Non-thin peers which we are connected to.
    boost::unordered_map<NewcoinAddress, Peer::pointer>	mConnectedMap;

    boost::asio::ssl::context							mCtx;

	bool												bScanning;
	boost::asio::deadline_timer							mScanTimer;
	std::string											mScanIp;
	int													mScanPort;

	void		scanHandler(const boost::system::error_code& ecResult);

	// Peers we are establishing a connection with as a client.
	// int												miConnectStarting;

	bool												peerAvailable(std::string& strIp, int& iPort);

public:
	ConnectionPool(boost::asio::io_service& io_service);

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
	bool getTopNAddrs(int n,std::vector<std::string>& addrs);
	bool savePeer(const std::string& strIp, int iPort, char code);

	// Inbound connection, false=reject
	bool peerRegister(Peer::pointer peer, const std::string& strIp, int iPort);

	// We know peers node public key.  false=reject
	bool peerConnected(Peer::pointer peer, const NewcoinAddress& na);

	// No longer connected.
	void peerDisconnected(Peer::pointer peer, const ipPort& ipPeer, const NewcoinAddress& naPeer);

	// As client accepted.
	void peerVerified(const std::string& strIp, int iPort);

	// As client failed connect and be accepted.
	void peerFailed(const std::string& strIp, int iPort);

	Json::Value getPeersJson();
	std::vector<Peer::pointer> getPeerVector();

	//
	// Scanning
	//

	void scanRefresh();

	//
	// Connection policy
	//
	void policyLowWater();
	void policyEnforce();

#if 0
	//std::vector<std::pair<PackedMessage::pointer,int> > mBroadcastMessages;

	bool isMessageKnown(PackedMessage::pointer msg);
#endif
};

extern void splitIpPort(const std::string& strIpPort, std::string& strIp, int& iPort);

#endif
// vim:ts=4
