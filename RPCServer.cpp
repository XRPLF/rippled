
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "json/value.h"
#include "json/reader.h"
#include "json/writer.h"

#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "Application.h"
#include "RPC.h"
#include "Wallet.h"
#include "Conversion.h"

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

	Json::Value result=doCommand(strMethod, valParams);

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

Json::Value RPCServer::doCreateFamily(Json::Value& params)
{
// createfamily <hexPrivateKey>
// createfamily <hexPublicKey>
// createfamily "<pass phrase>"
// createfamily

	std::string query;
	uint160 family;
	Json::Value ret(Json::objectValue);
	
	if(!extractString(query, params, 0))
	{
		std::cerr << "empty" << std::endl;
		uint256 privKey;
		family=theApp->getWallet().addRandomFamily(privKey);
		ret["PrivateGenerator"]=Wallet::privKeyToText(privKey);
	}
	else if(Wallet::isHexPrivateKey(query))
	{
		std::cerr << "hprivk" << std::endl;
		uint256 pk;
		pk.SetHex(query);
		family=theApp->getWallet().addFamily(pk, false);
	}
	else if(Wallet::isHexPublicKey(query))
	{
		std::cerr << "hpubk" << std::endl;
		family=theApp->getWallet().addFamily(query);
	}
	else
	{
		std::cerr << "PassPhrase(" << query << ")" << std::endl;
		family=theApp->getWallet().addFamily(query, false);
	}
	if(!family)
		return JSONRPCError(500, "Invalid family specifier");
	
	ret["FamilyIdentifier"]=family.GetHex();
	ret["ShortName"]=theApp->getWallet().getShortName(family);
	ret["PublicGenerator"]=theApp->getWallet().getPubGenHex(family);
	return ret;
}

Json::Value RPCServer::doAccountInfo(Json::Value &params)
{ // accountinfo <family>:<number>
	std::string acct;
	if(!extractString(acct, params, 0))
		return JSONRPCError(500, "Invalid account identifier");

	LocalAccount::pointer account=theApp->getWallet().parseAccount(acct);
	if(!account)
		return JSONRPCError(500, "Account not found");
	
	Json::Value ret(Json::objectValue);
	ret["ShortName"]=account->getShortName();
	ret["FullName"]=account->getFullName();
	ret["AccountID"]=NewcoinAddress(account->getAddress()).GetString();
	ret["Issued"]=Json::Value(account->isIssued());
	return ret;
}
 
Json::Value RPCServer::doNewAccount(Json::Value &params)
{ // newaccount <family>
	return "Not yet";
}

Json::Value RPCServer::doLock(Json::Value &params)
{ // lock <family>
  // lock
	return "Not yet";
}

Json::Value RPCServer::doUnlock(Json::Value &params)
{ // unlock <hexPrivateKey>
  // unlock "<pass phrase>"
	return "Not yet";
}

Json::Value RPCServer::doFamilyInfo(Json::Value &params)
{
	// familyinfo <family>
	// familyinfo <family> <number>
	// familyinfo
	int paramCount=getParamCount(params);

	if(paramCount==0)
	{
		std::vector<uint160> familyIDs;
		theApp->getWallet().getFamilies(familyIDs);

		Json::Value ret(Json::arrayValue);
		BOOST_FOREACH(const uint160& fid, familyIDs)
		{
			Json::Value obj(Json::objectValue);
			std::string name, comment;
			if(theApp->getWallet().getFamilyInfo(fid, name, comment))
			{
				obj["FamilyIdentifier"]=fid.GetHex();
				obj["ShortName"]=name;
				if(!comment.empty())
					obj["Comment"]=comment;
				ret.append(obj);
			}
		}
		return ret;
	}

	if(paramCount>2) return JSONRPCError(500, "Invalid parameters");
	std::string fParam;
	extractString(fParam, params, 0);

	uint160 family;
	if(Wallet::isHexFamily(fParam))
		family.SetHex(fParam);
	else if(Wallet::isHexPublicKey(fParam))
		family=theApp->getWallet().findFamilyPK(fParam);
	else
		family=theApp->getWallet().findFamilySN(fParam);
	if(!family) return JSONRPCError(500, "No such family");
		
	std::string name, comment, pubGen;
	bool isLocked;
	if(!theApp->getWallet().getFullFamilyInfo(family, name, comment, pubGen, isLocked))
		return JSONRPCError(500, "Family not found");
	Json::Value obj(Json::objectValue);
	obj["FamilyIdentifier"]=family.GetHex();
	obj["ShortName"]=name;
	if(!comment.empty())
		obj["Comment"]=comment;
	obj["PublicGenerator"]=pubGen;
	obj["Locked"]=Json::Value(isLocked);

	if(paramCount==2)
	{
		std::string keyNum;
		extractString(keyNum, params, 1);
		int kn=boost::lexical_cast<int>(keyNum);
		uint160 k=theApp->getWallet().peekKey(family, kn);
		if(!!k)
		{
			Json::Value key(Json::objectValue);
			key["Number"]=kn;
			key["Address"]=NewcoinAddress(k).GetString();
			obj["Account"]=key;
		}
	}

	return obj;
}

Json::Value RPCServer::doSendTo(Json::Value& params)
{   // Implement simple sending without gathering
	// sendto <destination> <amount>
	// sendto <destination> <amount> <tag>
	if(!params.isArray() || (params.size()<2))
		return JSONRPCError(500, "Invalid parameters");
	
	return "Not yet";
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	std::cerr << "RPC:" << command << std::endl;
	if(command== "stop")
	{
		mSocket.get_io_service().stop();
		return "newcoin server stopping";
	}
	if(command== "addUNL")
	{
		if(params.size()==2)
		{
			uint160 hanko=humanTo160(params[0u].asString());
			std::vector<unsigned char> pubKey;
			humanToPK(params[1u].asString(),pubKey);
			theApp->getUNL().addNode(hanko,pubKey);
			return "adding node";
		}else return "invalid params";
	}
	if(command=="getUNL")
	{
		std::string str;
		theApp->getUNL().dumpUNL(str);
		return(str.c_str());
	}
	if(command=="createfamily") return doCreateFamily(params);
	if(command=="familyinfo") return doFamilyInfo(params);
	if(command=="accountinfo") return doAccountInfo(params);
	if(command=="newaccount") return doNewAccount(params);
	if(command=="lock") return doLock(params);
	if(command=="unlock") return doUnlock(params);
	if(command=="sendto") return doSendTo(params);

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

uint160 RPCServer::parseAccount(const std::string& account)
{ // FIXME: Support local wallet key names
	if(account.find(':')!=std::string::npos)
	{ // local account in family:seq form
		LocalAccount::pointer lac(theApp->getWallet().parseAccount(account));
		if(!lac) return uint160();
		return lac->getAddress();
	}
	
	NewcoinAddress nac(account);
	if(!nac.IsValid()) return uint160();
	return nac.GetHash160();
}
