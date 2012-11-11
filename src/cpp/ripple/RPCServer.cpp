#include "RPCServer.h"
#include "Log.h"

#include "HttpsClient.h"
#include "RPC.h"
#include "utils.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/read_until.hpp>



#include "../json/reader.h"
#include "../json/writer.h"

SETUP_LOG();

#ifndef RPC_MAXIMUM_QUERY
#define RPC_MAXIMUM_QUERY	(1024*1024)
#endif

RPCServer::RPCServer(boost::asio::io_service& io_service , NetworkOPs* nopNetwork)
	: mNetOps(nopNetwork), mSocket(io_service)
{

	mRole = RPCHandler::GUEST;
}



void RPCServer::connected()
{
	//std::cout << "RPC request" << std::endl;
	if (mSocket.remote_endpoint().address().to_string()=="127.0.0.1") mRole = RPCHandler::ADMIN;
	else mRole = RPCHandler::GUEST;

	boost::asio::async_read_until(mSocket, mLineBuffer, "\r\n",
		boost::bind(&RPCServer::handle_read_line, shared_from_this(), boost::asio::placeholders::error));
}

void RPCServer::handle_read_req(const boost::system::error_code& e)
{
	std::string req;

	if (mLineBuffer.size())
	{
		req.assign(boost::asio::buffer_cast<const char*>(mLineBuffer.data()), mLineBuffer.size());
		mLineBuffer.consume(mLineBuffer.size());
	}

	req += strCopy(mQueryVec);
	mReplyStr = handleRequest(req);
	boost::asio::async_write(mSocket, boost::asio::buffer(mReplyStr),
		boost::bind(&RPCServer::handle_write, shared_from_this(), boost::asio::placeholders::error));
}

void RPCServer::handle_read_line(const boost::system::error_code& e)
{
	if (e)
		return;

	HTTPRequestAction action = mHTTPRequest.consume(mLineBuffer);

	if (action == haDO_REQUEST)
	{ // request with no body
		cLog(lsWARNING) << "RPC HTTP request with no body";
		boost::system::error_code ignore_ec;
		mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignore_ec);
		return;
	}
	else if (action == haREAD_LINE)
	{
		boost::asio::async_read_until(mSocket, mLineBuffer, "\r\n",
			boost::bind(&RPCServer::handle_read_line, shared_from_this(),
			boost::asio::placeholders::error));
	}
	else if (action == haREAD_RAW)
	{
		int rLen = mHTTPRequest.getDataSize();
		if ((rLen < 0) || (rLen > RPC_MAXIMUM_QUERY))
		{
			cLog(lsWARNING) << "Illegal RPC request length " << rLen;
			boost::system::error_code ignore_ec;
			mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignore_ec);
			return;
		}

		int alreadyHave = mLineBuffer.size();

		if (alreadyHave < rLen)
		{
			mQueryVec.resize(rLen - alreadyHave);
			boost::asio::async_read(mSocket, boost::asio::buffer(mQueryVec),
				boost::bind(&RPCServer::handle_read_req, shared_from_this(), boost::asio::placeholders::error));
			cLog(lsTRACE) << "Waiting for completed request: " << rLen;
		}
		else
		{ // we have the whole thing
			mQueryVec.resize(0);
			handle_read_req(e);
		}
	}
	else
	{
		boost::system::error_code ignore_ec;
		mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignore_ec);
	}
}

std::string RPCServer::handleRequest(const std::string& requestStr)
{
	cLog(lsTRACE) << "handleRequest " << requestStr;
	Json::Value id;

	// Parse request
	Json::Value valRequest;
	Json::Reader reader;
	if (!reader.parse(requestStr, valRequest) || valRequest.isNull() || !valRequest.isObject())
		return(HTTPReply(400, "unable to parse request"));

	// Parse id now so errors from here on will have the id
	id = valRequest["id"];

	// Parse method
	Json::Value valMethod = valRequest["method"];
	if (valMethod.isNull())
		return(HTTPReply(400, "null method"));
	if (!valMethod.isString())
		return(HTTPReply(400, "method is not string"));
	std::string strMethod = valMethod.asString();

	// Parse params
	Json::Value valParams = valRequest["params"];
	if (valParams.isNull())
		valParams = Json::Value(Json::arrayValue);
	else if (!valParams.isArray())
		return(HTTPReply(400, "params unparseable"));

	RPCHandler mRPCHandler(mNetOps);

	cLog(lsTRACE) << valParams;
	Json::Value result = mRPCHandler.doCommand(strMethod, valParams,mRole);
	cLog(lsTRACE) << result;

	std::string strReply = JSONRPCReply(result, Json::Value(), id);
	return HTTPReply(200, strReply);
}


#if 0
// now, expire, n
bool RPCServer::parseAcceptRate(const std::string& sAcceptRate)
{
	if (!sAcceptRate.compare("expire"))
		0;

	return true;
}
#endif


void RPCServer::handle_write(const boost::system::error_code& e)
{
	//std::cout << "async_write complete " << e << std::endl;

	if (!e)
	{
		HTTPRequestAction action = mHTTPRequest.requestDone(false);
		if (action == haCLOSE_CONN)
		{
			boost::system::error_code ignored_ec;
			mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}
		else
		{
			boost::asio::async_read_until(mSocket, mLineBuffer, "\r\n",
				boost::bind(&RPCServer::handle_read_line, shared_from_this(), boost::asio::placeholders::error));
		}
	}

	if (e != boost::asio::error::operation_aborted)
	{
		//connection_manager_.stop(shared_from_this());
	}
}

// vim:ts=4
