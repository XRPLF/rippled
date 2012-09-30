
#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "HttpsClient.h"
#include "Application.h"
#include "RPC.h"
#include "Wallet.h"
#include "NewcoinAddress.h"
#include "AccountState.h"
#include "NicknameState.h"
#include "utils.h"
#include "Log.h"
#include "RippleLines.h"

#include "Pathfinder.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <openssl/md5.h>

#include "../json/reader.h"
#include "../json/writer.h"

RPCServer::RPCServer(boost::asio::io_service& io_service , NetworkOPs* nopNetwork)
	: mNetOps(nopNetwork), mSocket(io_service)
{
	mRole=GUEST;
}

Json::Value RPCServer::RPCError(int iError)
{
	static struct {
		int			iError;
		const char*	pToken;
		const char*	pMessage;
	} errorInfoA[] = {
		{ rpcACT_EXISTS,			"actExists",		"Account already exists."								},
		{ rpcACT_MALFORMED,			"actMalformed",		"Account malformed."									},
		{ rpcACT_NOT_FOUND,			"actNotFound",		"Account not found."									},
		{ rpcBAD_SEED,				"badSeed",			"Disallowed seed."										},
		{ rpcDST_ACT_MALFORMED,		"dstActMalformed",	"Destination account is malformed."						},
		{ rpcDST_ACT_MISSING,		"dstActMissing",	"Destination account does not exists."					},
		{ rpcDST_AMT_MALFORMED,		"dstAmtMalformed",	"Destination amount/currency/issuer is malformed."		},
		{ rpcFAIL_GEN_DECRPYT,		"failGenDecrypt",	"Failed to decrypt generator."							},
		{ rpcGETS_ACT_MALFORMED,	"getsActMalformed",	"Gets account malformed."								},
		{ rpcGETS_AMT_MALFORMED,	"getsAmtMalformed",	"Gets amount malformed."								},
		{ rpcHOST_IP_MALFORMED,		"hostIpMalformed",	"Host IP is malformed."									},
		{ rpcINSUF_FUNDS,			"insufFunds",		"Insufficient funds."									},
		{ rpcINTERNAL,				"internal",			"Internal error."										},
		{ rpcINVALID_PARAMS,		"invalidParams",	"Invalid parameters."									},
		{ rpcLGR_IDXS_INVALID,		"lgrIdxsInvalid",	"Ledger indexes invalid."								},
		{ rpcLGR_IDX_MALFORMED,		"lgrIdxMalformed",	"Ledger index malformed."								},
		{ rpcLGR_NOT_FOUND,			"lgrNotFound",		"Ledger not found."										},
		{ rpcNICKNAME_MALFORMED,	"nicknameMalformed","Nickname is malformed."								},
		{ rpcNICKNAME_MISSING,		"nicknameMissing",	"Nickname does not exist."								},
		{ rpcNICKNAME_PERM,			"nicknamePerm",		"Account does not control nickname."					},
		{ rpcNOT_IMPL,				"notImpl",			"Not implemented."										},
		{ rpcNO_ACCOUNT,			"noAccount",		"No such account."										},
		{ rpcNO_CLOSED,				"noClosed",			"Closed ledger is unavailable."							},
		{ rpcNO_CURRENT,			"noCurrent",		"Current ledger is unavailable."						},
		{ rpcNO_GEN_DECRPYT,		"noGenDectypt",		"Password failed to decrypt master public generator."	},
		{ rpcNO_NETWORK,			"noNetwork",		"Network not available."								},
		{ rpcNO_PERMISSION,			"noPermission",		"You don't have permission for this command."			},
		{ rpcPASSWD_CHANGED,		"passwdChanged",	"Wrong key, password changed."							},
		{ rpcPAYS_ACT_MALFORMED,	"paysActMalformed",	"Pays account malformed."								},
		{ rpcPAYS_AMT_MALFORMED,	"paysAmtMalformed",	"Pays amount malformed."								},
		{ rpcPORT_MALFORMED,		"portMalformed",	"Port is malformed."									},
		{ rpcPUBLIC_MALFORMED,		"publicMalformed",	"Public key is malformed."								},
		{ rpcQUALITY_MALFORMED,		"qualityMalformed",	"Quality malformed."									},
		{ rpcSRC_ACT_MALFORMED,		"srcActMalformed",	"Source account is malformed."							},
		{ rpcSRC_ACT_MISSING,		"srcActMissing",	"Source account does not exist."						},
		{ rpcSRC_AMT_MALFORMED,		"srcAmtMalformed",	"Source amount/currency/issuer is malformed."			},
		{ rpcSRC_UNCLAIMED,			"srcUnclaimed",		"Source account is not claimed."						},
		{ rpcSUCCESS,				"success",			"Success."												},
		{ rpcTXN_NOT_FOUND,			"txnNotFound",		"Transaction not found."								},
		{ rpcUNKNOWN_COMMAND,		"unknownCmd",		"Unknown command."										},
		{ rpcWRONG_SEED,			"wrongSeed",		"The regular key does not point as the master key."		},
	};

	int		i;

	for (i=NUMBER(errorInfoA); i-- && errorInfoA[i].iError != iError;)
		;

	Json::Value	jsonResult = Json::Value(Json::objectValue);

	jsonResult["error"]			= i >= 0 ? errorInfoA[i].pToken : lexical_cast_i(iError);
	jsonResult["error_message"]	= i >= 0 ? errorInfoA[i].pMessage : lexical_cast_i(iError);
	jsonResult["error_code"]	= iError;
	if (i >= 0)
		std::cerr << "RPCError: "
			<< errorInfoA[i].pToken << ": " << errorInfoA[i].pMessage << std::endl;

	return jsonResult;
}

void RPCServer::connected()
{
	//std::cout << "RPC request" << std::endl;
	if (mSocket.remote_endpoint().address().to_string()=="127.0.0.1") mRole=ADMIN;
	else mRole=GUEST;

	mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
		boost::bind(&RPCServer::Shandle_read, shared_from_this(),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
}

void RPCServer::handle_read(const boost::system::error_code& e,
	std::size_t bytes_transferred)
{
	if (!e)
	{
		boost::tribool result;
		result = mRequestParser.parse(
			mIncomingRequest, mReadBuffer.data(), mReadBuffer.data() + bytes_transferred);

		if (result)
		{
			mReplyStr = handleRequest(mIncomingRequest.mBody);

			sendReply();
		}
		else if (!result)
		{ // bad request
			std::cout << "bad request" << std::endl;
		}
		else
		{  // not done keep reading
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::Shandle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}
	else if (e != boost::asio::error::operation_aborted)
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
	if (!reader.parse(requestStr, valRequest) || valRequest.isNull() || !valRequest.isObject())
		return(HTTPReply(400, "unable to parse request"));

	// Parse id now so errors from here on will have the id
	id = valRequest["id"];

	// Parse method
	Json::Value valMethod = valRequest["method"];
	if (valMethod.isNull())
		return(HTTPReply(400, "null method"));
	if (!valMethod.isString())
		return(HTTPReply(400, "method is not string"));
	std::string strMethod = valMethod.asString();

	// Parse params
	Json::Value valParams = valRequest["params"];
	if (valParams.isNull())
		valParams = Json::Value(Json::arrayValue);
	else if (!valParams.isArray())
		return(HTTPReply(400, "parms unparseable"));

	Json::StyledStreamWriter w;
	w.write(Log(lsTRACE).ref(), valParams);
	Json::Value result(doCommand(strMethod, valParams));
	w.write(Log(lsTRACE).ref(), result);

	std::string strReply = JSONRPCReply(result, Json::Value(), id);
	return( HTTPReply(200, strReply) );
}

int RPCServer::getParamCount(const Json::Value& params)
{ // If non-array, only counts strings
	if (params.isNull()) return 0;
	if (params.isArray()) return params.size();
	if (!params.isConvertibleTo(Json::stringValue))
		return 0;
	return 1;
}

#if 0
// now, expire, n
bool RPCServer::parseAcceptRate(const std::string& sAcceptRate)
{
	if (!sAcceptRate.compare("expire"))
		0;

	return true;
}
#endif

bool RPCServer::extractString(std::string& param, const Json::Value& params, int index)
{
	if (params.isNull()) return false;

	if (index!=0)
	{
		if (!params.isArray() || !params.isValidIndex(index))
			return false;
		Json::Value p(params.get(index, Json::nullValue));
		if (p.isNull() || !p.isConvertibleTo(Json::stringValue))
			return false;
		param = p.asString();
		return true;
	}

	if (params.isArray())
	{
		if ( (!params.isValidIndex(0)) || (!params[0u].isConvertibleTo(Json::stringValue)) )
			return false;
		param = params[0u].asString();
		return true;
	}

	if (!params.isConvertibleTo(Json::stringValue))
		return false;
	param = params.asString();
	return true;
}

// Look up the master public generator for a regular seed so we may index source accounts ids.
// --> naRegularSeed
// <-- naMasterGenerator
Json::Value RPCServer::getMasterGenerator(const uint256& uLedger, const NewcoinAddress& naRegularSeed, NewcoinAddress& naMasterGenerator)
{
	NewcoinAddress		na0Public;		// To find the generator's index.
	NewcoinAddress		na0Private;		// To decrypt the master generator's cipher.
	NewcoinAddress		naGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);

	na0Public.setAccountPublic(naGenerator, 0);
	na0Private.setAccountPrivate(naGenerator, naRegularSeed, 0);

	SLE::pointer		sleGen			= mNetOps->getGenerator(uLedger, na0Public.getAccountID());

	if (!sleGen)
	{
		// No account has been claimed or has had it password set for seed.
		return RPCError(rpcNO_ACCOUNT);
	}

	std::vector<unsigned char>	vucCipher			= sleGen->getIFieldVL(sfGenerator);
	std::vector<unsigned char>	vucMasterGenerator	= na0Private.accountPrivateDecrypt(na0Public, vucCipher);
	if (vucMasterGenerator.empty())
	{
		return RPCError(rpcFAIL_GEN_DECRPYT);
	}

	naMasterGenerator.setGenerator(vucMasterGenerator);

	return Json::Value(Json::objectValue);
}

// Given a seed and a source account get the regular public and private key for authorizing transactions.
// - Make sure the source account can pay.
// --> naRegularSeed : To find the generator
// --> naSrcAccountID : Account we want the public and private regular keys to.
// <-- naAccountPublic : Regular public key for naSrcAccountID
// <-- naAccountPrivate : Regular private key for naSrcAccountID
// <-- saSrcBalance: Balance minus fee.
// --> naVerifyGenerator : If provided, the found master public generator must match.
// XXX Be more lenient, allow use of master generator on claimed accounts.
Json::Value RPCServer::authorize(const uint256& uLedger,
	const NewcoinAddress& naRegularSeed, const NewcoinAddress& naSrcAccountID,
	NewcoinAddress& naAccountPublic, NewcoinAddress& naAccountPrivate,
	STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
	const NewcoinAddress& naVerifyGenerator)
{
	// Source/paying account must exist.
	asSrc	= mNetOps->getAccountState(uLedger, naSrcAccountID);
	if (!asSrc)
	{
		return RPCError(rpcSRC_ACT_MISSING);
	}

	NewcoinAddress	naMasterGenerator;

	if (asSrc->bHaveAuthorizedKey())
	{
		Json::Value	obj	= getMasterGenerator(uLedger, naRegularSeed, naMasterGenerator);

		if (!obj.empty())
			return obj;
	}
	else
	{
		// Try the seed as a master seed.
		naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);
	}

	// If naVerifyGenerator is provided, make sure it is the master generator.
	if (naVerifyGenerator.isValid() && naMasterGenerator != naVerifyGenerator)
	{
		return RPCError(rpcWRONG_SEED);
	}

	// Find the index of the account from the master generator, so we can generate the public and private keys.
	NewcoinAddress		naMasterAccountPublic;
	unsigned int		iIndex	= 0;
	bool				bFound	= false;

	// Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
	// related.
	while (!bFound && iIndex != theConfig.ACCOUNT_PROBE_MAX)
	{
		naMasterAccountPublic.setAccountPublic(naMasterGenerator, iIndex);

		Log(lsINFO) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID() << " : " << naSrcAccountID.humanAccountID();

		bFound	= naSrcAccountID.getAccountID() == naMasterAccountPublic.getAccountID();
		if (!bFound)
			++iIndex;
	}

	if (!bFound)
	{
		return RPCError(rpcACT_NOT_FOUND);
	}

	// Use the regular generator to determine the associated public and private keys.
	NewcoinAddress		naGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);

	naAccountPublic.setAccountPublic(naGenerator, iIndex);
	naAccountPrivate.setAccountPrivate(naGenerator, naRegularSeed, iIndex);

	if (asSrc->bHaveAuthorizedKey() && (asSrc->getAuthorizedKey().getAccountID() != naAccountPublic.getAccountID()))
	{
		// std::cerr << "iIndex: " << iIndex << std::endl;
		// std::cerr << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID()) << std::endl;
		// std::cerr << "naAccountPublic: " << strHex(naAccountPublic.getAccountID()) << std::endl;

		return RPCError(rpcPASSWD_CHANGED);
	}

	saSrcBalance	= asSrc->getBalance();

	if (saSrcBalance < saFee)
	{
		Log(lsINFO) << "authorize: Insufficent funds for fees: fee=" << saFee.getText() << " balance=" << saSrcBalance.getText();

		return RPCError(rpcINSUF_FUNDS);
	}
	else
	{
		saSrcBalance -= saFee;
	}

	Json::Value	obj;
	return obj;
}

// --> strIdent: public key, account ID, or regular seed.
// <-- bIndex: true if iIndex > 0 and used the index.
Json::Value RPCServer::accountFromString(const uint256& uLedger, NewcoinAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex)
{
	NewcoinAddress	naSeed;

	if (naAccount.setAccountPublic(strIdent) || naAccount.setAccountID(strIdent))
	{
		// Got the account.
		bIndex	= false;
	}
	// Must be a seed.
	else if (!naSeed.setSeedGeneric(strIdent))
	{
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		// We allow the use of the seeds to access #0.
		// This is poor practice and merely for debuging convenience.
		NewcoinAddress		naRegular0Public;
		NewcoinAddress		naRegular0Private;

		NewcoinAddress		naGenerator		= NewcoinAddress::createGeneratorPublic(naSeed);

		naRegular0Public.setAccountPublic(naGenerator, 0);
		naRegular0Private.setAccountPrivate(naGenerator, naSeed, 0);

//		uint160				uGeneratorID	= naRegular0Public.getAccountID();
		SLE::pointer		sleGen			= mNetOps->getGenerator(uLedger, naRegular0Public.getAccountID());
		if (!sleGen)
		{
			// Didn't find a generator map, assume it is a master generator.
			nothing();
		}
		else
		{
			// Found master public key.
			std::vector<unsigned char>	vucCipher				= sleGen->getIFieldVL(sfGenerator);
			std::vector<unsigned char>	vucMasterGenerator		= naRegular0Private.accountPrivateDecrypt(naRegular0Public, vucCipher);
			if (vucMasterGenerator.empty())
			{
				RPCError(rpcNO_GEN_DECRPYT);
			}

			naGenerator.setGenerator(vucMasterGenerator);
		}

		bIndex	= !iIndex;

		naAccount.setAccountPublic(naGenerator, iIndex);
	}

	return Json::Value(Json::objectValue);
}

// account_domain_set <seed> <paying_account> [<domain>]
Json::Value RPCServer::doAccountDomainSet(const Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naVerifyGenerator);

	if (!obj.empty())
		return obj;

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		false,
		0,
		NewcoinAddress(),
		true,
		strCopy(params[2u].asString()),
		false,
		0,
		false,
		uint256(),
		0);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();

	return Json::Value(Json::objectValue);
}

// account_email_set <seed> <paying_account> [<email_address>]
Json::Value RPCServer::doAccountEmailSet(const Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naVerifyGenerator);

	if (!obj.empty())
		return obj;

	// Hash as per: http://en.gravatar.com/site/implement/hash/
	std::string					strEmail	= 3 == params.size() ? params[2u].asString() : "";
		boost::trim(strEmail);
		boost::to_lower(strEmail);

	std::vector<unsigned char>	vucMD5(128/8, 0);
		MD5(reinterpret_cast<const unsigned char*>(strEmail.c_str()), strEmail.size(), &vucMD5.front());

	uint128						uEmailHash(vucMD5);
	std::vector<unsigned char>	vucDomain;

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		true,
		strEmail.empty() ? uint128() : uEmailHash,
		false,
		uint256(),
		NewcoinAddress(),
		false,
		vucDomain,
		false,
		0,
		false,
		uint256(),
		0);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	if (!strEmail.empty())
	{
		obj["Email"]		= strEmail;
		obj["EmailHash"]	= strHex(vucMD5);
		obj["UrlGravatar"]	= AccountState::createGravatarUrl(uEmailHash);
	}

	return obj;
}

// account_info <account>|<nickname>|<account_public_key>
// account_info <seed>|<pass_phrase>|<key> [<index>]
Json::Value RPCServer::doAccountInfo(const Json::Value &params)
{
	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;
	NewcoinAddress	naAccount;

	Json::Value		ret;

	// Get info on account.

	uint256			uAccepted		= mNetOps->getClosedLedger();
	Json::Value		jAccepted		= accountFromString(uAccepted, naAccount, bIndex, strIdent, iIndex);

	if (jAccepted.empty())
	{
		AccountState::pointer asAccepted	= mNetOps->getAccountState(uAccepted, naAccount);

		if (asAccepted)
			asAccepted->addJson(jAccepted);
	}

	ret["accepted"]	= jAccepted;

	Json::Value		jCurrent	= accountFromString(uint256(0), naAccount, bIndex, strIdent, iIndex);

	if (jCurrent.empty())
	{
		AccountState::pointer asCurrent	= mNetOps->getAccountState(uint256(0), naAccount);

		if (asCurrent)
			asCurrent->addJson(jCurrent);
	}

	ret["current"]	= jCurrent;

#if 0
	if (!jAccepted && !asCurrent)
	{
		ret["account"]	= naAccount.humanAccountID();
		ret["status"]	= "NotFound";
		if (bIndex)
			ret["index"]	= iIndex;
	}
#endif
	return ret;
}

// account_message_set <seed> <paying_account> <pub_key>
Json::Value RPCServer::doAccountMessageSet(const Json::Value& params) {
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;
	NewcoinAddress	naMessagePubKey;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naMessagePubKey.setAccountPublic(params[2u].asString()))
	{
		return RPCError(rpcPUBLIC_MALFORMED);
	}

	NewcoinAddress				naVerifyGenerator;
	NewcoinAddress				naAccountPublic;
	NewcoinAddress				naAccountPrivate;
	AccountState::pointer		asSrc;
	STAmount					saSrcBalance;
	Json::Value					obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naVerifyGenerator);
	std::vector<unsigned char>	vucDomain;

	if (!obj.empty())
		return obj;

	Transaction::pointer		trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		false,
		uint256(),
		naMessagePubKey,
		false,
		vucDomain,
		false,
		0,
		false,
		uint256(),
		0);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();
	obj["MessageKey"]		= naMessagePubKey.humanAccountPublic();

	return obj;
}

// account_publish_set <seed> <paying_account> <hash> <size>
Json::Value RPCServer::doAccountPublishSet(const Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naVerifyGenerator);

	if (!obj.empty())
		return obj;

	uint256						uPublishHash(params[2u].asString());
	uint32						uPublishSize	= lexical_cast_s<int>(params[3u].asString());
	std::vector<unsigned char>	vucDomain;

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		false,
		0,
		NewcoinAddress(),
		false,
		vucDomain,
		false,
		0,
		true,
		uPublishHash,
		uPublishSize);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();

	return Json::Value(Json::objectValue);
}

// account_rate_set <seed> <paying_account> <rate>
Json::Value RPCServer::doAccountRateSet(const Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naVerifyGenerator);

	if (!obj.empty())
		return obj;

	uint32						uRate	= lexical_cast_s<int>(params[2u].asString());
	std::vector<unsigned char>	vucDomain;

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		false,
		0,
		NewcoinAddress(),
		false,
		vucDomain,
		true,
		uRate,
		false,
		uint256(),
		0);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();

	return Json::Value(Json::objectValue);
}

// account_wallet_set <seed> <paying_account> [<wallet_hash>]
Json::Value RPCServer::doAccountWalletSet(const Json::Value& params) {
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress				naMasterGenerator;
	NewcoinAddress				naAccountPublic;
	NewcoinAddress				naAccountPrivate;
	AccountState::pointer		asSrc;
	STAmount					saSrcBalance;
	Json::Value					obj					= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);
	std::vector<unsigned char>	vucDomain;

	if (!obj.empty())
		return obj;

	std::string					strWalletLocator	= params.size() == 3 ? params[2u].asString() : "";
	uint256						uWalletLocator;

	if (!strWalletLocator.empty())
		uWalletLocator.SetHex(strWalletLocator);

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		true,
		uWalletLocator,
		NewcoinAddress(),
		false,
		vucDomain,
		false,
		0,
		false,
		uint256(),
		0);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();

	if (!strWalletLocator.empty())
		obj["WalletLocator"]	= uWalletLocator.GetHex();

	return obj;
}

Json::Value RPCServer::doConnect(const Json::Value& params)
{
	if (theConfig.RUN_STANDALONE)
		return "cannot connect in standalone mode";

	// connect <ip> [port]
	std::string strIp;
	int			iPort	= -1;

	// XXX Might allow domain for manual connections.
	if (!extractString(strIp, params, 0))
		return RPCError(rpcHOST_IP_MALFORMED);

	if (params.size() == 2)
	{
		std::string strPort;

		// YYY Should make an extract int.
		if (!extractString(strPort, params, 1))
			return RPCError(rpcPORT_MALFORMED);

		iPort	= lexical_cast_s<int>(strPort);
	}

	// XXX Validate legal IP and port
	theApp->getConnectionPool().connectTo(strIp, iPort);

	return "connecting";
}

// data_delete <key>
Json::Value RPCServer::doDataDelete(const Json::Value& params)
{
	std::string	strKey = params[0u].asString();

	Json::Value	ret = Json::Value(Json::objectValue);

	if (theApp->getWallet().dataDelete(strKey))
	{
		ret["key"]		= strKey;
	}
	else
	{
		ret	= RPCError(rpcINTERNAL);
	}

	return ret;
}

// data_fetch <key>
Json::Value RPCServer::doDataFetch(const Json::Value& params)
{
	std::string	strKey = params[0u].asString();
	std::string	strValue;

	Json::Value	ret = Json::Value(Json::objectValue);

	ret["key"]		= strKey;
	if (theApp->getWallet().dataFetch(strKey, strValue))
		ret["value"]	= strValue;

	return ret;
}

// data_store <key> <value>
Json::Value RPCServer::doDataStore(const Json::Value& params)
{
	std::string	strKey		= params[0u].asString();
	std::string	strValue	= params[1u].asString();

	Json::Value	ret = Json::Value(Json::objectValue);

	if (theApp->getWallet().dataStore(strKey, strValue))
	{
		ret["key"]		= strKey;
		ret["value"]	= strValue;
	}
	else
	{
		ret	= RPCError(rpcINTERNAL);
	}

	return ret;
}

// nickname_info <nickname>
// Note: Nicknames are not automatically looked up by commands as they are advisory and can be changed.
Json::Value RPCServer::doNicknameInfo(const Json::Value& params)
{
	std::string	strNickname	= params[0u].asString();
		boost::trim(strNickname);

	if (strNickname.empty())
	{
		return RPCError(rpcNICKNAME_MALFORMED);
	}

	NicknameState::pointer	nsSrc	= mNetOps->getNicknameState(uint256(0), strNickname);
	if (!nsSrc)
	{
		return RPCError(rpcNICKNAME_MISSING);
	}

	Json::Value ret(Json::objectValue);

	ret["nickname"]	= strNickname;

	nsSrc->addJson(ret);

	return ret;
}

// nickname_set <seed> <paying_account> <nickname> [<offer_minimum>] [<authorization>]
Json::Value RPCServer::doNicknameSet(const Json::Value& params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	STAmount					saMinimumOffer;
	bool						bSetOffer		= params.size() >= 4;
	std::string					strOfferCurrency;
	std::string					strNickname		= params[2u].asString();
									boost::trim(strNickname);
	std::vector<unsigned char>	vucSignature;

	if (strNickname.empty())
	{
		return RPCError(rpcNICKNAME_MALFORMED);
	}
	else if (params.size() >= 4 && !saMinimumOffer.setFullValue(params[3u].asString(), strOfferCurrency))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}

	STAmount				saFee;
	NicknameState::pointer	nsSrc	= mNetOps->getNicknameState(uint256(0), strNickname);

	if (!nsSrc)
	{
		// Creating nickname.
		saFee	= theConfig.FEE_NICKNAME_CREATE;
	}
	else if (naSrcAccountID != nsSrc->getAccountID())
	{
		// We don't own the nickname.
		return RPCError(rpcNICKNAME_PERM);
	}
	else
	{
		// Setting the minimum offer.
		saFee	= theConfig.FEE_DEFAULT;
	}

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, saFee, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	// YYY Could verify nickname does not exist or points to paying account.
	// XXX Adjust fee for nickname create.

	Transaction::pointer	trans	= Transaction::sharedNicknameSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		saFee,
		0,											// YYY No source tag
		Ledger::getNicknameHash(strNickname),
		bSetOffer,
		saMinimumOffer,
		vucSignature);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// offer_create <seed> <paying_account> <takers_gets_amount> <takers_gets_currency> <takers_gets_issuer> <taker_pays_amount> <taker_pays_currency> <taker_pays_issuer> <expires> [passive]
// *offering* for *wants*
Json::Value RPCServer::doOfferCreate(const Json::Value &params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	STAmount		saTakerPays;
	STAmount		saTakerGets;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!saTakerGets.setFullValue(params[2u].asString(), params[3u].asString(), params[4u].asString()))
	{
		return RPCError(rpcGETS_AMT_MALFORMED);
	}
	else if (!saTakerPays.setFullValue(params[5u].asString(), params[6u].asString(), params[7u].asString()))
	{
		return RPCError(rpcPAYS_AMT_MALFORMED);
	}
	else if (params.size() == 10 && params[9u].asString() != "passive")
	{
		return RPCError(rpcINVALID_PARAMS);
	}

	uint32					uExpiration	= lexical_cast_s<int>(params[8u].asString());
	bool					bPassive	= params.size() == 10;

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	Transaction::pointer trans	= Transaction::sharedOfferCreate(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		bPassive,
		saTakerPays,
		saTakerGets,
		uExpiration);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// offer_cancel <seed> <paying_account> <sequence>
Json::Value RPCServer::doOfferCancel(const Json::Value &params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	uint32			uSequence	= lexical_cast_s<int>(params[2u].asString());

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	Transaction::pointer trans	= Transaction::sharedOfferCancel(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		uSequence);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// owner_info <account>|<nickname>|<account_public_key>
// owner_info <seed>|<pass_phrase>|<key> [<index>]
Json::Value RPCServer::doOwnerInfo(const Json::Value& params)
{
	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;
	NewcoinAddress	naAccount;

	Json::Value		ret;

	// Get info on account.

	uint256			uAccepted	= mNetOps->getClosedLedger();
	Json::Value		jAccepted	= accountFromString(uAccepted, naAccount, bIndex, strIdent, iIndex);

	ret["accepted"]	= jAccepted.empty() ? mNetOps->getOwnerInfo(uAccepted, naAccount) : jAccepted;

	Json::Value		jCurrent	= accountFromString(uint256(0), naAccount, bIndex, strIdent, iIndex);

	ret["current"]	= jCurrent.empty() ? mNetOps->getOwnerInfo(uint256(0), naAccount) : jCurrent;

	return ret;
}
// password_fund <seed> <paying_account> [<account>]
// YYY Make making account default to first account for seed.
Json::Value RPCServer::doPasswordFund(const Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[params.size() == 3 ? 2u : 1u].asString()))
	{
		return RPCError(rpcDST_ACT_MALFORMED);
	}

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	// YYY Could verify dst exists and isn't already funded.

	Transaction::pointer	trans	= Transaction::sharedPasswordFund(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		naDstAccountID);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// password_set <master_seed> <regular_seed> [<account>]
Json::Value RPCServer::doPasswordSet(const Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;
	NewcoinAddress	naAccountID;

	if (!naMasterSeed.setSeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setSeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	// YYY Might use account from string to be more flexible.
	else if (params.size() >= 3 && !naAccountID.setAccountID(params[2u].asString()))
	{
		return RPCError(rpcACT_MALFORMED);
	}
	else
	{
		NewcoinAddress	naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naMasterSeed);
		NewcoinAddress	naRegularGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		NewcoinAddress	naAccountPublic;
		NewcoinAddress	naAccountPrivate;

		naAccountPublic.setAccountPublic(naMasterGenerator, 0);
		naAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, 0);

		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

		// Hash of regular account #0 public key.
//		uint160						uGeneratorID		= naRegular0Public.getAccountID();
		std::vector<unsigned char>	vucGeneratorCipher	= naRegular0Private.accountPrivateEncrypt(naRegular0Public, naMasterGenerator.getGenerator());
		std::vector<unsigned char>	vucGeneratorSig;

		// Prove that we have the corresponding private key to the generator id.  So, we can get the generator id.
		// XXX Check result.
		naRegular0Private.accountPrivateSign(Serializer::getSHA512Half(vucGeneratorCipher), vucGeneratorSig);

		NewcoinAddress		naMasterXPublic;
		NewcoinAddress		naRegularXPublic;
		unsigned int		iIndex	= -1;	// Compensate for initial increment.
		int					iMax	= theConfig.ACCOUNT_PROBE_MAX;

		// YYY Could probe periodically to see if accounts exists.
		// YYY Max could be set randomly.
		// Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
		// related.
		do {
			++iIndex;
			naMasterXPublic.setAccountPublic(naMasterGenerator, iIndex);
			naRegularXPublic.setAccountPublic(naRegularGenerator, iIndex);

			std::cerr << iIndex << ": " << naRegularXPublic.humanAccountID() << std::endl;

		} while (naAccountID.getAccountID() != naMasterXPublic.getAccountID() && --iMax);

		if (!iMax)
		{
			return RPCError(rpcACT_NOT_FOUND);
		}

		Transaction::pointer	trans	= Transaction::sharedPasswordSet(
			naAccountPublic, naAccountPrivate,
			0,
			naRegularXPublic,
			vucGeneratorCipher,
			naRegular0Public.getAccountPublic(),
			vucGeneratorSig);

		trans	= mNetOps->submitTransaction(trans);

		Json::Value obj(Json::objectValue);

		// We "echo" the seeds so they can be checked.
		obj["master_seed"]		= naMasterSeed.humanSeed();
		obj["master_key"]		= naMasterSeed.humanSeed1751();
		obj["regular_seed"]		= naRegularSeed.humanSeed();
		obj["regular_key"]		= naRegularSeed.humanSeed1751();

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();

		return obj;
	}
}

Json::Value RPCServer::doPeers(const Json::Value& params)
{
	// peers
	Json::Value obj(Json::objectValue);
	obj["peers"]=theApp->getConnectionPool().getPeersJson();
	return obj;
}

// ripple <regular_seed> <paying_account>
//	 <source_max> <source_currency> [<source_issuerID>]
//   <path>+
//   full|partial limit|average <dest_account> <dest_amount> <dest_currency> [<dest_issuerID>]
//
//   path:
//     path <path_element>+
//
//   path_element:
//      account <accountID> [<currency>] [<issuerID>]
//      offer <currency> [<issuerID>]
Json::Value RPCServer::doRipple(const Json::Value &params)
{
	NewcoinAddress	naSeed;
	STAmount		saSrcAmountMax;
	uint160			uSrcCurrencyID;
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSrcIssuerID;
	bool			bPartial;
	bool			bFull;
	bool			bLimit;
	bool			bAverage;
	NewcoinAddress	naDstAccountID;
	STAmount		saDstAmount;
	uint160			uDstCurrencyID;

	STPathSet		spsPaths;

	naSrcIssuerID.setAccountID(params[4u].asString());							// <source_issuerID>

	if (!naSeed.setSeedGeneric(params[0u].asString()))							// <regular_seed>
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))				// <paying_account>
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	// <source_max> <source_currency> [<source_issuerID>]
	else if (!saSrcAmountMax.setFullValue(params[2u].asString(), params[3u].asString(), params[naSrcIssuerID.isValid() ? 4u : 1u].asString()))
	{
		// Log(lsINFO) << "naSrcIssuerID.isValid(): " << naSrcIssuerID.isValid();
		// Log(lsINFO) << "source_max: " << params[2u].asString();
		// Log(lsINFO) << "source_currency: " << params[3u].asString();
		// Log(lsINFO) << "source_issuer: " << params[naSrcIssuerID.isValid() ? 4u : 2u].asString();

		return RPCError(rpcSRC_AMT_MALFORMED);
	}

	int		iArg	= 4 + naSrcIssuerID.isValid();

	// XXX bSrcRedeem & bSrcIssue not used.
	STPath		spPath;

	while (params.size() != iArg && params[iArg].asString() == "path")			// path
	{
		Log(lsINFO) << "Path>";
		++iArg;

		while (params.size() != iArg
			&& (params[iArg].asString() == "offer" || params[iArg].asString() == "account"))
		{
			if (params.size() >= iArg + 3 && params[iArg].asString() == "offer")	// offer
			{
				Log(lsINFO) << "Offer>";
				uint160			uCurrencyID;
				NewcoinAddress	naIssuerID;

				++iArg;

				if (!STAmount::currencyFromString(uCurrencyID, params[iArg++].asString()))	// <currency>
				{
					return RPCError(rpcINVALID_PARAMS);
				}
				else if (naIssuerID.setAccountID(params[iArg].asString()))				// [<issuerID>]
				{
					++iArg;
				}

				spPath.addElement(STPathElement(
					uint160(0),
					uCurrencyID,
					naIssuerID.isValid() ? naIssuerID.getAccountID() : uint160(0)));
			}
			else if (params.size() >= iArg + 2 && params[iArg].asString() == "account")	// account
			{
				Log(lsINFO) << "Account>";
				NewcoinAddress	naAccountID;
				uint160			uCurrencyID;
				NewcoinAddress	naIssuerID;

				++iArg;

				if (!naAccountID.setAccountID(params[iArg++].asString()))				// <accountID>
				{
					return RPCError(rpcINVALID_PARAMS);
				}

				if (params.size() != iArg && STAmount::currencyFromString(uCurrencyID, params[iArg].asString())) // [<currency>]
				{
					++iArg;
				}

				if (params.size() != iArg && naIssuerID.setAccountID(params[iArg].asString()))	// [<issuerID>]
				{
					++iArg;
				}

				spPath.addElement(STPathElement(
					naAccountID.getAccountID(),
					uCurrencyID,
					naIssuerID.isValid() ? naIssuerID.getAccountID() : uint160(0)));
			}
			else
			{
				return RPCError(rpcINVALID_PARAMS);
			}
		}

		if (spPath.isEmpty())
		{
			return RPCError(rpcINVALID_PARAMS);
		}
		else
		{
			spsPaths.addPath(spPath);
			spPath.clear();
		}
	}

	// full|partial
	bPartial	= params.size() != iArg ? params[iArg].asString() == "partial" : false;
	bFull		= params.size() != iArg ? params[iArg].asString() == "full" : false;

	if (!bPartial && !bFull)
	{
		return RPCError(rpcINVALID_PARAMS);
	}
	else
	{
		++iArg;
	}

	// limit|average
	bLimit		= params.size() != iArg ? params[iArg].asString() == "limit" : false;
	bAverage	= params.size() != iArg ? params[iArg].asString() == "average" : false;

	if (!bLimit && !bAverage)
	{
		return RPCError(rpcINVALID_PARAMS);
	}
	else
	{
		++iArg;
	}

	if (params.size() != iArg && !naDstAccountID.setAccountID(params[iArg++].asString()))		// <dest_account>
	{
		return RPCError(rpcDST_ACT_MALFORMED);
	}

	const unsigned int uDstIssuer	= params.size() == iArg + 3 ? iArg+2 : iArg-1;

	// <dest_amount> <dest_currency> <dest_issuerID>
	if (params.size() != iArg + 2 && params.size() != iArg + 3)
	{
		// Log(lsINFO) << "params.size(): " << params.size();

		return RPCError(rpcDST_AMT_MALFORMED);
	}
	else if (!saDstAmount.setFullValue(params[iArg].asString(), params[iArg+1].asString(), params[uDstIssuer].asString()))
	{
		// Log(lsINFO) << "  Amount: " << params[iArg].asString();
		// Log(lsINFO) << "Currency: " << params[iArg+1].asString();
		// Log(lsINFO) << "  Issuer: " << params[uDstIssuer].asString();

		return RPCError(rpcDST_AMT_MALFORMED);
	}

	AccountState::pointer	asDst	= mNetOps->getAccountState(uint256(0), naDstAccountID);
	STAmount				saFee	= theConfig.FEE_DEFAULT;

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj		= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, saFee, asSrc, naVerifyGenerator);

	if (!obj.empty())
		return obj;

	// YYY Could do some checking: source has funds or credit, dst exists and has sufficent credit limit.
	// YYY Currency from same source or loops not allowed.
	// YYY Limit paths length and count.
	if (!asDst)
	{
		Log(lsINFO) << "naDstAccountID: " << naDstAccountID.humanAccountID();

		return RPCError(rpcDST_ACT_MISSING);
	}

	Transaction::pointer	trans	= Transaction::sharedPayment(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		saFee,
		0,											// YYY No source tag
		naDstAccountID,
		saDstAmount,
		saSrcAmountMax,
		spsPaths,
		bPartial,
		bLimit);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();
	obj["seed"]				= naSeed.humanSeed();
	obj["fee"]				= saFee.getText();
	obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
	obj["dstAccountID"]		= naDstAccountID.humanAccountID();
	obj["srcAmountMax"]		= saSrcAmountMax.getText();
	obj["srcISO"]			= saSrcAmountMax.getHumanCurrency();
	obj["dstAmount"]		= saDstAmount.getText();
	obj["dstISO"]			= saDstAmount.getHumanCurrency();
	obj["paths"]			= spsPaths.getText();

	return obj;
}

// ripple_line_set <seed> <paying_account> <destination_account> <limit_amount> [<currency>] [<quality_in>] [<quality_out>]
Json::Value RPCServer::doRippleLineSet(const Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	STAmount		saLimitAmount;
	bool			bLimitAmount	= true;
	bool			bQualityIn		= params.size() >= 6;
	bool			bQualityOut		= params.size() >= 7;
	uint32			uQualityIn		= 0;
	uint32			uQualityOut		= 0;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return RPCError(rpcDST_ACT_MALFORMED);
	}
	else if (!saLimitAmount.setFullValue(params[3u].asString(), params.size() >= 5 ? params[4u].asString() : "", params[2u].asString()))
	{
		return RPCError(rpcSRC_AMT_MALFORMED);
	}
	else if (bQualityIn && !parseQuality(params[5u].asString(), uQualityIn))
	{
		return RPCError(rpcQUALITY_MALFORMED);
	}
	else if (bQualityOut && !parseQuality(params[6u].asString(), uQualityOut))
	{
		return RPCError(rpcQUALITY_MALFORMED);
	}
	else
	{
		NewcoinAddress			naMasterGenerator;
		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
		AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj			= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

		if (!obj.empty())
			return obj;

		Transaction::pointer	trans	= Transaction::sharedCreditSet(
			naAccountPublic, naAccountPrivate,
			naSrcAccountID,
			asSrc->getSeq(),
			theConfig.FEE_DEFAULT,
			0,											// YYY No source tag
			naDstAccountID,
			bLimitAmount, saLimitAmount,
			bQualityIn, uQualityIn,
			bQualityOut, uQualityOut);

		trans	= mNetOps->submitTransaction(trans);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();
		obj["seed"]				= naSeed.humanSeed();
		obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
		obj["dstAccountID"]		= naDstAccountID.humanAccountID();

		return obj;
	}
}

// ripple_lines_get <account>|<nickname>|<account_public_key> [<index>]
Json::Value RPCServer::doRippleLinesGet(const Json::Value &params)
{
//	uint256			uAccepted	= mNetOps->getClosedLedger();

	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;

	NewcoinAddress	naAccount;

	Json::Value ret;

	ret	= accountFromString(uint256(0), naAccount, bIndex, strIdent, iIndex);

	if (!ret.empty())
		return ret;

	// Get info on account.
	ret	= Json::Value(Json::objectValue);

	ret["account"]	= naAccount.humanAccountID();
	if (bIndex)
		ret["index"]	= iIndex;

	AccountState::pointer	as		= mNetOps->getAccountState(uint256(0), naAccount);
	if (as)
	{
		Json::Value	jsonLines(Json::arrayValue);

		ret["account"]	= naAccount.humanAccountID();

		// XXX This is wrong, we do access the current ledger and do need to worry about changes.
		// We access a committed ledger and need not worry about changes.

		RippleLines rippleLines(naAccount.getAccountID());
		BOOST_FOREACH(RippleState::pointer line, rippleLines.getLines())
		{
			STAmount		saBalance	= line->getBalance();
			STAmount		saLimit		= line->getLimit();
			STAmount		saLimitPeer	= line->getLimitPeer();

			Json::Value			jPeer	= Json::Value(Json::objectValue);

			//jPeer["node"]			= uNode.ToString();

			jPeer["account"]		= line->getAccountIDPeer().humanAccountID();
			// Amount reported is positive if current account holds other account's IOUs.
			// Amount reported is negative if other account holds current account's IOUs.
			jPeer["balance"]		= saBalance.getText();
			jPeer["currency"]		= saBalance.getHumanCurrency();
			jPeer["limit"]			= saLimit.getText();
			jPeer["limit_peer"]		= saLimitPeer.getText();
			jPeer["quality_in"]		= static_cast<Json::UInt>(line->getQualityIn());
			jPeer["quality_out"]	= static_cast<Json::UInt>(line->getQualityOut());

			jsonLines.append(jPeer);
		}
		ret["lines"]	= jsonLines;
	}
	else
	{
		ret	= RPCError(rpcACT_NOT_FOUND);
	}

	return ret;
}

// send regular_seed paying_account account_id amount [currency] [issuer] [send_max] [send_currency] [send_issuer]
Json::Value RPCServer::doSend(const Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	STAmount		saSrcAmountMax;
	STAmount		saDstAmount;
	std::string		sSrcCurrency;
	std::string		sDstCurrency;
	std::string		sSrcIssuer;
	std::string		sDstIssuer;

	if (params.size() >= 5)
		sDstCurrency	= params[4u].asString();

	if (params.size() >= 6)
		sDstIssuer		= params[5u].asString();

	if (params.size() >= 7)
		sSrcCurrency	= params[6u].asString();

	if (params.size() >= 8)
		sSrcIssuer		= params[7u].asString();

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return RPCError(rpcDST_ACT_MALFORMED);
	}
	else if (!saDstAmount.setFullValue(params[3u].asString(), sDstCurrency, sDstIssuer))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}
	else if (params.size() >= 7 && !saSrcAmountMax.setFullValue(params[5u].asString(), sSrcCurrency, sSrcIssuer))
	{
		return RPCError(rpcSRC_AMT_MALFORMED);
	}
	else
	{
		AccountState::pointer	asDst	= mNetOps->getAccountState(uint256(0), naDstAccountID);
		bool					bCreate	= !asDst;
		STAmount				saFee	= bCreate ? theConfig.FEE_ACCOUNT_CREATE : theConfig.FEE_DEFAULT;

		NewcoinAddress			naVerifyGenerator;
		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
		AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj		= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, saFee, asSrc, naVerifyGenerator);

		// Log(lsINFO) << boost::str(boost::format("doSend: sSrcIssuer=%s sDstIssuer=%s saSrcAmountMax=%s saDstAmount=%s")
		//	% sSrcIssuer
		//	% sDstIssuer
		//	% saSrcAmountMax.getFullText()
		//	% saDstAmount.getFullText());

		if (!obj.empty())
			return obj;

		if (params.size() < 7)
			saSrcAmountMax	= saDstAmount;

		// Do a few simple checks.
		if (!saSrcAmountMax.isNative())
		{
			Log(lsINFO) << "doSend: Ripple";

			nothing();
		}
		else if (!saSrcBalance.isPositive())
		{
			// No native currency to send.
			Log(lsINFO) << "doSend: No native currency to send: " << saSrcBalance.getText();

			return RPCError(rpcINSUF_FUNDS);
		}
		else if (saDstAmount.isNative() && saSrcAmountMax < saDstAmount)
		{
			// Not enough native currency.

			Log(lsINFO) << "doSend: Insufficient funds: src=" << saSrcAmountMax.getText() << " dst=" << saDstAmount.getText();

			return RPCError(rpcINSUF_FUNDS);
		}
		// XXX Don't allow send to self of same currency.

		Transaction::pointer	trans;

		if (asDst) {
			// Destination exists, ordinary send.

			STPathSet			spsPaths;
			/*
			uint160  srcCurrencyID;
			bool ret_b;
			ret_b = false;
			STAmount::currencyFromString(srcCurrencyID, sSrcCurrency);

			Pathfinder pf(naSrcAccountID, naDstAccountID, srcCurrencyID, saDstAmount);

			ret_b = pf.findPaths(5, 1, spsPaths);
			// TODO: Nope; the above can't be right
			*/
			trans	= Transaction::sharedPayment(
				naAccountPublic, naAccountPrivate,
				naSrcAccountID,
				asSrc->getSeq(),
				saFee,
				0,											// YYY No source tag
				naDstAccountID,
				saDstAmount,
				saSrcAmountMax,
				spsPaths);
		}
		else
		{
			// Create destination and send.

			trans	= Transaction::sharedCreate(
				naAccountPublic, naAccountPrivate,
				naSrcAccountID,
				asSrc->getSeq(),
				saFee,
				0,											// YYY No source tag
				naDstAccountID,
				saDstAmount);								// Initial funds in XNS.
		}

		trans	= mNetOps->submitTransaction(trans);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();
		obj["seed"]				= naSeed.humanSeed();
		obj["fee"]				= saFee.getText();
		obj["create"]			= bCreate;
		obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
		obj["dstAccountID"]		= naDstAccountID.humanAccountID();
		obj["srcAmountMax"]		= saSrcAmountMax.getText();
		obj["srcISO"]			= saSrcAmountMax.getHumanCurrency();
		obj["dstAmount"]		= saDstAmount.getText();
		obj["dstISO"]			= saDstAmount.getHumanCurrency();

		return obj;
	}
}

Json::Value RPCServer::doServerInfo(const Json::Value& params)
{
	Json::Value ret(Json::objectValue);

	ret["info"]	= theApp->getOPs().getServerInfo();

	return ret;
}

Json::Value RPCServer::doTx(const Json::Value& params)
{
	// tx <txID>
	// tx <account>

	std::string param1, param2;
	if (!extractString(param1, params, 0))
	{
		return RPCError(rpcINVALID_PARAMS);
	}

	if (Transaction::isHexTxID(param1))
	{ // transaction by ID
		Json::Value ret;
		uint256 txid(param1);

		Transaction::pointer txn = theApp->getMasterTransaction().fetch(txid, true);

		if (!txn) return RPCError(rpcTXN_NOT_FOUND);

		return txn->getJson(0);
	}

	return RPCError(rpcNOT_IMPL);
}

// ledger [id|current|lastclosed] [full]
Json::Value RPCServer::doLedger(const Json::Value& params)
{
	if (getParamCount(params) == 0)
	{
		Json::Value ret(Json::objectValue), current(Json::objectValue), closed(Json::objectValue);
		theApp->getMasterLedger().getCurrentLedger()->addJson(current, 0);
		theApp->getMasterLedger().getClosedLedger()->addJson(closed, 0);
		ret["open"] = current;
		ret["closed"] = closed;
		return ret;
	}

	std::string param;
	if (!extractString(param, params, 0))
	{
		return "bad params";
	}

	Ledger::pointer ledger;
	if (param == "current")
		ledger = theApp->getMasterLedger().getCurrentLedger();
	else if ((param == "lastclosed") || (param == "lastaccepted"))
		ledger = theApp->getMasterLedger().getClosedLedger();
	else if (param.size() > 12)
		ledger = theApp->getMasterLedger().getLedgerByHash(uint256(param));
	else
		ledger = theApp->getMasterLedger().getLedgerBySeq(lexical_cast_s<uint32>(param));

	if (!ledger)
		return RPCError(rpcLGR_NOT_FOUND);

	bool full = extractString(param, params, 1) && (param == "full");
	Json::Value ret(Json::objectValue);
	ledger->addJson(ret, full ? LEDGER_JSON_FULL : 0);
	return ret;
}

// account_tx <account> <minledger> <maxledger>
// account_tx <account> <ledger>
Json::Value RPCServer::doAccountTransactions(const Json::Value& params)
{
	std::string param;
	uint32 minLedger, maxLedger;

	if (!extractString(param, params, 0))
		return RPCError(rpcINVALID_PARAMS);

	NewcoinAddress account;
	if (!account.setAccountID(param))
		return RPCError(rpcACT_MALFORMED);

	if (!extractString(param, params, 1))
		return RPCError(rpcLGR_IDX_MALFORMED);

	minLedger = lexical_cast_s<uint32>(param);

	if ((params.size() == 3) && extractString(param, params, 2))
		maxLedger = lexical_cast_s<uint32>(param);
	else
		maxLedger = minLedger;

	if ((maxLedger < minLedger) || (maxLedger == 0))
	{
		std::cerr << "minL=" << minLedger << ", maxL=" << maxLedger << std::endl;

		return RPCError(rpcLGR_IDXS_INVALID);
	}

#ifndef DEBUG
	try
	{
#endif
		std::vector< std::pair<uint32, uint256> > txns = mNetOps->getAffectedAccounts(account, minLedger, maxLedger);
		Json::Value ret(Json::objectValue);
		ret["account"] = account.humanAccountID();
		Json::Value ledgers(Json::arrayValue);

//		uint32 currentLedger = 0;
		for (std::vector< std::pair<uint32, uint256> >::iterator it = txns.begin(), end = txns.end(); it != end; ++it)
		{
			Transaction::pointer txn = theApp->getMasterTransaction().fetch(it->second, true);
			if (!txn)
				ret["transactions"].append(it->second.GetHex());
			else
				ret["transactions"].append(txn->getJson(0));

		}
		return ret;
#ifndef DEBUG
	}
	catch (...)
	{
		return RPCError(rpcINTERNAL);
	}
#endif
}

// unl_add <domain>|<node_public> [<comment>]
Json::Value RPCServer::doUnlAdd(const Json::Value& params)
{
	std::string	strNode		= params[0u].asString();
	std::string strComment	= (params.size() == 2) ? params[1u].asString() : "";

	NewcoinAddress	naNodePublic;

	if (naNodePublic.setNodePublic(strNode))
	{
		theApp->getUNL().nodeAddPublic(naNodePublic, UniqueNodeList::vsManual, strComment);

		return "adding node by public key";
	}
	else
	{
		theApp->getUNL().nodeAddDomain(strNode, UniqueNodeList::vsManual, strComment);

		return "adding node by domain";
	}
}

// validation_create [<pass_phrase>|<seed>|<seed_key>]
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCServer::doValidationCreate(const Json::Value& params) {
	NewcoinAddress	naSeed;
	Json::Value		obj(Json::objectValue);

	if (params.empty())
	{
		std::cerr << "Creating random validation seed." << std::endl;

		naSeed.setSeedRandom();					// Get a random seed.
	}
	else if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}

	obj["validation_public_key"]	= NewcoinAddress::createNodePublic(naSeed).humanNodePublic();
	obj["validation_seed"]			= naSeed.humanSeed();
	obj["validation_key"]			= naSeed.humanSeed1751();

	return obj;
}

// validation_seed [<pass_phrase>|<seed>|<seed_key>]
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCServer::doValidationSeed(const Json::Value& params) {
	Json::Value obj(Json::objectValue);

	if (params.empty())
	{
		std::cerr << "Unset validation seed." << std::endl;

		theConfig.VALIDATION_SEED.clear();
	}
	else if (!theConfig.VALIDATION_SEED.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		obj["validation_public_key"]	= NewcoinAddress::createNodePublic(theConfig.VALIDATION_SEED).humanNodePublic();
		obj["validation_seed"]			= theConfig.VALIDATION_SEED.humanSeed();
		obj["validation_key"]			= theConfig.VALIDATION_SEED.humanSeed1751();
	}

	return obj;
}

Json::Value RPCServer::accounts(const uint256& uLedger, const NewcoinAddress& naMasterGenerator)
{
	Json::Value jsonAccounts(Json::arrayValue);

	// YYY Don't want to leak to thin server that these accounts are related.
	// YYY Would be best to alternate requests to servers and to cache results.
	unsigned int	uIndex	= 0;

	do {
		NewcoinAddress		naAccount;

		naAccount.setAccountPublic(naMasterGenerator, uIndex++);

		AccountState::pointer as	= mNetOps->getAccountState(uLedger, naAccount);
		if (as)
		{
			Json::Value	jsonAccount(Json::objectValue);

			as->addJson(jsonAccount);

			jsonAccounts.append(jsonAccount);
		}
		else
		{
			uIndex	= 0;
		}
	} while (uIndex);

	return jsonAccounts;
}

// wallet_accounts <seed>
Json::Value RPCServer::doWalletAccounts(const Json::Value& params)
{
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}

	// Try the seed as a master seed.
	NewcoinAddress	naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naSeed);

	Json::Value jsonAccounts	= accounts(uint256(0), naMasterGenerator);

	if (jsonAccounts.empty())
	{
		// No account via seed as master, try seed a regular.
		Json::Value	ret	= getMasterGenerator(uint256(0), naSeed, naMasterGenerator);

		if (!ret.empty())
			return ret;

		ret["accounts"]	= accounts(uint256(0), naMasterGenerator);

		return ret;
	}
	else
	{
		// Had accounts via seed as master, return them.
		Json::Value ret(Json::objectValue);

		ret["accounts"]	= jsonAccounts;

		return ret;
	}
}

// wallet_add <regular_seed> <paying_account> <master_seed> [<initial_funds>] [<account_annotation>]
Json::Value RPCServer::doWalletAdd(const Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;
	NewcoinAddress	naSrcAccountID;
	STAmount		saAmount;
	std::string		sDstCurrency;

	if (!naRegularSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naMasterSeed.setSeedGeneric(params[2u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (params.size() >= 4 && !saAmount.setFullValue(params[3u].asString(), sDstCurrency))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}
	else
	{
		NewcoinAddress			naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naMasterSeed);
		NewcoinAddress			naRegularGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);

		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
		AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj			= authorize(uint256(0), naRegularSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, theConfig.FEE_ACCOUNT_CREATE, asSrc, naMasterGenerator);

		if (!obj.empty())
			return obj;

		if (saSrcBalance < saAmount)
		{
			return RPCError(rpcINSUF_FUNDS);
		}
		else
		{
			NewcoinAddress				naNewAccountPublic;
			NewcoinAddress				naNewAccountPrivate;
			NewcoinAddress				naAuthKeyID;
			uint160						uAuthKeyID;
			AccountState::pointer		asNew;
			std::vector<unsigned char>	vucSignature;
			bool						bAgain	= true;
			int							iIndex	= -1;

			// Find an unmade account.
			do {
				++iIndex;
				naNewAccountPublic.setAccountPublic(naMasterGenerator, iIndex);

				asNew	= mNetOps->getAccountState(uint256(0), naNewAccountPublic);
				if (!asNew)
					bAgain	= false;
			} while (bAgain);

			// XXX Have a maximum number of accounts per wallet?

			// Determine corrisponding master private key.
			naNewAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, iIndex);

			// Determine new accounts authorized regular key.
			naAuthKeyID.setAccountPublic(naRegularGenerator, iIndex);

			uAuthKeyID	= naAuthKeyID.getAccountID();

			// Sign anything (naAuthKeyID) to prove we know new master private key.
			naNewAccountPrivate.accountPrivateSign(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature);

			Transaction::pointer	trans	= Transaction::sharedWalletAdd(
				naAccountPublic, naAccountPrivate,
				naSrcAccountID,
				asSrc->getSeq(),
				theConfig.FEE_ACCOUNT_CREATE,
				0,											// YYY No source tag
				saAmount,
				naAuthKeyID,
				naNewAccountPublic,
				vucSignature);

			trans	= mNetOps->submitTransaction(trans);

			obj["transaction"]		= trans->getSTransaction()->getJson(0);
			obj["status"]			= trans->getStatus();
			obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
			obj["newAccountID"]		= naNewAccountPublic.humanAccountID();
			obj["amount"]			= saAmount.getText();

			return obj;
		}
	}
}

// wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]
//
// To provide an example to client writers, we do everything we expect a client to do here.
Json::Value RPCServer::doWalletClaim(const Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;

	if (!naMasterSeed.setSeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setSeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		// Building:
		//	 peer_wallet_claim <account_id> <authorized_key> <encrypted_master_public_generator> <generator_pubkey> <generator_signature>
		//		<source_tag> [<annotation>]
		//
		//
		// Which has no confidential information.

		// XXX Need better parsing.
		uint32		uSourceTag		= (params.size() == 2) ? 0 : lexical_cast_s<uint32>(params[2u].asString());
		// XXX Annotation is ignored.
		std::string strAnnotation	= (params.size() == 3) ? "" : params[3u].asString();

		NewcoinAddress	naMasterGenerator	= NewcoinAddress::createGeneratorPublic(naMasterSeed);
		NewcoinAddress	naRegularGenerator	= NewcoinAddress::createGeneratorPublic(naRegularSeed);
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		NewcoinAddress	naAccountPublic;
		NewcoinAddress	naAccountPrivate;

		naAccountPublic.setAccountPublic(naMasterGenerator, 0);
		naAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, 0);

		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

		// Hash of regular account #0 public key.
		uint160						uGeneratorID		= naRegular0Public.getAccountID();
		std::vector<unsigned char>	vucGeneratorCipher	= naRegular0Private.accountPrivateEncrypt(naRegular0Public, naMasterGenerator.getGenerator());
		std::vector<unsigned char>	vucGeneratorSig;

		// Prove that we have the corresponding private key to the generator id.  So, we can get the generator id.
		// XXX Check result.
		naRegular0Private.accountPrivateSign(Serializer::getSHA512Half(vucGeneratorCipher), vucGeneratorSig);

		Transaction::pointer	trans	= Transaction::sharedClaim(
			naAccountPublic, naAccountPrivate,
			uSourceTag,
			vucGeneratorCipher,
			naRegular0Public.getAccountPublic(),
			vucGeneratorSig);

		trans	= mNetOps->submitTransaction(trans);

		Json::Value obj(Json::objectValue);

		// We "echo" the seeds so they can be checked.
		obj["master_seed"]		= naMasterSeed.humanSeed();
		obj["master_key"]		= naMasterSeed.humanSeed1751();
		obj["regular_seed"]		= naRegularSeed.humanSeed();
		obj["regular_key"]		= naRegularSeed.humanSeed1751();

		obj["account_id"]		= naAccountPublic.humanAccountID();
		obj["generator_id"]		= strHex(uGeneratorID);
		obj["generator"]		= strHex(vucGeneratorCipher);
		obj["annotation"]		= strAnnotation;

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();

		return obj;
	}
}

// wallet_create regular_seed paying_account account_id [initial_funds]
// We don't allow creating an account_id by default here because we want to make sure the person has a chance to write down the
// master seed of the account to be created.
// YYY Need annotation and source tag
Json::Value RPCServer::doWalletCreate(const Json::Value& params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	NewcoinAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return RPCError(rpcDST_ACT_MALFORMED);
	}
	else if (mNetOps->getAccountState(uint256(0), naDstAccountID))
	{
		return RPCError(rpcACT_EXISTS);
	}

	// Trying to build:
	//	 peer_wallet_create <paying_account> <paying_signature> <account_id> [<initial_funds>] [<annotation>]

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_ACCOUNT_CREATE, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	STAmount				saInitialFunds	= (params.size() < 4) ? 0 : lexical_cast_s<uint64>(params[3u].asString());

	if (saSrcBalance < saInitialFunds)
		return RPCError(rpcINSUF_FUNDS);

	Transaction::pointer	trans	= Transaction::sharedCreate(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_ACCOUNT_CREATE,
		0,											// YYY No source tag
		naDstAccountID,
		saInitialFunds);							// Initial funds in XNC.

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// wallet_propose [<passphrase>]
// <passphrase> is only for testing. Master seeds should only be generated randomly.
Json::Value RPCServer::doWalletPropose(const Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naAccount;

	if (params.empty())
	{
		naSeed.setSeedRandom();
	}
	else
	{
		naSeed	= NewcoinAddress::createSeedGeneric(params[0u].asString());
	}

	NewcoinAddress	naGenerator	= NewcoinAddress::createGeneratorPublic(naSeed);
	naAccount.setAccountPublic(naGenerator, 0);

	Json::Value obj(Json::objectValue);

	obj["master_seed"]		= naSeed.humanSeed();
	obj["master_key"]		= naSeed.humanSeed1751();
	obj["account_id"]		= naAccount.humanAccountID();

	return obj;
}

// wallet_seed [<seed>|<passphrase>|<passkey>]
Json::Value RPCServer::doWalletSeed(const Json::Value& params)
{
	NewcoinAddress	naSeed;

	if (params.size()
		&& !naSeed.setSeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		NewcoinAddress	naAccount;

		if (!params.size())
		{
			naSeed.setSeedRandom();
		}

		NewcoinAddress	naGenerator	= NewcoinAddress::createGeneratorPublic(naSeed);

		naAccount.setAccountPublic(naGenerator, 0);

		Json::Value obj(Json::objectValue);

		obj["seed"]		= naSeed.humanSeed();
		obj["key"]		= naSeed.humanSeed1751();

		return obj;
	}
}

// unl_delete <domain>|<public_key>
Json::Value RPCServer::doUnlDelete(const Json::Value& params)
{
	std::string	strNode		= params[0u].asString();

	NewcoinAddress	naNodePublic;

	if (naNodePublic.setNodePublic(strNode))
	{
		theApp->getUNL().nodeRemovePublic(naNodePublic);

		return "removing node by public key";
	}
	else
	{
		theApp->getUNL().nodeRemoveDomain(strNode);

		return "removing node by domain";
	}
}

Json::Value RPCServer::doUnlList(const Json::Value& params)
{
	Json::Value obj(Json::objectValue);

	obj["unl"]=theApp->getUNL().getUnlJson();

	return obj;
}

// Populate the UNL from a local validators.txt file.
Json::Value RPCServer::doUnlLoad(const Json::Value& params)
{
	if (theConfig.UNL_DEFAULT.empty() || !theApp->getUNL().nodeLoad(theConfig.UNL_DEFAULT))
	{
		return RPCError(rpcLOAD_FAILED);
	}

	return "loading";
}

// Populate the UNL from newcoin.org's validators.txt file.
Json::Value RPCServer::doUnlNetwork(const Json::Value& params)
{
	theApp->getUNL().nodeNetwork();

	return "fetching";
}

// unl_reset
Json::Value RPCServer::doUnlReset(const Json::Value& params)
{
	theApp->getUNL().nodeReset();

	return "removing nodes";
}

// unl_score
Json::Value RPCServer::doUnlScore(const Json::Value& params)
{
	theApp->getUNL().nodeScore();

	return "scoring requested";
}

Json::Value RPCServer::doStop(const Json::Value& params)
{
	theApp->stop();

	return SYSTEM_NAME " server stopping";
}

// TODO: for now this simply checks if this is the admin account
// TODO: need to prevent them hammering this over and over
// TODO: maybe a better way is only allow admin from local host
Json::Value RPCServer::doLogin(const Json::Value& params)
{
	std::string	username		= params[0u].asString();
	std::string	password		= params[1u].asString();

	if (username == theConfig.RPC_USER && password == theConfig.RPC_PASSWORD)
	{
		//mRole=ADMIN;
		return "logged in";
	}
	else
	{
		return "nope";
	}
}

Json::Value RPCServer::doLogRotate(const Json::Value& params) 
{
  return Log::rotateLog();
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	Log(lsTRACE) << "RPC:" << command;

	static struct {
		const char* pCommand;
		doFuncPtr	dfpFunc;
		int			iMinParams;
		int			iMaxParams;
		bool		mAdminRequired;
		unsigned int	iOptions;
	} commandsA[] = {
		{	"account_domain_set",	&RPCServer::doAccountDomainSet,		2,  3, false,	optCurrent	},
		{	"account_email_set",	&RPCServer::doAccountEmailSet,		2,  3, false,	optCurrent	},
		{	"account_info",			&RPCServer::doAccountInfo,			1,  2, false,	optCurrent	},
		{	"account_message_set",	&RPCServer::doAccountMessageSet,	3,  3, false,	optCurrent	},
		{	"account_publish_set",	&RPCServer::doAccountPublishSet,	4,  4, false,	optCurrent	},
		{	"account_rate_set",		&RPCServer::doAccountRateSet,		3,  3, false,	optCurrent	},
		{	"account_tx",			&RPCServer::doAccountTransactions,	2,  3, false,	optNetwork	},
		{	"account_wallet_set",	&RPCServer::doAccountWalletSet,		2,  3, false,	optCurrent	},
		{	"connect",				&RPCServer::doConnect,				1,  2, true					},
		{	"data_delete",			&RPCServer::doDataDelete,			1,  1, true					},
		{	"data_fetch",			&RPCServer::doDataFetch,			1,  1, true					},
		{	"data_store",			&RPCServer::doDataStore,			2,  2, true					},
		{	"ledger",				&RPCServer::doLedger,				0,  2, false,	optNetwork	},
		{       "logrotate",                    &RPCServer::doLogRotate,                        0,  0, true,    0      },
		{	"nickname_info",		&RPCServer::doNicknameInfo,			1,  1, false,	optCurrent	},
		{	"nickname_set",			&RPCServer::doNicknameSet,			2,  3, false,	optCurrent	},
		{	"offer_create",			&RPCServer::doOfferCreate,			9, 10, false,	optCurrent	},
		{	"offer_cancel",			&RPCServer::doOfferCancel,			3,  3, false,	optCurrent	},
		{	"owner_info",			&RPCServer::doOwnerInfo,			1,  2, false,	optCurrent	},
		{	"password_fund",		&RPCServer::doPasswordFund,			2,  3, false,	optCurrent	},
		{	"password_set",			&RPCServer::doPasswordSet,			2,  3, false,	optNetwork	},
		{	"peers",				&RPCServer::doPeers,				0,  0, true					},
		{	"ripple",				&RPCServer::doRipple,				9, -1, false,	optCurrent|optClosed },
		{	"ripple_lines_get",		&RPCServer::doRippleLinesGet,		1,  2, false,	optCurrent	},
		{	"ripple_line_set",		&RPCServer::doRippleLineSet,		4,  7, false,	optCurrent	},
		{	"send",					&RPCServer::doSend,					3,  9, false,	optCurrent	},
		{	"server_info",			&RPCServer::doServerInfo,			0,  0, true					},
		{	"stop",					&RPCServer::doStop,					0,  0, true					},
		{	"tx",					&RPCServer::doTx,					1,  1, true					},

		{	"unl_add",				&RPCServer::doUnlAdd,				1,  2, true					},
		{	"unl_delete",			&RPCServer::doUnlDelete,			1,  1, true					},
		{	"unl_list",				&RPCServer::doUnlList,				0,  0, true					},
		{	"unl_load",				&RPCServer::doUnlLoad,				0,  0, true					},
		{	"unl_network",			&RPCServer::doUnlNetwork,			0,  0, true					},
		{	"unl_reset",			&RPCServer::doUnlReset,				0,  0, true					},
		{	"unl_score",			&RPCServer::doUnlScore,				0,  0, true					},

		{	"validation_create",	&RPCServer::doValidationCreate,		0,  1, false				},
		{	"validation_seed",		&RPCServer::doValidationSeed,		0,  1, false				},

		{	"wallet_accounts",		&RPCServer::doWalletAccounts,		1,  1, false,	optCurrent	},
		{	"wallet_add",			&RPCServer::doWalletAdd,			3,  5, false,	optCurrent	},
		{	"wallet_claim",			&RPCServer::doWalletClaim,			2,  4, false,	optNetwork	},
		{	"wallet_create",		&RPCServer::doWalletCreate,			3,  4, false,	optCurrent	},
		{	"wallet_propose",		&RPCServer::doWalletPropose,		0,  1, false,				},
		{	"wallet_seed",			&RPCServer::doWalletSeed,			0,  1, false,				},

		{	"login",				&RPCServer::doLogin,				2,  2, true					},
	};

	int		i = NUMBER(commandsA);

	while (i-- && command != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return RPCError(rpcUNKNOWN_COMMAND);
	}
	else if (commandsA[i].mAdminRequired && mRole != ADMIN)
	{
		return RPCError(rpcNO_PERMISSION);
	}
	else if (params.size() < commandsA[i].iMinParams
		|| (commandsA[i].iMaxParams >= 0 && params.size() > commandsA[i].iMaxParams))
	{
		return RPCError(rpcINVALID_PARAMS);
	}
	else if ((commandsA[i].iOptions & optNetwork) && !mNetOps->available())
	{
		return RPCError(rpcNO_NETWORK);
	}
	// XXX Should verify we have a current ledger.
	else if ((commandsA[i].iOptions & optCurrent) && false)
	{
		return RPCError(rpcNO_CURRENT);
	}
	else if ((commandsA[i].iOptions & optClosed) && mNetOps->getClosedLedger().isZero())
	{
		return RPCError(rpcNO_CLOSED);
	}
	else
	{
		return (this->*(commandsA[i].dfpFunc))(params);
	}
}

void RPCServer::sendReply()
{
	//std::cout << "RPC reply: " << mReplyStr << std::endl;
	boost::asio::async_write(mSocket, boost::asio::buffer(mReplyStr),
			boost::bind(&RPCServer::Shandle_write, shared_from_this(),
			boost::asio::placeholders::error));
}

void RPCServer::handle_write(const boost::system::error_code& e)
{
	//std::cout << "async_write complete " << e << std::endl;

	if (!e)
	{
		bool keep_alive = (mIncomingRequest.http_version_major == 1) && (mIncomingRequest.http_version_minor >= 1);
		BOOST_FOREACH(HttpHeader& h, mIncomingRequest.headers)
		{
			if (boost::iequals(h.name, "connection"))
			{
				if (boost::iequals(h.value, "keep-alive"))
					keep_alive = true;
				if (boost::iequals(h.value, "close"))
					keep_alive = false;
			}
		}
		if (keep_alive)
		{
			mIncomingRequest.method.clear();
			mIncomingRequest.uri.clear();
			mIncomingRequest.mBody.clear();
			mIncomingRequest.headers.clear();
			mRequestParser.reset();
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::Shandle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
		else
		{
			boost::system::error_code ignored_ec;
			mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}
	}

	if (e != boost::asio::error::operation_aborted)
	{
		//connection_manager_.stop(shared_from_this());
	}
}

// vim:ts=4
