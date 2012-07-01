
#include "WSDoor.h"

#include "Application.h"
#include "Config.h"
#include "Log.h"
#include "NetworkOPs.h"
#include "NetworkOPs.h"
#include "utils.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/mem_fn.hpp>
#include <boost/unordered_set.hpp>

#include "../json/reader.h"
#include "../json/writer.h"

//
// This is a light weight, untrusted interface for web clients.
// For now we don't provide proof.  Later we will.
//

//
// Strategy:
// - We only talk to NetworkOPs (so we will work even in thin mode)
// - NetworkOPs is smart enough to subscribe and or pass back messages
//

// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}

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
	typedef void (WSConnection::*doFuncPtr)(Json::Value& jvResult, const Json::Value &jvRequest);

    boost::mutex									mLock;
	boost::unordered_set<NewcoinAddress>			mSubAccountInfo;
	boost::unordered_set<NewcoinAddress>			mSubAccountTransaction;

	WSServerHandler<websocketpp::WSDOOR_SERVER>*	mHandler;
	connection_ptr									mConnection;

public:
	WSConnection()
		: mHandler((WSServerHandler<websocketpp::WSDOOR_SERVER>*)(NULL)),
			mConnection(connection_ptr()) { ; }

	WSConnection(WSServerHandler<websocketpp::WSDOOR_SERVER>* wshpHandler, connection_ptr cpConnection)
		: mHandler(wshpHandler), mConnection(cpConnection) { ; }

	virtual ~WSConnection();

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj);

	// Utilities
	Json::Value invokeCommand(const Json::Value& jvRequest);
	boost::unordered_set<NewcoinAddress> parseAccountIds(const Json::Value& jvArray);

	// Commands
	void doAccountInfoSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountInfoUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountTransactionSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);

	void doLedgerSubcribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerAccountsSubcribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerAccountsUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doTransactionSubcribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
};

// A single instance of this object is made.
// This instance dispatches all events.  There is no per connection persistence.
template <typename endpoint_type>
class WSServerHandler : public endpoint_type::handler
{
public:
    typedef typename endpoint_type::handler::connection_ptr connection_ptr;
    typedef typename endpoint_type::handler::message_ptr message_ptr;

	// Private reasons to close.
	enum {
		crTooSlow	= 4000,		// Client is too slow.
	};

private:
    boost::shared_ptr<boost::asio::ssl::context>		mCtx;

protected:
    boost::mutex										mMapLock;
	// For each connection maintain an assoicated object to track subscriptions.
	boost::unordered_map<connection_ptr, boost::shared_ptr<WSConnection> >	mMap;

public:
	WSServerHandler(boost::shared_ptr<boost::asio::ssl::context> spCtx) : mCtx(spCtx) {}

    boost::shared_ptr<boost::asio::ssl::context> on_tls_init()
	{
		return mCtx;
	}

	void send(connection_ptr cpClient, message_ptr mpMessage)
	{
		try
		{
			cpClient->send(mpMessage->get_payload(), mpMessage->get_opcode());
		}
		catch (...)
		{
			cpClient->close(websocketpp::close::status::value(crTooSlow), std::string("Client is too slow."));
		}
	}

	void send(connection_ptr cpClient, const std::string& strMessage)
	{
		try
		{
			// Log(lsINFO) << "Ws:: Sending '" << strMessage << "'";

			cpClient->send(strMessage);
		}
		catch (...)
		{
			cpClient->close(websocketpp::close::status::value(crTooSlow), std::string("Client is too slow."));
		}
	}

	void send(connection_ptr cpClient, const Json::Value& jvObj)
	{
		Json::FastWriter	jfwWriter;

		// Log(lsINFO) << "Ws:: Object '" << jfwWriter.write(jvObj) << "'";

		send(cpClient, jfwWriter.write(jvObj));
	}

	void on_open(connection_ptr cpClient)
	{
		boost::mutex::scoped_lock	sl(mMapLock);

		mMap[cpClient]	= boost::make_shared<WSConnection>(this, cpClient);
	}

	void on_close(connection_ptr cpClient)
	{
		boost::mutex::scoped_lock	sl(mMapLock);

		mMap.erase(cpClient);
	}

    void on_message(connection_ptr cpClient, message_ptr mpMessage)
	{
		Json::Value		jvRequest;
		Json::Reader	jrReader;

		if (mpMessage->get_opcode() != websocketpp::frame::opcode::TEXT)
		{
			Json::Value	jvResult(Json::objectValue);

			jvResult["type"]	= "error";
			jvResult["error"]	= "wsTextRequired";	// We only accept text messages.

			send(cpClient, jvResult);
		}
		else if (!jrReader.parse(mpMessage->get_payload(), jvRequest) || jvRequest.isNull() || !jvRequest.isObject())
		{
			Json::Value	jvResult(Json::objectValue);

			jvResult["type"]	= "error";
			jvResult["error"]	= "jsonInvalid";	// Received invalid json.
			jvResult["value"]	= mpMessage->get_payload();

			send(cpClient, jvResult);
		}
		else
		{
			send(cpClient, mMap[cpClient]->invokeCommand(jvRequest));
		}
    }

	// Respond to http requests.
    void http(connection_ptr cpClient)
	{
        cpClient->set_body(
			"<!DOCTYPE html><html><head><title>" SYSTEM_NAME " Test</title></head>"
			"<body><h1>" SYSTEM_NAME " Test</h1><p>This page shows http(s) connectivity is working.</p></body></html>");
    }
};

void WSDoor::startListening()
{
	// Generate a single SSL context for use by all connections.
    boost::shared_ptr<boost::asio::ssl::context>	mCtx;
	mCtx	= boost::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

	mCtx->set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	SSL_CTX_set_tmp_dh_callback(mCtx->native_handle(), handleTmpDh);

	// Construct a single handler for all requests.
	websocketpp::WSDOOR_SERVER::handler::ptr	handler(new WSServerHandler<websocketpp::WSDOOR_SERVER>(mCtx));

	// Construct a websocket server.
	mEndpoint		= new websocketpp::WSDOOR_SERVER(handler);

	// mEndpoint->alog().unset_level(websocketpp::log::alevel::ALL);
	// mEndpoint->elog().unset_level(websocketpp::log::elevel::ALL);

	// Call the main-event-loop of the websocket server.
	mEndpoint->listen(
		boost::asio::ip::tcp::endpoint(
		boost::asio::ip::address().from_string(theConfig.WEBSOCKET_IP), theConfig.WEBSOCKET_PORT));

	delete mEndpoint;
}

WSDoor* WSDoor::createWSDoor()
{
	WSDoor*	wdpResult	= new WSDoor();

	if (!theConfig.WEBSOCKET_IP.empty() && theConfig.WEBSOCKET_PORT)
	{
		Log(lsINFO) << "Websocket: Listening: " << theConfig.WEBSOCKET_IP << " " << theConfig.WEBSOCKET_PORT;

		wdpResult->mThread	= new boost::thread(boost::bind(&WSDoor::startListening, wdpResult));
	}
	else
	{
		Log(lsINFO) << "Websocket: Disabled";
	}

	return wdpResult;
}

void WSDoor::stop()
{
	if (mThread)
	{
		mEndpoint->stop();

		mThread->join();
	}
}

//
// WSConnection
//

WSConnection::~WSConnection()
{
	theApp->getOPs().unsubTransaction(this);
	theApp->getOPs().unsubLedger(this);
	theApp->getOPs().unsubLedgerAccounts(this);
	theApp->getOPs().unsubAccountInfo(this, mSubAccountInfo);
	theApp->getOPs().unsubAccountTransaction(this, mSubAccountTransaction);
}

void WSConnection::send(const Json::Value& jvObj)
{
	mHandler->send(mConnection, jvObj);
}

//
// Utilities
//

Json::Value WSConnection::invokeCommand(const Json::Value& jvRequest)
{
	static struct {
		const char* pCommand;
		doFuncPtr	dfpFunc;
	} commandsA[] = {
		{ "account_info_subscribe",				&WSConnection::doAccountInfoSubscribe			},
		{ "account_info_unsubscribe",			&WSConnection::doAccountInfoUnsubscribe			},
		{ "account_transaction_subscribe",		&WSConnection::doAccountTransactionSubscribe	},
		{ "account_transaction_unsubscribe",	&WSConnection::doAccountTransactionUnsubscribe	},
		{ "ledger_subscribe",					&WSConnection::doLedgerSubcribe					},
		{ "ledger_unsubscribe",					&WSConnection::doLedgerUnsubscribe				},
		{ "ledger_accounts_subscribe",			&WSConnection::doLedgerAccountsSubcribe			},
		{ "ledger_accounts_unsubscribe",		&WSConnection::doLedgerAccountsUnsubscribe		},
		{ "transaction_subscribe",				&WSConnection::doTransactionSubcribe			},
		{ "transaction_unsubscribe",			&WSConnection::doTransactionUnsubscribe			},
	};

	if (!jvRequest.isMember("command"))
	{
		Json::Value	jvResult(Json::objectValue);

		jvResult["type"]	= "response";
		jvResult["result"]	= "error";
		jvResult["error"]	= "missingCommand";
		jvResult["command"]	= jvRequest;

		return jvResult;
	}

	std::string	strCommand	= jvRequest["command"].asString();

	int		i = NUMBER(commandsA);

	while (i-- && strCommand != commandsA[i].pCommand)
		;

	Json::Value	jvResult(Json::objectValue);

	jvResult["type"]	= "response";

	if (i < 0)
	{
		jvResult["error"]	= "unknownCommand";	// Unknown command.
	}
	else
	{
		(this->*(commandsA[i].dfpFunc))(jvResult, jvRequest);
	}

	if (jvRequest.isMember("id"))
	{
		jvResult["id"]		= jvRequest["id"];
	}

	if (jvResult.isMember("error"))
	{
		jvResult["result"]	= "error";
		jvResult["request"]	= jvRequest;
	}
	else
	{
		jvResult["result"]	= "success";
	}

	return jvResult;
}

boost::unordered_set<NewcoinAddress> WSConnection::parseAccountIds(const Json::Value& jvArray)
{
	boost::unordered_set<NewcoinAddress>	usnaResult;

	for (Json::Value::const_iterator it = jvArray.begin(); it != jvArray.end(); it++)
	{
		NewcoinAddress	naString;

		if (!(*it).isString() || !naString.setAccountID((*it).asString()))
		{
			usnaResult.clear();
			break;
		}
		else
		{
			(void) usnaResult.insert(naString);
		}
	}

	return usnaResult;
}

//
// Commands
//

void WSConnection::doAccountInfoSubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!jvRequest.isMember("accounts"))
	{
		jvResult["error"]	= "missingField";
	}
	else if (jvRequest["accounts"].empty())
	{
		jvResult["error"]	= "emptySet";
	}
	else
	{
		boost::unordered_set<NewcoinAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const NewcoinAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountInfo.insert(naAccountID);
			}

			theApp->getOPs().subAccountInfo(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doAccountInfoUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!jvRequest.isMember("accounts"))
	{
		jvResult["error"]	= "missingField";
	}
	else if (jvRequest["accounts"].empty())
	{
		jvResult["error"]	= "emptySet";
	}
	else
	{
		boost::unordered_set<NewcoinAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const NewcoinAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountInfo.erase(naAccountID);
			}

			theApp->getOPs().unsubAccountInfo(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doAccountTransactionSubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!jvRequest.isMember("accounts"))
	{
		jvResult["error"]	= "missingField";
	}
	else if (jvRequest["accounts"].empty())
	{
		jvResult["error"]	= "emptySet";
	}
	else
	{
		boost::unordered_set<NewcoinAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const NewcoinAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountTransaction.insert(naAccountID);
			}

			theApp->getOPs().subAccountTransaction(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doAccountTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!jvRequest.isMember("accounts"))
	{
		jvResult["error"]	= "missingField";
	}
	else if (jvRequest["accounts"].empty())
	{
		jvResult["error"]	= "emptySet";
	}
	else
	{
		boost::unordered_set<NewcoinAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const NewcoinAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountTransaction.erase(naAccountID);
			}

			theApp->getOPs().unsubAccountTransaction(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doLedgerSubcribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().subLedger(this))
	{
		jvResult["error"]	= "ledgerSubscribed";
	}
}

void WSConnection::doLedgerUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().unsubLedger(this))
	{
		jvResult["error"]	= "ledgerNotSubscribed";
	}
}

void WSConnection::doLedgerAccountsSubcribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().subLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsSubscribed";
	}
}

void WSConnection::doLedgerAccountsUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().unsubLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsNotSubscribed";
	}
}

void WSConnection::doTransactionSubcribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().subTransaction(this))
	{
		jvResult["error"]	= "TransactionsSubscribed";
	}
}

void WSConnection::doTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theApp->getOPs().unsubTransaction(this))
	{
		jvResult["error"]	= "TransactionsNotSubscribed";
	}
}

// vim:ts=4
