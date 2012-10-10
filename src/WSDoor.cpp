
#include "WSDoor.h"

#include "Application.h"
#include "Config.h"
#include "Log.h"
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
// Might need to support this header for browsers: Access-Control-Allow-Origin: *
// - https://developer.mozilla.org/en-US/docs/HTTP_access_control
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
	Json::Value invokeCommand(const Json::Value& jvRequest);
	boost::unordered_set<NewcoinAddress> parseAccountIds(const Json::Value& jvArray);

	// Request-Response Commands
	void doLedgerAccept(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerClosed(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerCurrent(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerEntry(Json::Value& jvResult, const Json::Value& jvRequest);
	void doSubmit(Json::Value& jvResult, const Json::Value& jvRequest);

	// Streaming Commands
	void doAccountInfoSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountInfoUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountTransactionSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doAccountTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);

	void doServerSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doServerUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
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
		// Request-Response Commands:
		{ "ledger_accept",						&WSConnection::doLedgerAccept					},
		{ "ledger_closed",						&WSConnection::doLedgerClosed					},
		{ "ledger_current",						&WSConnection::doLedgerCurrent					},
		{ "ledger_entry",						&WSConnection::doLedgerEntry					},
		{ "submit",								&WSConnection::doSubmit							},

		// Streaming commands:
		{ "account_info_subscribe",				&WSConnection::doAccountInfoSubscribe			},
		{ "account_info_unsubscribe",			&WSConnection::doAccountInfoUnsubscribe			},
		{ "account_transaction_subscribe",		&WSConnection::doAccountTransactionSubscribe	},
		{ "account_transaction_unsubscribe",	&WSConnection::doAccountTransactionUnsubscribe	},
		{ "ledger_accounts_subscribe",			&WSConnection::doLedgerAccountsSubcribe			},
		{ "ledger_accounts_unsubscribe",		&WSConnection::doLedgerAccountsUnsubscribe		},
		{ "server_subscribe",					&WSConnection::doServerSubscribe				},
		{ "server_unsubscribe",					&WSConnection::doServerUnsubscribe				},
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

			mNetwork.subAccountInfo(this, usnaAccoundIds);
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

			mNetwork.unsubAccountInfo(this, usnaAccoundIds);
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

			mNetwork.subAccountTransaction(this, usnaAccoundIds);
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

			mNetwork.unsubAccountTransaction(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doLedgerAccountsSubcribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.subLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsSubscribed";
	}
}

void WSConnection::doLedgerAccountsUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.unsubLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsNotSubscribed";
	}
}

void WSConnection::doLedgerAccept(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!theConfig.RUN_STANDALONE)
	{
		jvResult["error"]	= "notStandAlone";
	}
	else
	{
		theApp->getOPs().acceptLedger();

		jvResult["ledger_current_index"]	= mNetwork.getCurrentLedgerID();
	}
}

void WSConnection::doLedgerClosed(Json::Value& jvResult, const Json::Value& jvRequest)
{
	uint256	uLedger	= mNetwork.getClosedLedger();

	jvResult["ledger_closed_index"]		= mNetwork.getLedgerID(uLedger);
	jvResult["ledger_closed"]			= uLedger.ToString();
}

void WSConnection::doLedgerCurrent(Json::Value& jvResult, const Json::Value& jvRequest)
{
	jvResult["ledger_current_index"]	= mNetwork.getCurrentLedgerID();
}

void WSConnection::doLedgerEntry(Json::Value& jvResult, const Json::Value& jvRequest)
{
	NetworkOPs&	noNetwork	= mNetwork;
	uint256	uLedger			= jvRequest.isMember("ledger") ? uint256(jvRequest["ledger"].asString()) : 0;
	uint32	uLedgerIndex	= jvRequest.isMember("ledger_index") && jvRequest["ledger_index"].isNumeric() ? jvRequest["ledger_index"].asUInt() : 0;

	Ledger::pointer	 lpLedger;

	if (!!uLedger)
	{
		// Ledger directly specified.
		lpLedger	= noNetwork.getLedgerByHash(uLedger);

		if (!lpLedger)
		{
			jvResult["error"]	= "ledgerNotFound";
			return;
		}

		uLedgerIndex	= lpLedger->getLedgerSeq();	// Set the current index, override if needed.
	}
	else if (!!uLedgerIndex)
	{
		lpLedger		= noNetwork.getLedgerBySeq(uLedgerIndex);

		if (!lpLedger)
		{
			jvResult["error"]	= "ledgerNotFound";	// ledger_index from future?
			return;
		}
	}
	else
	{
		// Default to current ledger.
		lpLedger		= noNetwork.getCurrentLedger();
		uLedgerIndex	= lpLedger->getLedgerSeq();	// Set the current index.
	}

	if (lpLedger->isClosed())
	{
		if (!!uLedger)
			jvResult["ledger_closed"]			= uLedger.ToString();

		jvResult["ledger_closed_index"]		= uLedgerIndex;
	}
	else
	{
		jvResult["ledger_current_index"]	= uLedgerIndex;
	}

	uint256		uNodeIndex;
	bool		bNodeBinary	= false;

	if (jvRequest.isMember("index"))
	{
		// XXX Needs to provide proof.
		uNodeIndex.SetHex(jvRequest["index"].asString());
		bNodeBinary	= true;
	}
	else if (jvRequest.isMember("account_root"))
	{
		NewcoinAddress	naAccount;

		if (!naAccount.setAccountID(jvRequest["account_root"].asString()))
		{
			jvResult["error"]	= "malformedAddress";
		}
		else
		{
			uNodeIndex = Ledger::getAccountRootIndex(naAccount.getAccountID());
		}
	}
	else if (jvRequest.isMember("directory"))
	{

		if (!jvRequest.isObject())
		{
			uNodeIndex.SetHex(jvRequest["directory"].asString());
		}
		else if (jvRequest["directory"].isMember("sub_index")
			&& !jvRequest["directory"]["sub_index"].isIntegral())
		{
			jvResult["error"]	= "malformedRequest";
		}
		else
		{
			uint64	uSubIndex = jvRequest["directory"].isMember("sub_index")
									? jvRequest["directory"]["sub_index"].asUInt()
									: 0;

			if (jvRequest["directory"].isMember("dir_root"))
			{
				uint256	uDirRoot;

				uDirRoot.SetHex(jvRequest["dir_root"].asString());

				uNodeIndex	= Ledger::getDirNodeIndex(uDirRoot, uSubIndex);
			}
			else if (jvRequest["directory"].isMember("owner"))
			{
				NewcoinAddress	naOwnerID;

				if (!naOwnerID.setAccountID(jvRequest["directory"]["owner"].asString()))
				{
					jvResult["error"]	= "malformedAddress";
				}
				else
				{
					uint256	uDirRoot	= Ledger::getOwnerDirIndex(naOwnerID.getAccountID());

					uNodeIndex	= Ledger::getDirNodeIndex(uDirRoot, uSubIndex);
				}
			}
			else
			{
				jvResult["error"]	= "malformedRequest";
			}
		}
	}
	else if (jvRequest.isMember("generator"))
	{
		NewcoinAddress	naGeneratorID;

		if (!jvRequest.isObject())
		{
			uNodeIndex.SetHex(jvRequest["generator"].asString());
		}
		else if (!jvRequest["generator"].isMember("regular_seed"))
		{
			jvResult["error"]	= "malformedRequest";
		}
		else if (!naGeneratorID.setSeedGeneric(jvRequest["generator"]["regular_seed"].asString()))
		{
			jvResult["error"]	= "malformedAddress";
		}
		else
		{
			NewcoinAddress		na0Public;		// To find the generator's index.
			NewcoinAddress		naGenerator	= NewcoinAddress::createGeneratorPublic(naGeneratorID);

			na0Public.setAccountPublic(naGenerator, 0);

			uNodeIndex	= Ledger::getGeneratorIndex(na0Public.getAccountID());
		}
	}
	else if (jvRequest.isMember("offer"))
	{
		NewcoinAddress	naAccountID;

		if (!jvRequest.isObject())
		{
			uNodeIndex.SetHex(jvRequest["offer"].asString());
		}
		else if (!jvRequest["offer"].isMember("account")
			|| !jvRequest["offer"].isMember("seq")
			|| !jvRequest["offer"]["seq"].isIntegral())
		{
			jvResult["error"]	= "malformedRequest";
		}
		else if (!naAccountID.setAccountID(jvRequest["offer"]["account"].asString()))
		{
			jvResult["error"]	= "malformedAddress";
		}
		else
		{
			uint32		uSequence	= jvRequest["offer"]["seq"].asUInt();

			uNodeIndex	= Ledger::getOfferIndex(naAccountID.getAccountID(), uSequence);
		}
	}
	else if (jvRequest.isMember("ripple_state"))
	{
		NewcoinAddress	naA;
		NewcoinAddress	naB;
		uint160			uCurrency;

		if (!jvRequest.isMember("accounts")
			|| !jvRequest.isMember("currency")
			|| !jvRequest["accounts"].isArray()
			|| 2 != jvRequest["accounts"].size()) {
			jvResult["error"]	= "malformedRequest";
		}
		else if (!naA.setAccountID(jvRequest["accounts"][0u].asString())
			|| !naB.setAccountID(jvRequest["accounts"][1u].asString())) {
			jvResult["error"]	= "malformedAddress";
		}
		else if (!STAmount::currencyFromString(uCurrency, jvRequest["currency"].asString())) {
			jvResult["error"]	= "malformedCurrency";
		}
		else
		{
			uNodeIndex	= Ledger::getRippleStateIndex(naA, naB, uCurrency);
		}
	}
	else
	{
		jvResult["error"]	= "unknownOption";
	}

	if (!!uNodeIndex)
	{
		SLE::pointer	sleNode	= noNetwork.getSLE(lpLedger, uNodeIndex);

		if (!sleNode)
		{
			// Not found.
			// XXX Should also provide proof.
			jvResult["error"]		= "entryNotFound";
		}
		else if (bNodeBinary)
		{
			// XXX Should also provide proof.
			Serializer s;

			sleNode->add(s);

			jvResult["node_binary"]	= strHex(s.peekData());
			jvResult["index"]		= uNodeIndex.ToString();
		}
		else
		{
			jvResult["node"]		= sleNode->getJson(0);
			jvResult["index"]		= uNodeIndex.ToString();
		}
	}
}

void WSConnection::doServerSubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.subLedger(this))
	{
		jvResult["error"]	= "serverSubscribed";
	}
	else
	{
		if (theConfig.RUN_STANDALONE)
			jvResult["stand_alone"]	= 1;

		// XXX Make sure these values are available before returning them.
		// XXX return connected status.
		jvResult["ledger_closed"]			= mNetwork.getClosedLedger().ToString();
		jvResult["ledger_current_index"]	= mNetwork.getCurrentLedgerID();
	}
}

void WSConnection::doServerUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.unsubLedger(this))
	{
		jvResult["error"]	= "serverNotSubscribed";
	}
}

// XXX Current requires secret. Allow signed transaction as an alternative.
void WSConnection::doSubmit(Json::Value& jvResult, const Json::Value& jvRequest)
{
	NewcoinAddress	naAccount;

	if (!jvRequest.isMember("transaction"))
	{
		jvResult["error"]	= "fieldNotFoundTransaction";
	}
	else if (!jvRequest["transaction"].isMember("Account"))
	{
		jvResult["error"]	= "fieldNotFoundAccount";
	}
	else if (!naAccount.setAccountID(jvRequest["transaction"]["Account"].asString()))
	{
		jvResult["error"]	= "malformedAccount";
	}
	else if (!jvRequest.isMember("secret"))
	{
		jvResult["error"]	= "fieldNotFoundSecret";
	}
	else
	{
		Ledger::pointer	lpCurrent		= mNetwork.getCurrentLedger();
		SLE::pointer	sleAccountRoot	= mNetwork.getSLE(lpCurrent, Ledger::getAccountRootIndex(naAccount.getAccountID()));

		if (!sleAccountRoot)
		{
			// XXX Ignore transactions for accounts not created.

			jvResult["error"]	= "accountNotFound";
			return;
		}

		bool			bHaveAuthKey	= false;
		NewcoinAddress	naAuthorizedPublic;
#if 0

		if (sleAccountRoot->isFieldPresent(sfAuthorizedKey))
		{
			naAuthorizedPublic	= mLedgerEntry->getFieldAccount(sfAuthorizedKey);
			// Json::Value	obj	= getMasterGenerator(uLedger, naRegularSeed, naMasterGenerator);
		}
#endif

		NewcoinAddress	naSecret			= NewcoinAddress::createSeedGeneric(jvRequest["secret"].asString());
		NewcoinAddress	naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naSecret);

		// Find the index of Account from the master generator, so we can generate the public and private keys.
		NewcoinAddress		naMasterAccountPublic;
		unsigned int		iIndex	= 0;
		bool				bFound	= false;

		// Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
		// related.
		while (!bFound && iIndex != theConfig.ACCOUNT_PROBE_MAX)
		{
			naMasterAccountPublic.setAccountPublic(naMasterGenerator, iIndex);

			Log(lsWARNING) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID() << " : " << naAccount.humanAccountID();

			bFound	= naAccount.getAccountID() == naMasterAccountPublic.getAccountID();
			if (!bFound)
				++iIndex;
		}

		if (!bFound)
		{
			jvResult["error"]	= "accountNotMatched";
			return;
		}

		// Use the generator to determine the associated public and private keys.
		NewcoinAddress	naGenerator			= NewcoinAddress::createGeneratorPublic(naSecret);
		NewcoinAddress	naAccountPublic		= NewcoinAddress::createAccountPublic(naGenerator, iIndex);
		NewcoinAddress	naAccountPrivate	= NewcoinAddress::createAccountPrivate(naGenerator, naSecret, iIndex);

		if (bHaveAuthKey
			// The generated pair must match authorized...
			&& naAuthorizedPublic.getAccountID() != naAccountPublic.getAccountID()
			// ... or the master key must have been used.
			&& naAccount.getAccountID() != naAccountPublic.getAccountID())
		{
			// std::cerr << "iIndex: " << iIndex << std::endl;
			// std::cerr << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID()) << std::endl;
			// std::cerr << "naAccountPublic: " << strHex(naAccountPublic.getAccountID()) << std::endl;

			jvResult["error"]	= "passwordChanged";
			return;
		}

		std::auto_ptr<STObject>	sopTrans;

		try
		{
			sopTrans = STObject::parseJson(jvRequest["transaction"]);
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "malformedTransaction";
			jvResult["error_exception"]	= e.what();
			return;
		}

		sopTrans->setFieldVL(sfSigningPubKey, naAccountPublic.getAccountPublic());

		SerializedTransaction::pointer	stpTrans	= boost::make_shared<SerializedTransaction>(*sopTrans);

		stpTrans->sign(naAccountPrivate);

		Transaction::pointer			tpTrans;

		try
		{
			tpTrans		= boost::make_shared<Transaction>(stpTrans, false);
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "internalTransaction";
			jvResult["error_exception"]	= e.what();
			return;
		}

		try
		{
			tpTrans	= mNetwork.submitTransaction(tpTrans);
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "internalSubmit";
			jvResult["error_exception"]	= e.what();
			return;
		}

		try
		{
			jvResult["submitted"]	= tpTrans->getJson(0);
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "internalJson";
			jvResult["error_exception"]	= e.what();
			return;
		}
	}
}

void WSConnection::doTransactionSubcribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.subTransaction(this))
	{
		jvResult["error"]	= "TransactionsSubscribed";
	}
}

void WSConnection::doTransactionUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.unsubTransaction(this))
	{
		jvResult["error"]	= "TransactionsNotSubscribed";
	}
}

// vim:ts=4
