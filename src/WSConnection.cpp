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
	mNetwork.unsubTransactions(this);
	mNetwork.unsubRTTransactions(this);
	mNetwork.unsubLedger(this);
	mNetwork.unsubServer(this);
	mNetwork.unsubAccount(this, mSubAccountInfo,true);
	mNetwork.unsubAccount(this, mSubAccountInfo,false);
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
		{ "submit",								&WSConnection::doSubmit					},
		{ "subscribe",							&WSConnection::doSubscribe				},
		{ "unsubscribe",						&WSConnection::doUnsubscribe			},
		{ "rpc",								&WSConnection::doRPC					},
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
					mNetwork.subServer(this);
				}else if(streamName=="ledger")
				{
					mNetwork.subLedger(this);
				}else if(streamName=="transactions")
				{
					mNetwork.subTransactions(this);
				}else if(streamName=="rt_transactions")
				{
					mNetwork.subRTTransactions(this); 
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

			mNetwork.subAccount(this, usnaAccoundIds,true);
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

			mNetwork.subAccount(this, usnaAccoundIds,false);
		}
	}
}

void WSConnection::doUnsubscribe(Json::Value& jvResult,  Json::Value& jvRequest)
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
					mNetwork.unsubServer(this);
				}else if(streamName=="ledger")
				{
					mNetwork.unsubLedger(this);
				}else if(streamName=="transactions")
				{
					mNetwork.unsubTransactions(this);
				}else if(streamName=="rt_transactions")
				{
					mNetwork.unsubRTTransactions(this); 
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

			mNetwork.unsubAccount(this, usnaAccoundIds,true);
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

			mNetwork.unsubAccount(this, usnaAccoundIds,false);
		}
	}
}



void WSConnection::doRPC(Json::Value& jvResult, Json::Value& jvRequest)
{
	if (jvRequest.isMember("rpc_command") )
	{
		jvResult=theApp->getRPCHandler().doCommand(jvRequest["rpc_command"].asString(),jvRequest["params"],RPCHandler::GUEST);

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
	}else 
	{
		jvResult=theApp->getRPCHandler().handleJSONSubmit(jvRequest["key"].asString(),jvRequest["tx_json"]);

		// TODO: track the transaction mNetwork.subSubmit(this, jvResult["tx hash"] );
	}
}

