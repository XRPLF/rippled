//
// WSConnection
//

#include "Log.h"

SETUP_LOG();

#include "CallRPC.h"	// XXX Remove this, don't provide support for RPC syntax.
#include "WSConnection.h"
#include "WSHandler.h"

#include "../json/reader.h"
#include "../json/writer.h"


//template <typename endpoint_type>
//WSConnection::~WSConnection()

//template WSConnection::~WSConnection<server>();
//template WSConnection::~WSConnection<server_tls>();

template <typename endpoint_type>
void WSConnection<endpoint_type>::send(const Json::Value& jvObj)
{
	mHandler->send(mConnection, jvObj);
}
template void WSConnection::send<server>(const Json::Value& jvObj);
template void WSConnection::send<server_tls>(const Json::Value& jvObj);

//
// Utilities
//
template <typename endpoint_type>
Json::Value WSConnection::invokeCommand(Json::Value& jvRequest)
{
	if (!jvRequest.isMember("command"))
	{
		Json::Value	jvResult(Json::objectValue);

		jvResult["type"]	= "response";
		jvResult["result"]	= "error";
		jvResult["error"]	= "missingCommand";
		jvResult["command"]	= jvRequest;

		return jvResult;
	}

	RPCHandler mRPCHandler(&mNetwork, this);
	Json::Value	jvResult(Json::objectValue);

	// XXX Temporarily support RPC style commands over websocket. Remove this.
	if (jvRequest.isMember("params"))
	{
		RPCParser	rpParser;

		Json::Value	jvRpcRequest	= rpParser.parseCommand(jvRequest["command"].asString(), jvRequest["params"]);

		if (jvRpcRequest.isMember("error"))
		{
			jvResult		= jvRpcRequest;
		}
		else
		{
			jvResult["result"] = mRPCHandler.doCommand(
				jvRpcRequest,
				mHandler->getPublic() ? RPCHandler::GUEST : RPCHandler::ADMIN);
		}
	}
	else
	{
		jvResult["result"] = mRPCHandler.doCommand(
			jvRequest,
			mHandler->getPublic() ? RPCHandler::GUEST : RPCHandler::ADMIN);
	}

	// Currently we will simply unwrap errors returned by the RPC
	// API, in the future maybe we can make the responses
	// consistent.
	//
	// Regularize result. This is duplicate code.
	if (jvResult["result"].isMember("error"))
	{
		jvResult			= jvResult["result"];
		jvResult["status"]	= "error";
		jvResult["request"]	= jvRequest;

	} else {
		jvResult["status"]	= "success";
	}

	if (jvRequest.isMember("id"))
	{
		jvResult["id"]		= jvRequest["id"];
	}

	jvResult["type"]		= "response";

	return jvResult;
}

// vim:ts=4
