#ifndef __RPCSERVER__
#define __RPCSERVER__

#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "../json/value.h"

#include "HTTPRequest.h"
#include "RippleAddress.h"
#include "NetworkOPs.h"
#include "SerializedLedger.h"
#include "RPCHandler.h"

class RPCServer : public boost::enable_shared_from_this<RPCServer>
{
public:
	
	typedef boost::shared_ptr<RPCServer> pointer;

private:
	

	NetworkOPs*	mNetOps;
	RPCHandler mRPCHandler;

	boost::asio::ip::tcp::socket mSocket;

	boost::asio::streambuf mLineBuffer;
	std::vector<unsigned char> mQueryVec;
	std::string mReplyStr;

	HTTPRequest mHTTPRequest;

	
	int mRole;

	RPCServer(boost::asio::io_service& io_service, NetworkOPs* nopNetwork);

	RPCServer(const RPCServer&); // no implementation
	RPCServer& operator=(const RPCServer&); // no implementation

	void handle_write(const boost::system::error_code& ec);
	void handle_read_line(const boost::system::error_code& ec);
	void handle_read_req(const boost::system::error_code& ec);

	std::string handleRequest(const std::string& requestStr);

public:
	static pointer create(boost::asio::io_service& io_service, NetworkOPs* mNetOps)
	{
		return pointer(new RPCServer(io_service, mNetOps));
	}

	boost::asio::ip::tcp::socket& getSocket()
	{
		return mSocket;
	}

	void connected();
};

#endif

// vim:ts=4
