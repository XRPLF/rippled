//
// WSConnection
//

#include "Log.h"

SETUP_LOG();

#include "WSConnection.h"
#include "WSHandler.h"

#include "../json/reader.h"
#include "../json/writer.h"

WSConnection::~WSConnection()
{
	mNetwork.unsubTransactions(this);
	mNetwork.unsubRTTransactions(this);
	mNetwork.unsubLedger(this);
	mNetwork.unsubServer(this);
	mNetwork.unsubAccount(this, mSubAccountInfo, true);
	mNetwork.unsubAccount(this, mSubAccountInfo, false);
}

void WSConnection::send(const Json::Value& jvObj)
{
	mHandler->send(mConnection, jvObj);
}

//
// Utilities
//

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

	Json::Value	jvResult(Json::objectValue);

	// Regular RPC command
	jvResult["result"] = theApp->getRPCHandler().doCommand(
		jvRequest["command"].asString(),
		jvRequest.isMember("params")
		? jvRequest["params"]
		: jvRequest,
		mHandler->getPublic() ? RPCHandler::GUEST : RPCHandler::ADMIN,
		this);

	// Currently we will simply unwrap errors returned by the RPC
	// API, in the future maybe we can make the responses
	// consistent.
	if (jvResult["result"].isObject() && jvResult["result"].isMember("error"))
	{
		jvResult = jvResult["result"];
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
