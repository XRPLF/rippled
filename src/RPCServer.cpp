
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "../json/reader.h"
#include "../json/writer.h"

#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "Application.h"
#include "RPC.h"
#include "Wallet.h"
#include "Conversion.h"
#include "LocalTransaction.h"
#include "NewcoinAddress.h"
#include "AccountState.h"

/*
Just read from wire until the entire request is in.
*/

RPCServer::RPCServer(boost::asio::io_service& io_service)
	: mSocket(io_service)
{
}

void RPCServer::connected()
{
	//BOOST_LOG_TRIVIAL(info) << "RPC request";
	std::cout << "RPC request" << std::endl;

	mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
		boost::bind(&RPCServer::handle_read, shared_from_this(),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
}

void RPCServer::handle_read(const boost::system::error_code& e,
	std::size_t bytes_transferred)
{
	if(!e)
	{
		boost::tribool result;
		result = mRequestParser.parse(
			mIncomingRequest, mReadBuffer.data(), mReadBuffer.data() + bytes_transferred);

		if(result)
		{
			mReplyStr=handleRequest(mIncomingRequest.mBody);
			sendReply();
		}
		else if(!result)
		{ // bad request
			std::cout << "bad request" << std::endl;
		}
		else
		{  // not done keep reading
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::handle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}
	else if(e != boost::asio::error::operation_aborted)
	{

	}
}

std::string RPCServer::handleRequest(const std::string& requestStr)
{
	std::cout << "handleRequest " << requestStr << std::endl;
	Json::Value id;

	// Parse request
	Json::Value valRequest;
	Json::Reader reader;
	if(!reader.parse(requestStr, valRequest) || valRequest.isNull() || !valRequest.isObject())
		return(HTTPReply(400, ""));

	// Parse id now so errors from here on will have the id
	id = valRequest["id"];

	// Parse method
	Json::Value valMethod = valRequest["method"];
	if (valMethod.isNull())
		return(HTTPReply(400, ""));
	if (!valMethod.isString())
		return(HTTPReply(400, ""));
	std::string strMethod = valMethod.asString();

	// Parse params
	Json::Value valParams = valRequest["params"];
	if (valParams.isNull())
		valParams = Json::Value(Json::arrayValue);
	else if(!valParams.isArray())
		return(HTTPReply(400, ""));

#ifdef DEBUG
	Json::StyledStreamWriter w;
	w.write(std::cerr, valParams);
#endif

	Json::Value result(doCommand(strMethod, valParams));

#ifdef DEBUG
	w.write(std::cerr, result);
#endif

	std::string strReply = JSONRPCReply(result, Json::Value(), id);
	return( HTTPReply(200, strReply) );
}

int RPCServer::getParamCount(const Json::Value& params)
{ // If non-array, only counts strings
	if(params.isNull()) return 0;
	if(params.isArray()) return params.size();
	if(!params.isConvertibleTo(Json::stringValue))
		return 0;
	return 1;
}

bool RPCServer::extractString(std::string& param, const Json::Value& params, int index)
{
	if(params.isNull()) return false;

	if(index!=0)
	{
		if(!params.isArray() || !params.isValidIndex(index))
			return false;
		Json::Value p(params.get(index, Json::nullValue));
		if(p.isNull() || !p.isConvertibleTo(Json::stringValue))
			return false;
		param = p.asString();
		return true;
	}

	if(params.isArray())
	{
		if( (!params.isValidIndex(0)) || (!params[0u].isConvertibleTo(Json::stringValue)) )
			return false;
		param = params[0u].asString();
		return true;
	}

	if(!params.isConvertibleTo(Json::stringValue))
		return false;
	param = params.asString();
	return true;
}

NewcoinAddress RPCServer::parseFamily(const std::string& fParam)
{
	NewcoinAddress	family;

	if(family.setFamilyGenerator(fParam))
	{
		family = theApp->getWallet().findFamilyPK(family);
	}

	return family;
}

Json::Value RPCServer::doCreateFamily(Json::Value& params)
{
	// createfamily FXXXX
	// createfamily fXXXX
	// createfamily "<pass phrase>"
	// createfamily

	std::string query;
	NewcoinAddress family;
	NewcoinAddress seed;

	if(!extractString(query, params, 0))
	{
		// No parameters, generate a family from a random seed.
		family=theApp->getWallet().addRandomFamily(seed);
	}
	else if(seed.setFamilySeed(query))
	{
		// Had a family seed.
		family=theApp->getWallet().addFamily(seed, false);
	}
	else if(family.setFamilyGenerator(query))
	{
		// Had a public generator
		family=theApp->getWallet().addFamily(family);
	}
	else
	{
		// Must be a pass phrase.
		family=theApp->getWallet().addFamily(query, false);
	}

	if(!family.IsValid())
		return JSONRPCError(500, "Invalid family specifier");

	Json::Value ret(theApp->getWallet().getFamilyJson(family));
	if(ret.isNull()) return JSONRPCError(500, "Invalid family");
	if(seed.IsValid())
	{
		ret["FamilySeed"]=seed.humanFamilySeed();
	}

	return ret;
}

Json::Value RPCServer::doAccountInfo(Json::Value &params)
{   // accountinfo <family>:<number>
	// accountinfo <account>
	std::string acct;
	if(!extractString(acct, params, 0))
		return JSONRPCError(500, "Invalid account identifier");

	LocalAccount::pointer account=theApp->getWallet().parseAccount(acct);
	if(account) return account->getJson();

	NewcoinAddress acctid;
	if (!acctid.setAccountID(acct))
		return JSONRPCError(500, "Unable to parse account");

	LocalAccount::pointer lac(theApp->getWallet().getLocalAccount(acctid));
	if(!!lac) return lac->getJson();

	AccountState::pointer as=theApp->getMasterLedger().getCurrentLedger()->getAccountState(acctid);
	Json::Value ret(Json::objectValue);
	if(as)
		as->addJson(ret);
	else
	{
		NewcoinAddress ad;
		ad.setAccountID(acct);
		ret[ad.humanAccountID()]="NotFound";
	}
	return ret;
}

Json::Value RPCServer::doNewAccount(Json::Value &params)
{   // newaccount <family> [<name>]
	std::string fParam;
	if(!extractString(fParam, params, 0))
		return JSONRPCError(500, "Family required");

	NewcoinAddress family = parseFamily(fParam);
	if(!family.IsValid()) return JSONRPCError(500, "Family not found.");

	LocalAccount::pointer account(theApp->getWallet().getNewLocalAccount(family));
	if(!account)
		return JSONRPCError(500, "Family not found");

	return account->getJson();
}

Json::Value RPCServer::doLock(Json::Value &params)
{   // lock <family>
	// lock
	std::string fParam;

	if(extractString(fParam, params, 0))
	{ // local <family>
		NewcoinAddress family = parseFamily(fParam);
		if(!family.IsValid()) return JSONRPCError(500, "Family not found");

		theApp->getWallet().lock(family);
	}
	else
	{
		theApp->getWallet().lock();
	}

	return "locked";
}

Json::Value RPCServer::doUnlock(Json::Value &params)
{   // unlock sXXXX
    // unlock "<pass phrase>"

	std::string param;
	NewcoinAddress familyGenerator;

	if(!extractString(param, params, 0) || familyGenerator.setFamilyGenerator(param))
		return JSONRPCError(500, "Private key required");

	NewcoinAddress family;
	NewcoinAddress familySeed;

	if(familySeed.setFamilySeed(param))
		// sXXX
		family=theApp->getWallet().addFamily(familySeed, false);
	else
		// pass phrase
		family=theApp->getWallet().addFamily(param, false);

	if(!family.IsValid())
		return JSONRPCError(500, "Bad family");

	Json::Value ret(theApp->getWallet().getFamilyJson(family));
	if(ret.isNull()) return JSONRPCError(500, "Invalid family");

	return ret;
}

Json::Value RPCServer::doFamilyInfo(Json::Value &params)
{
	// familyinfo <family>
	// familyinfo <family> <number>
	// familyinfo
	int paramCount=getParamCount(params);

	if(paramCount==0)
	{
		std::vector<NewcoinAddress> familyIDs;
		theApp->getWallet().getFamilies(familyIDs);

		Json::Value ret(Json::arrayValue);
		BOOST_FOREACH(const NewcoinAddress& fid, familyIDs)
		{
			Json::Value obj(theApp->getWallet().getFamilyJson(fid));
			if(!obj.isNull()) ret.append(obj);
		}
		return ret;
	}

	if(paramCount>2) return JSONRPCError(500, "Invalid parameters");
	std::string fParam;
	extractString(fParam, params, 0);

	NewcoinAddress family=parseFamily(fParam);
	if(!family.IsValid()) return JSONRPCError(500, "No such family");

	Json::Value obj(theApp->getWallet().getFamilyJson(family));
	if(obj.isNull())
		return JSONRPCError(500, "Family not found");

	if(paramCount==2)
	{
		std::string keyNum;
		extractString(keyNum, params, 1);
		int kn=boost::lexical_cast<int>(keyNum);
		NewcoinAddress k=theApp->getWallet().peekKey(family, kn);

		if(k.IsValid())
		{
			Json::Value key(Json::objectValue);
			key["Number"]=kn;
			key["Address"]=k.humanAccountID();
			obj["Account"]=key;
		}
	}

	return obj;
}

Json::Value RPCServer::doConnect(Json::Value& params)
{
	// connect <ip> [port]
	std::string host, port;

	if(!extractString(host, params, 0))
		return JSONRPCError(500, "Host required");
	if(!extractString(port, params, 1))
		port="6561";
	if(!theApp->getConnectionPool().connectTo(host, port))
		return JSONRPCError(500, "Unable to connect");
	return "connecting";
}

Json::Value RPCServer::doPeers(Json::Value& params)
{
	// peers
	return theApp->getConnectionPool().getPeersJson();
}

Json::Value RPCServer::doSendTo(Json::Value& params)
{   // Implement simple sending without gathering
	// sendto <destination> <amount>
	// sendto <destination> <amount> <tag>
	if(!params.isArray() || (params.size()<2))
		return JSONRPCError(500, "Invalid parameters");

	int paramCount=getParamCount(params);
	if((paramCount<2)||(paramCount>3))
		return JSONRPCError(500, "Invalid parameters");

	std::string sDest, sAmount;
	if(!extractString(sDest, params, 0) || !extractString(sAmount, params, 1))
		return JSONRPCError(500, "Invalid parameters");

	NewcoinAddress destAccount	= parseAccount(sDest);
	if(!destAccount.IsValid())
		return JSONRPCError(500, "Unable to parse destination account");

	uint64 iAmount;
	try
	{
		iAmount=boost::lexical_cast<uint64>(sAmount);
		if(iAmount<=0) return JSONRPCError(500, "Invalid amount");
	}
	catch (...)
	{
		return JSONRPCError(500, "Invalid amount");
	}

	uint32 iTag(0);
	try
	{
		if(paramCount>2)
		{
			std::string sTag;
			extractString(sTag, params, 2);
			iTag=boost::lexical_cast<uint32>(sTag);
		}
	}
	catch (...)
	{
		return JSONRPCError(500, "Invalid tag");
	}

#ifdef DEBUG
	std::cerr << "SendTo(" << destAccount.humanAccountID() << ") amount=" << iAmount <<
		", tag=" << iTag << std::endl;
#endif

	LocalTransaction::pointer lt(new LocalTransaction(destAccount, iAmount, iTag));
	if(!lt->makeTransaction())
		return JSONRPCError(500, "Insufficient funds in unlocked accounts");
	lt->performTransaction();
	return lt->getTransaction()->getJson(true);
}

Json::Value RPCServer::doTx(Json::Value& params)
{
	// tx
	// tx <txID>
	// tx <family> <seq>
	// tx <account>

	std::string param1, param2;
	if(!extractString(param1, params, 0))
	{ // all local transactions
		Json::Value ret(Json::objectValue);
		theApp->getWallet().addLocalTransactions(ret);
		return ret;
	}

	if(Transaction::isHexTxID(param1))
	{ // transaction by ID
		Json::Value ret;
		uint256 txid(param1);
		if(theApp->getWallet().getTxJson(txid, ret))
			return ret;

		Transaction::pointer txn=theApp->getMasterTransaction().fetch(txid, true);
		if(!txn) return JSONRPCError(500, "Transaction not found");
		return txn->getJson(true);
	}

	if(extractString(param2, params, 1))
	{ // family seq
		LocalAccount::pointer account=theApp->getWallet().parseAccount(param1+":"+param2);
		if(!account)
			return JSONRPCError(500, "Account not found");
		Json::Value ret;
		if(!theApp->getWallet().getTxsJson(account->getAddress(), ret))
			return JSONRPCError(500, "Unable to get wallet transactions");
		return ret;
	}
	else
	{
		// account
	}

	return "not implemented";
}

Json::Value RPCServer::doLedger(Json::Value& params)
{
	// ledger
	// ledger <seq>
	// ledger <account>

	int paramCount=getParamCount(params);

	if(paramCount==0);
	{
		Json::Value ret(Json::objectValue);
		theApp->getMasterLedger().getCurrentLedger()->addJson(ret);
		theApp->getMasterLedger().getClosingLedger()->addJson(ret);
		return ret;
	}

	return "not implemented";
}

// unl_add <node_public>
// unl_add <node_public> <comment>
Json::Value RPCServer::doUnlAdd(Json::Value& params) {
	if(params.size()==1 || params.size()==2)
	{
		std::string	strNodePublic=params[0u].asString();
		std::string strComment=params.size() == 2
			? ""
			: params[1u].asString();

		NewcoinAddress	nodePublic;

		if(nodePublic.setNodePublic(strNodePublic))
		{
			theApp->getUNL().addNode(nodePublic, strComment);

			return "adding node";
		}
		else
		{
			return "invalid public key";
		}
	}
	else return "invalid params";
}

Json::Value RPCServer::doUnlDefault(Json::Value& params) {
	return "not implemented";
}

// unl_delete <hanko>
Json::Value RPCServer::doUnlDelete(Json::Value& params) {
	if(params.size()==1)
	{
		std::string	strHanko=params[0u].asString();

		NewcoinAddress	hanko;

		if(hanko.setHanko(strHanko))
		{
			theApp->getUNL().removeNode(hanko);

			return "removing node";
		}
		else
		{
			return "invalid hanko";
		}
	}
	else return "invalid params";
}

Json::Value RPCServer::doUnlFetch(Json::Value& params) {
	return "not implemented";
}

Json::Value RPCServer::doUnlList(Json::Value& params) {
	return theApp->getUNL().getUnlJson();
}

Json::Value RPCServer::doUnlReset(Json::Value& params) {
	return "not implemented";
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	std::cerr << "RPC:" << command << std::endl;

	if(command== "stop")
	{
		mSocket.get_io_service().stop();
		return "newcoin server stopping";
	}

	if(command=="unl_add") return doUnlAdd(params);
	if(command=="unl_default") return doUnlDefault(params);
	if(command=="unl_delete") return doUnlDelete(params);
	if(command=="unl_fetch") return doUnlFetch(params);
	if(command=="unl_list") return doUnlList(params);
	if(command=="unl_reset") return doUnlReset(params);

	if(command=="createfamily") return doCreateFamily(params);
	if(command=="familyinfo") return doFamilyInfo(params);
	if(command=="accountinfo") return doAccountInfo(params);
	if(command=="newaccount") return doNewAccount(params);
	if(command=="lock") return doLock(params);
	if(command=="unlock") return doUnlock(params);
	if(command=="sendto") return doSendTo(params);
	if(command=="connect") return doConnect(params);
	if(command=="peers") return doPeers(params);
	if(command=="tx") return doTx(params);
	if(command=="ledger") return doLedger(params);

	return "unknown command";
}

void RPCServer::sendReply()
{
	boost::asio::async_write(mSocket, boost::asio::buffer(mReplyStr),
			boost::bind(&RPCServer::handle_write, shared_from_this(),
			boost::asio::placeholders::error));
}

void RPCServer::handle_write(const boost::system::error_code& /*error*/)
{
}

NewcoinAddress RPCServer::parseAccount(const std::string& account)
{ // FIXME: Support local wallet key names
	if(account.find(':')!=std::string::npos)
	{ // local account in family:seq form
		LocalAccount::pointer lac(theApp->getWallet().parseAccount(account));

		return lac
			? lac->getAddress()
			: NewcoinAddress();
	}

	NewcoinAddress *nap	= new NewcoinAddress();

	nap->setAccountID(account) || nap->setAccountPublic(account);

	return *nap;
}
// vim:ts=4
