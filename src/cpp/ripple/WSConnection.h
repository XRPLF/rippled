
#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

#include "../json/value.h"

#include "WSDoor.h"
#include "Application.h"
#include "Log.h"
#include "NetworkOPs.h"
#include "CallRPC.h"

template <typename endpoint_type>
class WSServerHandler;
//
// Storage for connection specific info
// - Subscriptions
//
template <typename endpoint_type>
class WSConnection : public InfoSub
{
public:
	typedef typename endpoint_type::handler::connection_ptr connection_ptr;
	typedef typename endpoint_type::handler::message_ptr message_ptr;

protected:
	typedef void (WSConnection::*doFuncPtr)(Json::Value& jvResult, Json::Value &jvRequest);

	WSServerHandler<endpoint_type>*					 mHandler;
	connection_ptr									mConnection;
	NetworkOPs&										mNetwork;

public:
	//	WSConnection()
	//		: mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
	//			mConnection(connection_ptr()) { ; }

	WSConnection(WSServerHandler<endpoint_type>* wshpHandler, connection_ptr cpConnection)
		: mHandler(wshpHandler), mConnection(cpConnection), mNetwork(theApp->getOPs()) { ; }

	virtual ~WSConnection()
	{
		mNetwork.unsubTransactions(this);
		mNetwork.unsubRTTransactions(this);
		mNetwork.unsubLedger(this);
		mNetwork.unsubServer(this);
		mNetwork.unsubAccount(this, mSubAccountInfo, true);
		mNetwork.unsubAccount(this, mSubAccountInfo, false);
	}

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj)
	{
		mHandler->send(mConnection, jvObj);
	}

	// Utilities
	Json::Value invokeCommand(Json::Value& jvRequest)
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

		RPCHandler	mRPCHandler(&mNetwork, this);
		Json::Value	jvResult(Json::objectValue);

		jvResult["result"] = mRPCHandler.doCommand(
			jvRequest,
			mHandler->getPublic() ? RPCHandler::GUEST : RPCHandler::ADMIN);

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
};


// vim:ts=4
