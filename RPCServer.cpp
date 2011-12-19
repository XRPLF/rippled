
#include <iostream>

#include <boost/bind.hpp>

#include "json/value.h"
#include "json/reader.h"

#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "Application.h"
#include "RPC.h"
#include "Conversion.h"

/*
Just read from wire until the entire request is in.
*/

RPCServer::RPCServer(boost::asio::io_service& io_service)
	: mSocket(io_service)
{
}

void RPCServer::connected()
{
	//BOOST_LOG_TRIVIAL(info) << "RPC request";
	std::cout << "RPC request" << std::endl;

	mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
		boost::bind(&RPCServer::handle_read, shared_from_this(),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
}

void RPCServer::handle_read(const boost::system::error_code& e,
	std::size_t bytes_transferred)
{
	if(!e)
	{
		boost::tribool result;
		result = mRequestParser.parse(
			mIncomingRequest, mReadBuffer.data(), mReadBuffer.data() + bytes_transferred);

		if(result)
		{
//			mReplyStr=handleRequest(mIncomingRequest.mBody);
			sendReply();
		}
		else if(!result)
		{ // bad request
			std::cout << "bad request" << std::endl;
		}
		else
		{  // not done keep reading
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::handle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}
	else if(e != boost::asio::error::operation_aborted)
	{
		
	}
}



std::string RPCServer::handleRequest(const std::string& requestStr)
{
	std::cout << "handleRequest " << requestStr << std::endl;
	Json::Value id;
	
	// Parse request
	Json::Value valRequest;
	Json::Reader reader;
	if(!reader.parse(requestStr, valRequest) || valRequest.isNull() || !valRequest.isObject())
		return(HTTPReply(400, ""));

	// Parse id now so errors from here on will have the id
	id = valRequest["id"];

	// Parse method
	Json::Value valMethod = valRequest["method"];
	if (valMethod.isNull())
		return(HTTPReply(400, ""));
	if (!valMethod.isString())
		return(HTTPReply(400, ""));
	std::string strMethod = valMethod.asString();

	

	// Parse params
	Json::Value valParams = valRequest["params"];
	if (valParams.isNull())
		valParams = Json::Value(Json::arrayValue);
	else if(!valParams.isArray())
		return(HTTPReply(400, ""));

	Json::Value result=doCommand(strMethod, valParams);
	std::string strReply = JSONRPCReply(result, Json::Value(), id);
	return( HTTPReply(200, strReply) );
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	if(command== "stop")
	{
		mSocket.get_io_service().stop();

		return "newcoin server stopping";
	}
	if(command=="send")
	{

	}
	if(command== "addUNL")
	{
		if(params.size()==2)
		{
			uint160 hanko=humanTo160(params[0u].asString());
			std::vector<unsigned char> pubKey;
			humanToPK(params[1u].asString(),pubKey);
			theApp->getUNL().addNode(hanko,pubKey);
			return "adding node";
		}else return "invalid params";
	}
	if(command=="getUNL")
	{
		std::string str;
		theApp->getUNL().dumpUNL(str);
		return(str.c_str());
	}

	return "unknown command";
}

void RPCServer::sendReply()
{


	boost::asio::async_write(mSocket, boost::asio::buffer(mReplyStr),
			boost::bind(&RPCServer::handle_write, shared_from_this(),
			boost::asio::placeholders::error));
		
}

void RPCServer::handle_write(const boost::system::error_code& /*error*/)
{
	
}
