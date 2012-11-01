
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

SETUP_LOG();

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
	Json::Value invokeCommand(const Json::Value& jvRequest);
	boost::unordered_set<RippleAddress> parseAccountIds(const Json::Value& jvArray);

	// Commands
	void doSubmit(Json::Value& jvResult, const Json::Value& jvRequest);
	void doRPC(Json::Value& jvResult, const Json::Value& jvRequest);
	void doSubscribe(Json::Value& jvResult, const Json::Value& jvRequest);
	void doUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest);



	// deprecated
	void doLedgerAccept(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerClosed(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerCurrent(Json::Value& jvResult, const Json::Value& jvRequest);
	void doLedgerEntry(Json::Value& jvResult, const Json::Value& jvRequest);
	void doTransactionEntry(Json::Value& jvResult, const Json::Value& jvRequest);

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
			cLog(lsDEBUG) << "Ws:: Sending '" << strMessage << "'";

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

		// cLog(lsDEBUG) << "Ws:: Object '" << jfwWriter.write(jvObj) << "'";

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
		{ "transaction_entry",					&WSConnection::doTransactionEntry				},
		{ "subscribe",							&WSConnection::doSubscribe						},
		{ "unsubscribe",						&WSConnection::doUnsubscribe					},

		// deprecated
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

boost::unordered_set<RippleAddress> WSConnection::parseAccountIds(const Json::Value& jvArray)
{
	boost::unordered_set<RippleAddress>	usnaResult;

	for (Json::Value::const_iterator it = jvArray.begin(); it != jvArray.end(); it++)
	{
		RippleAddress	naString;

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

/*
server : Sends a message anytime the server status changes such as network connectivity.
ledger : Sends a message at every ledger close.
transactions : Sends a message for every transaction that makes it into a ledger.
rt_transactions
*/
// TODO
void WSConnection::doSubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (jvRequest.isMember("streams"))
	{
		for (Json::Value::const_iterator it = jvRequest["streams"].begin(); it != jvRequest["streams"].end(); it++)
		{
			if ((*it).isString() )
			{
				std::string streamName=(*it).asString();

				if(streamName=="server")
				{
					mNetwork.subLedgerAccounts(this);
				}else if(streamName=="ledger")
				{
					mNetwork.subLedgerAccounts(this);
				}else if(streamName=="transactions")
				{
					mNetwork.subTransaction(this);
				}else if(streamName=="rt_transactions")
				{
					mNetwork.subTransaction(this); // TODO
				}else
				{
					jvResult["error"]	= str(boost::format("Unknown stream: %s") % streamName);
				}
			}else
			{
				jvResult["error"]	= "malformedSteam";
			}
		}
	}

	if (jvRequest.isMember("rt_accounts"))
	{
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["rt_accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountInfo.insert(naAccountID);
			}

			mNetwork.subAccountInfo(this, usnaAccoundIds);
		}
	}

	if (jvRequest.isMember("accounts"))
	{
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mSubAccountInfo.insert(naAccountID);
			}

			mNetwork.subAccountInfo(this, usnaAccoundIds);
		}
	}
}

void WSConnection::doUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{

}

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
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		
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
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
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
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
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
		boost::unordered_set<RippleAddress> usnaAccoundIds	= parseAccountIds(jvRequest["accounts"]);

		if (usnaAccoundIds.empty())
		{
			jvResult["error"]	= "malformedAccount";
		}
		else
		{
			boost::mutex::scoped_lock	sl(mLock);

			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
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
		mNetwork.acceptLedger();

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
	uint256	uLedger			= jvRequest.isMember("ledger_closed") ? uint256(jvRequest["ledger_closed"].asString()) : 0;
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
		RippleAddress	naAccount;

		if (!naAccount.setAccountID(jvRequest["account_root"].asString())
			|| !naAccount.getAccountID())
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
				RippleAddress	naOwnerID;

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
		RippleAddress	naGeneratorID;

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
			RippleAddress		na0Public;		// To find the generator's index.
			RippleAddress		naGenerator	= RippleAddress::createGeneratorPublic(naGeneratorID);

			na0Public.setAccountPublic(naGenerator, 0);

			uNodeIndex	= Ledger::getGeneratorIndex(na0Public.getAccountID());
		}
	}
	else if (jvRequest.isMember("offer"))
	{
		RippleAddress	naAccountID;

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
		RippleAddress	naA;
		RippleAddress	naB;
		uint160			uCurrency;
		Json::Value		jvRippleState	= jvRequest["ripple_state"];

		if (!jvRippleState.isMember("currency")
			|| !jvRippleState.isMember("accounts")
			|| !jvRippleState["accounts"].isArray()
			|| 2 != jvRippleState["accounts"].size()
			|| !jvRippleState["accounts"][0u].isString()
			|| !jvRippleState["accounts"][1u].isString()
			|| jvRippleState["accounts"][0u].asString() == jvRippleState["accounts"][1u].asString()
			) {

			cLog(lsINFO)
				<< boost::str(boost::format("ledger_entry: ripple_state: accounts: %d currency: %d array: %d size: %d equal: %d")
					% jvRippleState.isMember("accounts")
					% jvRippleState.isMember("currency")
					% jvRippleState["accounts"].isArray()
					% jvRippleState["accounts"].size()
					% (jvRippleState["accounts"][0u].asString() == jvRippleState["accounts"][1u].asString())
					);

			jvResult["error"]	= "malformedRequest";
		}
		else if (!naA.setAccountID(jvRippleState["accounts"][0u].asString())
			|| !naB.setAccountID(jvRippleState["accounts"][1u].asString())) {
			jvResult["error"]	= "malformedAddress";
		}
		else if (!STAmount::currencyFromString(uCurrency, jvRippleState["currency"].asString())) {
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

// The objective is to allow the client to know the server's status. The only thing that show the server is fully operating is the
// stream of ledger_closeds. Therefore, that is all that is provided. A client can drop servers that do not provide recent
// ledger_closeds.
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

		if (NetworkOPs::omDISCONNECTED != mNetwork.getOperatingMode()) {
			jvResult["ledger_closed"]			= mNetwork.getClosedLedger().ToString();
			jvResult["ledger_current_index"]	= mNetwork.getCurrentLedgerID();
		}
	}
}

void WSConnection::doServerUnsubscribe(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!mNetwork.unsubLedger(this))
	{
		jvResult["error"]	= "serverNotSubscribed";
	}
}

void WSConnection::doRPC(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (jvRequest.isMember("command"))
	{
		// TODO
	}else jvResult["error"]	= "fieldNotCommand";
	
}

// XXX Current requires secret. Allow signed transaction as an alternative.
void WSConnection::doSubmit(Json::Value& jvResult, const Json::Value& jvRequest)
{
	RippleAddress	naAccount;

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
		RippleAddress	naAuthorizedPublic;
#if 0

		if (sleAccountRoot->isFieldPresent(sfAuthorizedKey))
		{
			naAuthorizedPublic	= mLedgerEntry->getFieldAccount(sfAuthorizedKey);
			// Json::Value	obj	= getMasterGenerator(uLedger, naRegularSeed, naMasterGenerator);
		}
#endif

		RippleAddress	naSecret			= RippleAddress::createSeedGeneric(jvRequest["secret"].asString());
		RippleAddress	naMasterGenerator	= RippleAddress::createGeneratorPublic(naSecret);

		// Find the index of Account from the master generator, so we can generate the public and private keys.
		RippleAddress		naMasterAccountPublic;
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
		RippleAddress	naGenerator			= RippleAddress::createGeneratorPublic(naSecret);
		RippleAddress	naAccountPublic		= RippleAddress::createAccountPublic(naGenerator, iIndex);
		RippleAddress	naAccountPrivate	= RippleAddress::createAccountPrivate(naGenerator, naSecret, iIndex);

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

		SerializedTransaction::pointer stpTrans;

		try
		{
			stpTrans = boost::make_shared<SerializedTransaction>(*sopTrans);
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "invalidTransaction";
			jvResult["error_exception"]	= e.what();
			return;
		}

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

			if (!tpTrans) {
				jvResult["error"]			= "invalidTransaction";
				jvResult["error_exception"]	= "Unable to sterilize transaction.";
				return;
			}
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "internalSubmit";
			jvResult["error_exception"]	= e.what();
			return;
		}

		try
		{
			jvResult["transaction"]		= tpTrans->getJson(0);

			if (temUNCERTAIN != tpTrans->getResult())
			{
				std::string	sToken;
				std::string	sHuman;

				transResultInfo(tpTrans->getResult(), sToken, sHuman);

				jvResult["engine_result"]			= sToken;
				jvResult["engine_result_code"]		= tpTrans->getResult();
				jvResult["engine_result_message"]	= sHuman;
			}
		}
		catch (std::exception& e)
		{
			jvResult["error"]			= "internalJson";
			jvResult["error_exception"]	= e.what();
			return;
		}
	}
}

void WSConnection::doTransactionEntry(Json::Value& jvResult, const Json::Value& jvRequest)
{
	if (!jvRequest.isMember("transaction"))
	{
		jvResult["error"]	= "fieldNotFoundTransaction";
	}
	if (!jvRequest.isMember("ledger_closed"))
	{
		jvResult["error"]	= "notYetImplemented";	// XXX We don't support any transaction yet.
	}
	else
	{
		uint256						uTransID;
		// XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
		uTransID.SetHex(jvRequest["transaction"].asString());

		uint256						uLedgerID;
		// XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
		uLedgerID.SetHex(jvRequest["ledger_closed"].asString());

		Ledger::pointer				lpLedger	= theApp->getMasterLedger().getLedgerByHash(uLedgerID);

		if (!lpLedger) {
			jvResult["error"]	= "ledgerNotFound";
		}
		else
		{
			Transaction::pointer		tpTrans;
			TransactionMetaSet::pointer	tmTrans;

			if (!lpLedger-> getTransaction(uTransID, tpTrans, tmTrans))
			{
				jvResult["error"]	= "transactionNotFound";
			}
			else
			{
				jvResult["transaction"]		= tpTrans->getJson(0);
				jvResult["metadata"]		= tmTrans->getJson(0);
				// 'accounts'
				// 'engine_...'
				// 'ledger_...'
			}
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
