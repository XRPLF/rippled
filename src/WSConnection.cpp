#include "WSConnection.h"
#include "WSHandler.h"

#include "../json/reader.h"
#include "../json/writer.h"
//
// WSConnection
//

SETUP_LOG();

WSConnection::~WSConnection()
{
	mNetwork.unsubTransaction(this);
	mNetwork.unsubLedger(this);
	mNetwork.unsubLedgerAccounts(this);
	mNetwork.unsubAccountInfo(this, mSubAccountInfo);
	mNetwork.unsubAccountTransaction(this, mSubAccountTransaction);
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
accounts
rt_accounts
*/
// TODO
void WSConnection::doSubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (jvRequest.isMember("streams"))
	{
		for (Json::Value::iterator it = jvRequest["streams"].begin(); it != jvRequest["streams"].end(); it++)
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

void WSConnection::doUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
{

}

void WSConnection::doAccountInfoSubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doAccountInfoUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doAccountTransactionSubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doAccountTransactionUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doLedgerAccountsSubcribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (!mNetwork.subLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsSubscribed";
	}
}

void WSConnection::doLedgerAccountsUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (!mNetwork.unsubLedgerAccounts(this))
	{
		jvResult["error"]	= "ledgerAccountsNotSubscribed";
	}
}

void WSConnection::doLedgerAccept(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doLedgerClosed(Json::Value& jvResult,  Json::Value& jvRequest)
{
	uint256	uLedger	= mNetwork.getClosedLedger();

	jvResult["ledger_closed_index"]		= mNetwork.getLedgerID(uLedger);
	jvResult["ledger_closed"]			= uLedger.ToString();
}

void WSConnection::doLedgerCurrent(Json::Value& jvResult,  Json::Value& jvRequest)
{
	jvResult["ledger_current_index"]	= mNetwork.getCurrentLedgerID();
}

void WSConnection::doLedgerEntry(Json::Value& jvResult,  Json::Value& jvRequest)
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
void WSConnection::doServerSubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doServerUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (!mNetwork.unsubLedger(this))
	{
		jvResult["error"]	= "serverNotSubscribed";
	}
}

void WSConnection::doRPC(Json::Value& jvResult, Json::Value& jvRequest)
{
	if (jvRequest.isMember("command") && jvRequest.isMember("params"))
	{
		jvResult=theApp->getRPCHandler().doCommand(jvRequest["command"].asString(),jvRequest["params"],RPCHandler::GUEST);

	}else jvResult["error"]	= "fieldNotCommand";

}

// XXX Currently requires secret. Allow signed transaction as an alternative.
void WSConnection::doSubmit(Json::Value& jvResult, Json::Value& jvRequest)
{
	if (!jvRequest.isMember("tx_json"))
	{
		jvResult["error"]	= "fieldNotFoundTransaction";
	}else if (!jvRequest.isMember("key"))
	{
		jvResult["error"]	= "fieldNotFoundKey";
	}else jvResult=theApp->getRPCHandler().handleJSONSubmit(jvRequest["key"].asString(),jvRequest["tx_json"]);
}

void WSConnection::doTransactionEntry(Json::Value& jvResult,  Json::Value& jvRequest)
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

void WSConnection::doTransactionSubcribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (!mNetwork.subTransaction(this))
	{
		jvResult["error"]	= "TransactionsSubscribed";
	}
}

void WSConnection::doTransactionUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
{
	if (!mNetwork.unsubTransaction(this))
	{
		jvResult["error"]	= "TransactionsNotSubscribed";
	}
}
