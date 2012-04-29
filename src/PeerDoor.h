#include <map>
#include <set>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Peer.h"

/*
Handles incoming connections from other Peers
*/

class PeerDoor
{
private:
	boost::asio::ip::tcp::acceptor	mAcceptor;
	boost::asio::ssl::context		mCtx;

	void	startListening();
	void	handleConnect(Peer::pointer new_connection, const boost::system::error_code& error);

public:
	PeerDoor(boost::asio::io_service& io_service);
};
// vim:ts=4
