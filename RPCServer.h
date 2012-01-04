#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "json/value.h"

#include "HttpRequest.h"
#include "RequestParser.h"

class RPCServer  : public boost::enable_shared_from_this<RPCServer>
{
	boost::asio::ip::tcp::socket mSocket;
	boost::array<char, 8192> mReadBuffer;
	std::string mReplyStr;
	
	HttpRequest mIncomingRequest;
	HttpRequestParser mRequestParser;

	RPCServer(boost::asio::io_service& io_service);

	void handle_write(const boost::system::error_code& error);

	void handle_read(const boost::system::error_code& e, std::size_t bytes_transferred);

	
	std::string handleRequest(const std::string& requestStr);
	void sendReply();

	Json::Value doCommand(const std::string& command, Json::Value& params);

	Json::Value doCreateFamily(Json::Value& params);
	Json::Value doFamilyInfo(Json::Value& params);
	Json::Value doAccountInfo(Json::Value& params);
	Json::Value doNewAccount(Json::Value& params);
	Json::Value doLock(Json::Value& params);
	Json::Value doUnlock(Json::Value& params);
	Json::Value doSendTo(Json::Value& params);
	
public:
	typedef boost::shared_ptr<RPCServer> pointer;

	static pointer create(boost::asio::io_service& io_service)
	{
		return pointer(new RPCServer(io_service));
	}

	boost::asio::ip::tcp::socket& getSocket()
	{
		return mSocket;
	}

	void connected();

};
