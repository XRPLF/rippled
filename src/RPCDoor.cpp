#include "RPCDoor.h"
#include "Application.h"
#include "Config.h"
#include <boost/bind.hpp>
//#include <boost/log/trivial.hpp>
#include <iostream>

using namespace std;
using namespace boost::asio::ip;

RPCDoor::RPCDoor(boost::asio::io_service& io_service) :
	mAcceptor(io_service, tcp::endpoint(address::from_string(theConfig.RPC_IP), theConfig.RPC_PORT))
{
	cerr << "RPC port: " << theConfig.RPC_IP << " " << theConfig.RPC_PORT << " allow remote: " << theConfig.RPC_ALLOW_REMOTE << endl;
	startListening();
}

void RPCDoor::startListening()
{
	RPCServer::pointer new_connection = RPCServer::create(mAcceptor.get_io_service(), &theApp->getOPs());
	mAcceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

	mAcceptor.async_accept(new_connection->getSocket(),
		boost::bind(&RPCDoor::handleConnect, this, new_connection,
		boost::asio::placeholders::error));
}

bool RPCDoor::isClientAllowed(const std::string& ip)
{
	if(theConfig.RPC_ALLOW_REMOTE) return(true);
	if(ip=="127.0.0.1") return(true);
	return(false);
}

void RPCDoor::handleConnect(RPCServer::pointer new_connection,
	const boost::system::error_code& error)
{
	if(!error)
	{
		// Restrict callers by IP
		if(!isClientAllowed(new_connection->getSocket().remote_endpoint().address().to_string()))
		{
			return;
		}

		new_connection->connected();
	}
	else cout << "Error: " << error;

	startListening();
}
// vim:ts=4
