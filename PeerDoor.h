#include <map>
#include <set>

#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

#include "Peer.h"

/*
Handles incoming connections from other Peers
*/

class PeerDoor
{
	boost::asio::ip::tcp::acceptor mAcceptor;
	void startListening();
	void handleConnect(Peer::pointer new_connection,
		const boost::system::error_code& error);

	boost::mutex peerLock;
	std::map<uint160, Peer::pointer> peerMap;

public:
	PeerDoor(boost::asio::io_service& io_service);


	// hanko->peer mapping functions
	bool inMap(const uint160& hanko);
	bool addToMap(const uint160& hanko, Peer::pointer peer);
	bool delFromMap(const uint160& hanko, Peer::pointer peer);
	Peer::pointer findInMap(const uint160& hanko);
	std::map<uint160, Peer::pointer> getAllConnected();
};
