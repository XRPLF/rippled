
#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include <boost/bind.hpp>
//#include <boost/log/trivial.hpp>

#include <iostream>
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "RPC.h"


using namespace std;
using namespace json_spirit;

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
	cout << "RPC request" << endl;

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
			mReplyStr=handleRequest(mIncomingRequest.mBody);
			sendReply();
		}else if(!result)
		{ // bad request
			cout << "bad request" << endl;
		}else
		{  // not done keep reading
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::handle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}else if(e != boost::asio::error::operation_aborted)
	{
		
	}
}



std::string RPCServer::handleRequest(std::string& requestStr)
{
	cout << "handleRequest " << requestStr << endl;
	Value id = json_spirit::Value::null;
	
	// Parse request
	Value valRequest;
	if(!read_string(requestStr, valRequest) || valRequest.type() != obj_type)
		return(HTTPReply(400, ""));

	const Object& request = valRequest.get_obj();

	// Parse id now so errors from here on will have the id
	id = find_value(request, "id");

	// Parse method
	Value valMethod = find_value(request, "method");
	if (valMethod.type() == null_type)
		return(HTTPReply(400, ""));
	if (valMethod.type() != str_type)
		return(HTTPReply(400, ""));
	string strMethod = valMethod.get_str();

	

	// Parse params
	Value valParams = find_value(request, "params");
	Array params;
	if (valParams.type() == array_type)
		params = valParams.get_array();
	else if (valParams.type() == null_type)
		params = Array();
	else
		return(HTTPReply(400, ""));



	Value result=doCommand(strMethod,params);	
	string strReply = JSONRPCReply(result, Value::null, id);
	return( HTTPReply(200, strReply) );
}

Value RPCServer::doCommand(std::string& command, Array& params)
{
	if(command== "stop")
	{
		mSocket.get_io_service().stop();

		return "newcoin server stopping";
	}
	if(command=="send")
	{

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