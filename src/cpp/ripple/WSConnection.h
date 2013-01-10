
#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

#include "../json/value.h"

#include <boost/weak_ptr.hpp>

#include "WSDoor.h"
#include "Application.h"
#include "NetworkOPs.h"
#include "CallRPC.h"
#include "InstanceCounter.h"
#include "Log.h"

DEFINE_INSTANCE(WebSocketConnection);

#ifndef WEBSOCKET_PING_FREQUENCY
#define WEBSOCKET_PING_FREQUENCY 120
#endif

template <typename endpoint_type>
class WSServerHandler;
//
// Storage for connection specific info
// - Subscriptions
//
template <typename endpoint_type>
class WSConnection : public InfoSub, public IS_INSTANCE(WebSocketConnection)
{
public:
	typedef typename endpoint_type::connection_type connection;
	typedef typename boost::shared_ptr<connection> connection_ptr;
	typedef typename boost::weak_ptr<connection> weak_connection_ptr;
	typedef typename endpoint_type::handler::message_ptr message_ptr;

protected:
	typedef void (WSConnection::*doFuncPtr)(Json::Value& jvResult, Json::Value &jvRequest);

	WSServerHandler<endpoint_type>*		mHandler;
	weak_connection_ptr					mConnection;
	NetworkOPs&							mNetwork;

	boost::asio::deadline_timer			mPingTimer;
	bool								mPinged;

public:
	//	WSConnection()
	//		: mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
	//			mConnection(connection_ptr()) { ; }

	WSConnection(WSServerHandler<endpoint_type>* wshpHandler, const connection_ptr& cpConnection)
		: mHandler(wshpHandler), mConnection(cpConnection), mNetwork(theApp->getOPs()),
		mPingTimer(theApp->getAuxService()), mPinged(false)
	{ setPingTimer(); }

	void preDestroy()
	{ // sever connection
		mConnection.reset();
	}

	virtual ~WSConnection() { ; }

	static void destroy(boost::shared_ptr< WSConnection<endpoint_type> >)
	{ // Just discards the reference
	}

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj)
	{
		connection_ptr ptr = mConnection.lock();
		if (ptr)
			mHandler->send(ptr, jvObj);
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

	bool onPingTimer()
	{
		if (mPinged)
			return true;
		mPinged = true;
		setPingTimer();
		return false;
	}

	void onPong()
	{
		mPinged = false;
	}

	static void pingTimer(weak_connection_ptr c, WSServerHandler<endpoint_type>* h)
	{
		connection_ptr ptr = c.lock();
		if (ptr)
			h->pingTimer(ptr);
	}

	void setPingTimer()
	{
		mPingTimer.expires_from_now(boost::posix_time::seconds(WEBSOCKET_PING_FREQUENCY));
		mPingTimer.async_wait(boost::bind(&WSConnection<endpoint_type>::pingTimer, mConnection, mHandler));
	}

};


// vim:ts=4
