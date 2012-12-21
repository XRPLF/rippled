#include "RPCDoor.h"
#include "Application.h"
#include "Config.h"
#include "Log.h"
#include <boost/bind.hpp>
#include <iostream>

SETUP_LOG();

using namespace std;
using namespace boost::asio::ip;

RPCDoor::RPCDoor(boost::asio::io_service& io_service) :
	mAcceptor(io_service, tcp::endpoint(address::from_string(theConfig.RPC_IP), theConfig.RPC_PORT)),
	mDelayTimer(io_service)
{
	cLog(lsINFO) << "RPC port: " << theConfig.RPC_IP << " " << theConfig.RPC_PORT << " allow remote: " << theConfig.RPC_ALLOW_REMOTE;
	startListening();
}

RPCDoor::~RPCDoor()
{
	cLog(lsINFO) << "RPC port: " << theConfig.RPC_IP << " " << theConfig.RPC_PORT << " allow remote: " << theConfig.RPC_ALLOW_REMOTE;
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
	if (theConfig.RPC_ALLOW_REMOTE)
		return true;
	if (ip == "127.0.0.1")
		return true;

	return false;
}

void RPCDoor::handleConnect(RPCServer::pointer new_connection,
	const boost::system::error_code& error)
{
	bool delay = false;
	if (!error)
	{
		// Restrict callers by IP
		if (!isClientAllowed(new_connection->getSocket().remote_endpoint().address().to_string()))
		{
			startListening();
			return;
		}

		new_connection->connected();
	}
	else
	{
		if (error == boost::system::errc::too_many_files_open)
			delay = true;
		cLog(lsINFO) << "RPCDoor::handleConnect Error: " << error;
	}

	if (delay)
	{
		mDelayTimer.expires_from_now(boost::posix_time::milliseconds(1000));
		mDelayTimer.async_wait(boost::bind(&RPCDoor::startListening, this));
	}
	else
		startListening();
}
// vim:ts=4
