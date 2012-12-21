#include "RPCServer.h"
#include <boost/asio.hpp>

/*
Handles incoming connections from people making RPC Requests
*/

class RPCDoor
{
	boost::asio::ip::tcp::acceptor	mAcceptor;
	boost::asio::deadline_timer		mDelayTimer;

	void startListening();
	void handleConnect(RPCServer::pointer new_connection,
		const boost::system::error_code& error);

	bool isClientAllowed(const std::string& ip);
public:
	RPCDoor(boost::asio::io_service& io_service);
	~RPCDoor();
};
