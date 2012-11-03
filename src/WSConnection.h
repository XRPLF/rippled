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
class WSConnection : public InfoSub
{
public:
	typedef websocketpp::WSDOOR_SERVER::handler::connection_ptr connection_ptr;
	typedef websocketpp::WSDOOR_SERVER::handler::message_ptr message_ptr;

protected:
	typedef void (WSConnection::*doFuncPtr)(Json::Value& jvResult, Json::Value &jvRequest);

	boost::mutex									mLock;
	boost::unordered_set<RippleAddress>			mSubAccountInfo;
	boost::unordered_set<RippleAddress>			mSubAccountTransaction;

	WSServerHandler<websocketpp::WSDOOR_SERVER>*	mHandler;
	connection_ptr									mConnection;
	NetworkOPs&										mNetwork;

public:
	//	WSConnection()
	//		: mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
	//			mConnection(connection_ptr()) { ; }

	WSConnection(WSServerHandler<websocketpp::WSDOOR_SERVER>* wshpHandler, connection_ptr cpConnection)
		: mHandler(wshpHandler), mConnection(cpConnection), mNetwork(theApp->getOPs()) { ; }

	virtual ~WSConnection();

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj);

	// Utilities
	Json::Value invokeCommand(Json::Value& jvRequest);
	boost::unordered_set<RippleAddress> parseAccountIds(const Json::Value& jvArray);

	// Commands
	void doSubmit(Json::Value& jvResult, Json::Value& jvRequest);
	void doRPC(Json::Value& jvResult, Json::Value& jvRequest);
	void doSubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);



	// deprecated
	void doLedgerAccept(Json::Value& jvResult,  Json::Value& jvRequest);
	void doLedgerClosed(Json::Value& jvResult,  Json::Value& jvRequest);
	void doLedgerCurrent(Json::Value& jvResult,  Json::Value& jvRequest);
	void doLedgerEntry(Json::Value& jvResult,  Json::Value& jvRequest);
	void doTransactionEntry(Json::Value& jvResult,  Json::Value& jvRequest);

	void doAccountInfoSubscribe(Json::Value& jvResult, Json::Value& jvRequest);
	void doAccountInfoUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doAccountTransactionSubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doAccountTransactionUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);

	void doServerSubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doServerUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doLedgerAccountsSubcribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doLedgerAccountsUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doTransactionSubcribe(Json::Value& jvResult,  Json::Value& jvRequest);
	void doTransactionUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest);
};