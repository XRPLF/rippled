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
	// Only peers we know the connection ip for are listed.
	// We know the ip and port for:
	// - All outbound connections
	// - Some inbound connections (which we figured out).
    boost::unordered_map<ipPort, Peer::pointer>			mIpMap;

	// Non-thin peers which we are connected to.
	// Peers we have the public key for.
    boost::unordered_map<NewcoinAddress, Peer::pointer>	mConnectedMap;

    boost::asio::ssl::context							mCtx;

	Peer::pointer										mScanning;
	boost::asio::deadline_timer							mScanTimer;
	std::string											mScanIp;
	int													mScanPort;

	void			scanHandler(const boost::system::error_code& ecResult);

	boost::asio::deadline_timer							mPolicyTimer;

	void			policyHandler(const boost::system::error_code& ecResult);

	// Peers we are establishing a connection with as a client.
	// int												miConnectStarting;

	bool			peerAvailable(std::string& strIp, int& iPort);
	bool			peerScanSet(const std::string& strIp, int iPort);

	Peer::pointer	peerConnect(const std::string& strIp, int iPort);

public:
	ConnectionPool(boost::asio::io_service& io_service);

	// Begin enforcing connection policy.
	void start();

	// Send message to network.
	void relayMessage(Peer* fromPeer, const PackedMessage::pointer& msg);

	// Manual connection request.
	// Queue for immediate scanning.
	void connectTo(const std::string& strIp, int iPort);

	//
	// Peer connectivity notification.
	//
	bool getTopNAddrs(int n,std::vector<std::string>& addrs);
	bool savePeer(const std::string& strIp, int iPort, char code);

	// We know peers node public key.
	// <-- bool: false=reject
	bool peerConnected(Peer::ref peer, const NewcoinAddress& naPeer, const std::string& strIP, int iPort);

	// No longer connected.
	void peerDisconnected(Peer::ref peer, const NewcoinAddress& naPeer);

	// As client accepted.
	void peerVerified(Peer::ref peer);

	// As client failed connect and be accepted.
	void peerClosed(Peer::ref peer, const std::string& strIp, int iPort);

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

};

extern void splitIpPort(const std::string& strIpPort, std::string& strIp, int& iPort);

#endif
// vim:ts=4
