#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"
#include "WSDoor.h"
#include "Application.h"

#include "Log.h"
#include "NetworkOPs.h"

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

	WSServerHandler<endpoint_type>*	mHandler;
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
	template <typename endpoint_type>
	void send(const Json::Value& jvObj);

	// Utilities
	template <typename endpoint_type>
	Json::Value invokeCommand(Json::Value& jvRequest);

};


// vim:ts=4
