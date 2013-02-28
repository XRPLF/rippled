
#include "../websocketpp/src/sockets/autotls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

#include "../json/value.h"

#include <boost/weak_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "WSDoor.h"
#include "Application.h"
#include "NetworkOPs.h"
#include "CallRPC.h"
#include "InstanceCounter.h"
#include "Log.h"
#include "RPCErr.h"

DEFINE_INSTANCE(WebSocketConnection);

#ifndef WEBSOCKET_PING_FREQUENCY
#define WEBSOCKET_PING_FREQUENCY (5*60)
#endif

template <typename endpoint_type>
class WSServerHandler;
//
// Storage for connection specific info
// - Subscriptions
//
template <typename endpoint_type>
class WSConnection : public InfoSub, public IS_INSTANCE(WebSocketConnection),
	public boost::enable_shared_from_this< WSConnection<endpoint_type> >
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
	std::string							mRemoteIP;

	boost::asio::deadline_timer			mPingTimer;
	bool								mPinged;

public:
	//	WSConnection()
	//		: mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
	//			mConnection(connection_ptr()) { ; }

	WSConnection(WSServerHandler<endpoint_type>* wshpHandler, const connection_ptr& cpConnection)
		: mHandler(wshpHandler), mConnection(cpConnection), mNetwork(theApp->getOPs()),
		mPingTimer(cpConnection->get_io_service()), mPinged(false)
	{
		mRemoteIP = cpConnection->get_socket().lowest_layer().remote_endpoint().address().to_string();
		cLog(lsDEBUG) << "Websocket connection from " << mRemoteIP;
		setPingTimer();
	}

	void preDestroy()
	{ // sever connection
		mPingTimer.cancel();
		mConnection.reset();
	}

	virtual ~WSConnection() { ; }

	static void destroy(boost::shared_ptr< WSConnection<endpoint_type> >)
	{ // Just discards the reference
	}

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj, bool broadcast)
	{
		connection_ptr ptr = mConnection.lock();
		if (ptr)
			mHandler->send(ptr, jvObj, broadcast);
	}

	// Utilities
	Json::Value invokeCommand(Json::Value& jvRequest)
	{
		if (!jvRequest.isMember("command"))
		{
			Json::Value	jvResult(Json::objectValue);

			jvResult["type"]	= "response";
			jvResult["status"]	= "error";
			jvResult["error"]	= "missingCommand";
			jvResult["request"] = jvRequest;

			if (jvRequest.isMember("id"))
			{
				jvResult["id"]	= jvRequest["id"];
			}

			return jvResult;
		}

		RPCHandler	mRPCHandler(&mNetwork, boost::shared_polymorphic_downcast<InfoSub>(this->shared_from_this()));
		Json::Value	jvResult(Json::objectValue);

		int iRole	= mHandler->getPublic()
						? RPCHandler::GUEST		// Don't check on the public interface.
						: iAdminGet(jvRequest, mRemoteIP);

		if (RPCHandler::FORBID == iRole)
		{
			jvResult["result"]	= rpcError(rpcFORBIDDEN);
		}
		else
		{
			jvResult["result"] = mRPCHandler.doCommand(jvRequest, iRole);
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

	bool onPingTimer(std::string&)
	{
#ifdef DISCONNECT_ON_WEBSOCKET_PING_TIMEOUTS
		if (mPinged)
			return true; // causes connection to close
#endif
		mPinged = true;
		setPingTimer();
		return false; // causes ping to be sent
	}

	void onPong(const std::string&)
	{
		mPinged = false;
	}

	static void pingTimer(weak_connection_ptr c, WSServerHandler<endpoint_type>* h, const boost::system::error_code& e)
	{
		if (e)
			return;

		connection_ptr ptr = c.lock();
		if (ptr)
			h->pingTimer(ptr);
	}

	void setPingTimer()
	{
		mPingTimer.expires_from_now(boost::posix_time::seconds(WEBSOCKET_PING_FREQUENCY));
		mPingTimer.async_wait(boost::bind(
			&WSConnection<endpoint_type>::pingTimer, mConnection, mHandler, boost::asio::placeholders::error));
	}

};

// vim:ts=4
