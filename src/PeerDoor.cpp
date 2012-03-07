#include "PeerDoor.h"
#include "Config.h"
#include <boost/bind.hpp>
//#include <boost/log/trivial.hpp>
#include <iostream>
using namespace std;
using namespace boost::asio::ip;

PeerDoor::PeerDoor(boost::asio::io_service& io_service) : 
	mAcceptor(io_service, tcp::endpoint(tcp::v4(), theConfig.PEER_PORT))
{
	cout << "Opening peer door on port: " << theConfig.PEER_PORT << endl;
	startListening();
}

void PeerDoor::startListening()
{
	Peer::pointer new_connection = Peer::create(mAcceptor.get_io_service());

	mAcceptor.async_accept(new_connection->getSocket(),
		boost::bind(&PeerDoor::handleConnect, this, new_connection,
		boost::asio::placeholders::error));
}

void PeerDoor::handleConnect(Peer::pointer new_connection,
	const boost::system::error_code& error)
{
	if(!error)
	{
		new_connection->connected(error);
	}
	else cout << "Error: " << error; // BOOST_LOG_TRIVIAL(info) << "Error: " << error;

	startListening();
}

