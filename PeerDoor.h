#include "Peer.h"
#include <boost/asio.hpp>

/*
Handles incoming connections from other Peers
*/

class PeerDoor
{
	boost::asio::ip::tcp::acceptor mAcceptor;
	void startListening();
	void handleConnect(Peer::pointer new_connection,
		const boost::system::error_code& error);
public:
	PeerDoor(boost::asio::io_service& io_service);
};