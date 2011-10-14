#include "RPCDoor.h"
#include "Config.h"
#include <boost/bind.hpp>
//#include <boost/log/trivial.hpp>
#include <iostream>

using namespace std;
using namespace boost::asio::ip;

RPCDoor::RPCDoor(boost::asio::io_service& io_service) : 
mAcceptor(io_service, tcp::endpoint(boost::asio::ip::address_v4::loopback(), theConfig.RPC_PORT))
{
	
	startListening();
}

void RPCDoor::startListening()
{
	RPCServer::pointer new_connection = RPCServer::create(mAcceptor.get_io_service());
	mAcceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

	mAcceptor.async_accept(new_connection->getSocket(),
		boost::bind(&RPCDoor::handleConnect, this, new_connection,
		boost::asio::placeholders::error));
}

bool RPCDoor::isClientAllowed(std::string ip)
{
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
	}else cout << "Error: " << error;//BOOST_LOG_TRIVIAL(info) << "Error: " << error;

	startListening();
}