
#include <fstream>
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <openssl/md5.h>

#include "../json/reader.h"
#include "../json/writer.h"

#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "HttpsClient.h"
#include "Application.h"
#include "RPC.h"
#include "Wallet.h"
#include "Conversion.h"
#include "NewcoinAddress.h"
#include "AccountState.h"
#include "NicknameState.h"
#include "utils.h"

#define VALIDATORS_FETCH_SECONDS	30
#define VALIDATORS_FILE_PATH		"/" VALIDATORS_FILE_NAME
#define VALIDATORS_FILE_BYTES_MAX	(50 << 10)

/*
Just read from wire until the entire request is in.
*/

RPCServer::RPCServer(boost::asio::io_service& io_service , NetworkOPs* nopNetwork)
	: mNetOps(nopNetwork), mSocket(io_service)
{
}

Json::Value RPCServer::RPCError(int iError)
{
	static struct {
		int			iError;
		const char*	pToken;
		const char*	pMessage;
	} errorInfoA[] = {
		{ rpcACT_EXISTS,		"actExists",		"Account already exists." },
		{ rpcACT_MALFORMED,		"actMalformed",		"Account malformed." },
		{ rpcACT_NOT_FOUND,		"actNotFound",		"Account not found." },
		{ rpcBAD_SEED,			"badSeed",			"Disallowed seed." },
		{ rpcDST_ACT_MALFORMED,	"dstActMalformed",	"Destination account is malformed."	},
		{ rpcDST_AMT_MALFORMED,	"dstAmtMalformed",	"Destination amount/currency is malformed."	},
		{ rpcFAIL_GEN_DECRPYT,	"failGenDecrypt",	"Failed to decrypt generator." },
		{ rpcHOST_IP_MALFORMED,	"hostIpMalformed",	"Host IP is malformed."	},
		{ rpcINSUF_FUNDS,		"insufFunds",		"Insufficient funds." },
		{ rpcINTERNAL,			"internal",			"Internal error."	},
		{ rpcINVALID_PARAMS,	"invalidParams",	"Invalid parameters." },
		{ rpcLGR_IDXS_INVALID,	"lgrIdxsInvalid",	"Ledger indexes invalid." },
		{ rpcLGR_IDX_MALFORMED,	"lgrIdxMalformed",	"Ledger index malformed." },
		{ rpcLGR_NOT_FOUND,		"lgrNotFound",		"Ledger not found." },
		{ rpcMUST_SEND_XNS,		"mustSendXns",		"Can only send XNS to accounts which are not created." },
		{ rpcNICKNAME_MALFORMED,"nicknameMalformed","Nickname is malformed."	},
		{ rpcNICKNAME_MISSING,	"nicknameMissing",	"Nickname does not exist."	},
		{ rpcNICKNAME_PERM,		"nicknamePerm",		"Account does not control nickname."	},
		{ rpcNOT_IMPL,			"notImpl",			"Not implemented." },
		{ rpcNO_CLOSED,			"noClosed",			"Closed ledger is unavailable." },
		{ rpcNO_CURRENT,		"noCurrent",		"Current ledger is unavailable." },
		{ rpcNO_GEN_DECRPYT,	"noGenDectypt",		"Password failed to decrypt master public generator."	},
		{ rpcNO_NETWORK,		"noNetwork",		"Network not available." },
		{ rpcPASSWD_CHANGED,	"passwdChanged",	"Wrong key, password changed." },
		{ rpcPORT_MALFORMED,	"portMalformed",	"Port is malformed."	},
		{ rpcPUBLIC_MALFORMED,	"publicMalformed",	"Public key is malformed."	},
		{ rpcSRC_ACT_MALFORMED,	"srcActMalformed",	"Source account is malformed."	},
		{ rpcSRC_AMT_MALFORMED,	"srcAmtMalformed",	"Source amount/currency is malformed."	},
		{ rpcSRC_MISSING,		"srcMissing",		"Source account does not exist." },
		{ rpcSRC_UNCLAIMED,		"srcUnclaimed",		"Source account is not claimed." },
		{ rpcSUCCESS,			"success",			"Success." },
		{ rpcTXN_NOT_FOUND,		"txnNotFound",		"Transaction not found." },
		{ rpcUNKNOWN_COMMAND,	"unknownCmd",		"Unknown command." },
		{ rpcWRONG_PASSWORD,	"wrongPassword",	"Wrong password." },
		{ rpcWRONG_SEED,		"wrongSeed",		"The regular key does not point as the master key." },
	};

	int		i;

	for (i=NUMBER(errorInfoA); i-- && errorInfoA[i].iError != iError;)
		;

	Json::Value	jsonResult = Json::Value(Json::objectValue);

	jsonResult["error"]			= iError;
	if (i >= 0) {
		std::cerr << "RPCError: "
			<< errorInfoA[i].pToken << ": " << errorInfoA[i].pMessage << std::endl;

		jsonResult["error_token"]	= errorInfoA[i].pToken;
		jsonResult["error_message"]	= errorInfoA[i].pMessage;
	}

	return jsonResult;
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
	if (!e)
	{
		boost::tribool result;
		result = mRequestParser.parse(
			mIncomingRequest, mReadBuffer.data(), mReadBuffer.data() + bytes_transferred);

		if (result)
		{
			mReplyStr=handleRequest(mIncomingRequest.mBody);

			sendReply();
		}
		else if (!result)
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
	else if (!valParams.isArray())
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
	NewcoinAddress		naGenerator;
	NewcoinAddress		na0Public;		// To find the generator's index.
	NewcoinAddress		na0Private;		// To decrypt the master generator's cipher.

	naGenerator.setFamilyGenerator(naRegularSeed);
	na0Public.setAccountPublic(naGenerator, 0);
	na0Private.setAccountPrivate(naGenerator, naRegularSeed, 0);

	SLE::pointer		sleGen			= mNetOps->getGenerator(uLedger, na0Public.getAccountID());

	if (!sleGen)
	{
		// No account has been claimed or has had it password set for seed.
		return RPCError(rpcWRONG_PASSWORD);
	}

	std::vector<unsigned char>	vucCipher			= sleGen->getIFieldVL(sfGenerator);
	std::vector<unsigned char>	vucMasterGenerator	= na0Private.accountPrivateDecrypt(na0Public, vucCipher);
	if (vucMasterGenerator.empty())
	{
		return RPCError(rpcFAIL_GEN_DECRPYT);
	}

	naMasterGenerator.setFamilyGenerator(vucMasterGenerator);

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
		return RPCError(rpcSRC_MISSING);
	}
	
	NewcoinAddress	naMasterGenerator;

	
	if(asSrc->bHaveAuthorizedKey())
	{
		Json::Value	obj	= getMasterGenerator(uLedger, naRegularSeed, naMasterGenerator);

		if (!obj.empty())
			return obj;
	}else
	{
		// Try the seed as a master seed.
		naMasterGenerator.setFamilyGenerator(naRegularSeed);
	}

	// If naVerifyGenerator is provided, make sure it is the master generator.
	if (naVerifyGenerator.isValid() && naMasterGenerator != naVerifyGenerator)
	{
		return RPCError(rpcWRONG_SEED);
	}

	// Find the index of the account from the master generator, so we can generate the public and private keys.
	NewcoinAddress		naMasterAccountPublic;
	unsigned int		iIndex = -1;	// Compensate for initial increment.

	// XXX Stop after Config.account_probe_max
	// Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
	// related.
	do {
		++iIndex;
		naMasterAccountPublic.setAccountPublic(naMasterGenerator, iIndex);
	} while (naSrcAccountID.getAccountID() != naMasterAccountPublic.getAccountID());

	// Use the regular generator to determine the associated public and private keys.
	NewcoinAddress		naGenerator;

	naGenerator.setFamilyGenerator(naRegularSeed);
	naAccountPublic.setAccountPublic(naGenerator, iIndex);
	naAccountPrivate.setAccountPrivate(naGenerator, naRegularSeed, iIndex);

	if (asSrc->bHaveAuthorizedKey() && (asSrc->getAuthorizedKey().getAccountID() != naAccountPublic.getAccountID()))
	{
		std::cerr << "iIndex: " << iIndex << std::endl;
		std::cerr << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID()) << std::endl;
		std::cerr << "naAccountPublic: " << strHex(naAccountPublic.getAccountID()) << std::endl;

		return RPCError(rpcPASSWD_CHANGED);
	}

	saSrcBalance	= asSrc->getBalance();

	if (saSrcBalance < saFee)
	{
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
	else if (!naSeed.setFamilySeedGeneric(strIdent))
	{
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		// We allow the use of the seeds to access #0.
		// This is poor practice and merely for debuging convenience.
		NewcoinAddress		naGenerator;
		NewcoinAddress		naRegular0Public;
		NewcoinAddress		naRegular0Private;

		naGenerator.setFamilyGenerator(naSeed);

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

			naGenerator.setFamilyGenerator(vucMasterGenerator);
		}

		bIndex	= !iIndex;

		naAccount.setAccountPublic(naGenerator, iIndex);
	}

	return Json::Value(Json::objectValue);
}

// account_email_set <seed> <paying_account> [<email_address>]
Json::Value RPCServer::doAccountEmailSet(Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger	= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		strEmail.empty(),
		uEmailHash,
		false,
		uint256(),
		NewcoinAddress());

	(void) mNetOps->processTransaction(trans);

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
Json::Value RPCServer::doAccountInfo(Json::Value &params)
{
	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size()? boost::lexical_cast<int>(params[1u].asString()) : 0;
	NewcoinAddress	naAccount;

	Json::Value		ret;

	// Get info on account.

	uint256			uClosed		= mNetOps->getClosedLedger();
	Json::Value		jClosed		= accountFromString(uClosed, naAccount, bIndex, strIdent, iIndex);

	if (jClosed.empty())
	{
		AccountState::pointer asClosed	= mNetOps->getAccountState(uClosed, naAccount);

		if (asClosed)
			asClosed->addJson(jClosed);
	}

	ret["closed"]	= jClosed;

	uint256			uCurrent	= mNetOps->getCurrentLedger();
	Json::Value		jCurrent	= accountFromString(uCurrent, naAccount, bIndex, strIdent, iIndex);

	if (jCurrent.empty())
	{
		AccountState::pointer asCurrent	= mNetOps->getAccountState(uCurrent, naAccount);

		if (asCurrent)
			asCurrent->addJson(jCurrent);
	}

	ret["current"]	= jCurrent;

#if 0
	if (!jClosed && !asCurrent)
	{
		ret["account"]	= naAccount.humanAccountID();
		ret["status"]	= "NotFound";
		if (bIndex)
			ret["index"]	= iIndex;
	}
#endif
	return ret;
}

// account_lines <account>|<nickname>|<account_public_key> [<index>]
Json::Value RPCServer::doAccountLines(Json::Value &params)
{
//	uint256			uClosed		= mNetOps->getClosedLedger();
	uint256			uCurrent	= mNetOps->getCurrentLedger();

	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size()? boost::lexical_cast<int>(params[1u].asString()) : 0;

	NewcoinAddress	naAccount;

	Json::Value ret;

	ret	= accountFromString(uCurrent, naAccount, bIndex, strIdent, iIndex);

	if (!ret.empty())
		return ret;

	// Get info on account.
	ret	= Json::Value(Json::objectValue);

	ret["account"]	= naAccount.humanAccountID();
	if (bIndex)
		ret["index"]	= iIndex;

	AccountState::pointer	as		= mNetOps->getAccountState(uCurrent, naAccount);
	if (as)
	{
		Json::Value	jsonLines = Json::Value(Json::objectValue);

		ret["account"]	= naAccount.humanAccountID();

		// We access a committed ledger and need not worry about changes.
		uint256	uDirLineNodeFirst;
		uint256	uDirLineNodeLast;

		if (mNetOps->getDirLineInfo(uCurrent, naAccount, uDirLineNodeFirst, uDirLineNodeLast))
		{
			for (; uDirLineNodeFirst <= uDirLineNodeLast; uDirLineNodeFirst++)
			{
				STVector256	svRippleNodes	= mNetOps->getDirNode(uCurrent, uDirLineNodeFirst);

				BOOST_FOREACH(uint256& uNode, svRippleNodes.peekValue())
				{
					NewcoinAddress	naAccountPeer;
					STAmount		saBalance;
					STAmount		saLimit;
					STAmount		saLimitPeer;

					RippleState::pointer	rsLine	= mNetOps->getRippleState(uCurrent, uNode);

					if (rsLine)
					{
						rsLine->setViewAccount(naAccount);

						naAccountPeer		= rsLine->getAccountIDPeer();
						saBalance			= rsLine->getBalance();
						saLimit				= rsLine->getLimit();
						saLimitPeer			= rsLine->getLimitPeer();

						Json::Value				jPeer	= Json::Value(Json::objectValue);

						jPeer["node"]		= uNode.ToString();

						jPeer["balance"]	= saBalance.getText();
						jPeer["currency"]	= saBalance.getCurrencyHuman();
						jPeer["limit"]		= saLimit.getJson(0);
						jPeer["limit_peer"]	= saLimitPeer.getJson(0);

						jsonLines[naAccountPeer.humanAccountID()]	= jPeer;
					}
					else
					{
						std::cerr << "doAccountLines: Bad index: " << uNode.ToString() << std::endl;
					}
				}
			}
		}
		ret["lines"]	= jsonLines;
	}
	else
	{
		ret["status"]	= "NotFound";
	}

	return ret;
}

// account_message_set <seed> <paying_account> <pub_key>
Json::Value RPCServer::doAccountMessageSet(Json::Value& params) {
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger	= mNetOps->getCurrentLedger();
	NewcoinAddress	naMessagePubKey;

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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

	NewcoinAddress			naVerifyGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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
		uint256(),
		naMessagePubKey);

	(void) mNetOps->processTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();
	obj["MessageKey"]		= naMessagePubKey.humanAccountPublic();

	return obj;
}

// account_wallet_set <seed> <paying_account> [<wallet_hash>]
Json::Value RPCServer::doAccountWalletSet(Json::Value& params) {
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger	= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	std::string				strWalletLocator	= params.size() == 3 ? params[2u].asString() : "";
	uint256					uWalletLocator;

	uWalletLocator.SetHex(strWalletLocator);

	Transaction::pointer	trans	= Transaction::sharedAccountSet(
		naAccountPublic, naAccountPrivate,
		naSrcAccountID,
		asSrc->getSeq(),
		theConfig.FEE_DEFAULT,
		0,											// YYY No source tag
		false,
		uint128(),
		strWalletLocator.empty(),
		uWalletLocator,
		NewcoinAddress());

	(void) mNetOps->processTransaction(trans);

	obj["transaction"]		= trans->getSTransaction()->getJson(0);
	obj["status"]			= trans->getStatus();

	if (!strWalletLocator.empty())
		obj["WalletLocator"]	= uWalletLocator.GetHex();

	return obj;
}

Json::Value RPCServer::doConnect(Json::Value& params)
{
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

		iPort	= boost::lexical_cast<int>(strPort);
	}

	if (!theApp->getConnectionPool().connectTo(strIp, iPort))
		return "connected";

	return "connecting";
}

// credit_set <seed> <paying_account> <destination_account> <limit_amount> [<currency>] [<accept_rate>]
Json::Value RPCServer::doCreditSet(Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	STAmount		saLimitAmount;
	uint256			uLedger		= mNetOps->getCurrentLedger();
	uint32			uAcceptRate	= params.size() >= 6 ? boost::lexical_cast<uint32>(params[5u].asString()) : 0;

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	else if (!saLimitAmount.setValue(params[3u].asString(), params.size() >= 5 ? params[4u].asString() : ""))
	{
		return RPCError(rpcSRC_AMT_MALFORMED);
	}
	else
	{
		NewcoinAddress			naMasterGenerator;
		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
	    AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj			= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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
			saLimitAmount,
			uAcceptRate);

		(void) mNetOps->processTransaction(trans);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();
		obj["seed"]				= naSeed.humanFamilySeed();
		obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
		obj["dstAccountID"]		= naDstAccountID.humanAccountID();
		obj["limitAmount"]		= saLimitAmount.getText();
		obj["acceptRate"]		= uAcceptRate;

		return obj;
	}
}

// data_delete <key>
Json::Value RPCServer::doDataDelete(Json::Value& params)
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
Json::Value RPCServer::doDataFetch(Json::Value& params)
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
Json::Value RPCServer::doDataStore(Json::Value& params)
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
Json::Value RPCServer::doNicknameInfo(Json::Value& params)
{
	uint256			uLedger	= mNetOps->getCurrentLedger();

	std::string	strNickname	= params[0u].asString();
		boost::trim(strNickname);

	if (strNickname.empty())
	{
		return RPCError(rpcNICKNAME_MALFORMED);
	}

	NicknameState::pointer	nsSrc	= mNetOps->getNicknameState(uLedger, strNickname);
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
Json::Value RPCServer::doNicknameSet(Json::Value& params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	else if (params.size() >= 4 && !saMinimumOffer.setValue(params[3u].asString(), strOfferCurrency))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}

	STAmount				saFee;
	NicknameState::pointer	nsSrc	= mNetOps->getNicknameState(uLedger, strNickname);

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
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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

	(void) mNetOps->processTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// password_fund <seed> <paying_account> [<account>]
// YYY Make making account default to first account for seed.
Json::Value RPCServer::doPasswordFund(Json::Value &params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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

	(void) mNetOps->processTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// password_set <master_seed> <regular_seed> [<account>]
Json::Value RPCServer::doPasswordSet(Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;
	NewcoinAddress	naAccountID;

	if (!naMasterSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setFamilySeedGeneric(params[1u].asString()))
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
		NewcoinAddress	naMasterGenerator;
		NewcoinAddress	naRegularGenerator;
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		NewcoinAddress	naAccountPublic;
		NewcoinAddress	naAccountPrivate;

		naMasterGenerator.setFamilyGenerator(naMasterSeed);
		naAccountPublic.setAccountPublic(naMasterGenerator, 0);
		naAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, 0);

		naRegularGenerator.setFamilyGenerator(naRegularSeed);

		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

		// Hash of regular account #0 public key.
//		uint160						uGeneratorID		= naRegular0Public.getAccountID();
		std::vector<unsigned char>	vucGeneratorCipher	= naRegular0Private.accountPrivateEncrypt(naRegular0Public, naMasterGenerator.getFamilyGenerator());
		std::vector<unsigned char>	vucGeneratorSig;

		// Prove that we have the corrisponding private key to the generator id.  So, we can get the generator id.
		// XXX Check result.
		naRegular0Private.accountPrivateSign(Serializer::getSHA512Half(vucGeneratorCipher), vucGeneratorSig);

		NewcoinAddress		naMasterXPublic;
		NewcoinAddress		naRegularXPublic;
		unsigned int		iIndex	= -1;	// Compensate for initial increment.
		int					iMax	= theConfig.ACCOUNT_PROBE_MAX;

		// YYY Could probe peridoically to see if accounts exists.
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

		Transaction::pointer	trns	= Transaction::sharedPasswordSet(
			naAccountPublic, naAccountPrivate,
			0,
			naRegularXPublic,
			vucGeneratorCipher,
			naRegular0Public.getAccountPublic(),
			vucGeneratorSig);

		(void) mNetOps->processTransaction(trns);

		Json::Value obj(Json::objectValue);

		// We "echo" the seeds so they can be checked.
		obj["master_seed"]		= naMasterSeed.humanFamilySeed();
		obj["master_key"]		= naMasterSeed.humanFamilySeed1751();
		obj["regular_seed"]		= naRegularSeed.humanFamilySeed();
		obj["regular_key"]		= naRegularSeed.humanFamilySeed1751();

		obj["transaction"]		= trns->getSTransaction()->getJson(0);
		obj["status"]			= trns->getStatus();

		return obj;
	}
}

Json::Value RPCServer::doPeers(Json::Value& params)
{
	// peers
	return theApp->getConnectionPool().getPeersJson();
}

// send regular_seed paying_account account_id amount [currency] [send_max] [send_currency]
Json::Value RPCServer::doSend(Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	STAmount		saSrcAmount;
	STAmount		saDstAmount;
	std::string		sSrcCurrency;
	std::string		sDstCurrency;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (params.size() >= 5)
		sDstCurrency	= params[4u].asString();

	if (params.size() >= 7)
		sSrcCurrency	= params[6u].asString();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	else if (!saDstAmount.setValue(params[3u].asString(), sDstCurrency))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}
	else if (params.size() >= 6 && !saSrcAmount.setValue(params[5u].asString(), sSrcCurrency))
	{
		return RPCError(rpcSRC_AMT_MALFORMED);
	}
	else
	{
		AccountState::pointer	asDst	= mNetOps->getAccountState(uLedger, naDstAccountID);
		bool					bCreate	= !asDst;
		STAmount				saFee	= bCreate ? theConfig.FEE_ACCOUNT_CREATE : theConfig.FEE_DEFAULT;

		NewcoinAddress			naVerifyGenerator;
		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
	    AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj		= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, saFee, asSrc, naVerifyGenerator);

		if (!obj.empty())
			return obj;

		if (params.size() < 6)
			saSrcAmount	= saDstAmount;

		if (saSrcBalance < saDstAmount)
		{
			return RPCError(rpcINSUF_FUNDS);
		}

		Transaction::pointer	trans;

		if (asDst) {
			// Ordinary send.

			STPathSet				spPaths;

			trans	= Transaction::sharedPayment(
				naAccountPublic, naAccountPrivate,
				naSrcAccountID,
				asSrc->getSeq(),
				saFee,
				0,											// YYY No source tag
				naDstAccountID,
				saDstAmount,
				saSrcAmount,
				spPaths);
		}
		else if (!saDstAmount.isNative())
		{
			return RPCError(rpcMUST_SEND_XNS);
		}
		else
		{
			// Create and send.

			trans	= Transaction::sharedCreate(
				naAccountPublic, naAccountPrivate,
				naSrcAccountID,
				asSrc->getSeq(),
				saFee,
				0,											// YYY No source tag
				naDstAccountID,
				saDstAmount);								// Initial funds in XNC.
		}

		(void) mNetOps->processTransaction(trans);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();
		obj["seed"]				= naSeed.humanFamilySeed();
		obj["fee"]				= saFee.getText();
		obj["create"]			= bCreate;
		obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
		obj["dstAccountID"]		= naDstAccountID.humanAccountID();
		obj["srcAmount"]		= saSrcAmount.getText();
		obj["srcISO"]			= saSrcAmount.getCurrencyHuman();
		obj["dstAmount"]		= saDstAmount.getText();
		obj["dstISO"]			= saDstAmount.getCurrencyHuman();

		return obj;
	}
}

// transit_set <seed> <paying_account> <transit_rate> <starts> <expires>
Json::Value RPCServer::doTransitSet(Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naSrcAccountID;
	std::string		sTransitRate;
	std::string		sTransitStart;
	std::string		sTransitExpire;
	uint32			uTransitRate;
	uint32			uTransitStart;
	uint32			uTransitExpire;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (params.size() >= 6)
		sTransitRate		= params[6u].asString();

	if (params.size() >= 7)
		sTransitStart	= params[7u].asString();

	if (params.size() >= 8)
		sTransitExpire	= params[8u].asString();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else
	{
		NewcoinAddress			naMasterGenerator;
		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
	    AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj		= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, theConfig.FEE_DEFAULT, asSrc, naMasterGenerator);

		if (!obj.empty())
			return obj;

		uTransitRate		= 0;
		uTransitStart		= 0;
		uTransitExpire		= 0;

		Transaction::pointer	trans	= Transaction::sharedTransitSet(
			naAccountPublic, naAccountPrivate,
			naSrcAccountID,
			asSrc->getSeq(),
			theConfig.FEE_DEFAULT,
			0,											// YYY No source tag
			uTransitRate,
			uTransitStart,
			uTransitExpire);

		(void) mNetOps->processTransaction(trans);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();
		obj["seed"]				= naSeed.humanFamilySeed();
		obj["srcAccountID"]		= naSrcAccountID.humanAccountID();
		obj["transitRate"]		= uTransitRate;
		obj["transitStart"]		= uTransitStart;
		obj["transitExpire"]	= uTransitExpire;

		return obj;
	}
}

Json::Value RPCServer::doTx(Json::Value& params)
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

		Transaction::pointer txn=theApp->getMasterTransaction().fetch(txid, true);

		if (!txn) return RPCError(rpcTXN_NOT_FOUND);

		return txn->getJson(true);
	}

	return RPCError(rpcNOT_IMPL);
}

// ledger [id|current|lastclosed] [full]
Json::Value RPCServer::doLedger(Json::Value& params)
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
	else if (param == "lastclosed")
		ledger = theApp->getMasterLedger().getClosedLedger();
	else if (param.size() > 12)
		ledger = theApp->getMasterLedger().getLedgerByHash(uint256(param));
	else
		ledger = theApp->getMasterLedger().getLedgerBySeq(boost::lexical_cast<uint32>(param));

	if (!ledger)
		return RPCError(rpcLGR_NOT_FOUND);

	bool full = false;
	if (extractString(param, params, 1))
	{
		if (param == "full")
			full = true;
	}

	Json::Value ret(Json::objectValue);
	ledger->addJson(ret, full ? LEDGER_JSON_FULL : 0);
	return ret;
}

// account_tx <account> <minledger> <maxledger>
// account_tx <account> <ledger>
Json::Value RPCServer::doAccountTransactions(Json::Value& params)
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

	minLedger = boost::lexical_cast<uint32>(param);

	if ((params.size() == 3) && extractString(param, params, 2))
		maxLedger = boost::lexical_cast<uint32>(param);
	else
		maxLedger = minLedger;

	if ((maxLedger < minLedger) || (minLedger == 0) || (maxLedger == 0))
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
		ret["Account"] = account.humanAccountID();
		Json::Value ledgers(Json::arrayValue);

		uint32 currentLedger = 0;
		Json::Value ledger, jtxns;
		for (std::vector< std::pair<uint32, uint256> >::iterator it = txns.begin(), end = txns.end(); it != end; ++it)
		{
			if (it->first != currentLedger) // different/new ledger
			{
				if (currentLedger != 0) // add old ledger
				{
					ledger["Transactions"] = jtxns;
					ledgers.append(ledger);
					ledger = Json::objectValue;
				}
				currentLedger = it->first;
				ledger["LedgerSeq"] = currentLedger;
				jtxns = Json::arrayValue;
			}
			jtxns.append(it->second.GetHex());
		}
		if (currentLedger != 0)
		{
			ledger["Transactions"] = jtxns;
			ledgers.append(ledger);
		}

		ret["Ledgers"] = ledgers;
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
Json::Value RPCServer::doUnlAdd(Json::Value& params)
{
	std::string	strNode		= params[0u].asString();
	std::string strComment	= (params.size() == 2) ? "" : params[1u].asString();

	NewcoinAddress	nodePublic;

	if (nodePublic.setNodePublic(strNode))
	{
		theApp->getUNL().nodeAddPublic(nodePublic, strComment);

		return "adding node by public key";
	}
	else
	{
		theApp->getUNL().nodeAddDomain(strNode, UniqueNodeList::vsManual, strComment);

		return "adding node by domain";
	}

	return "invalid params";
}

// validation_create
// validation_create <pass_phrase>
// validation_create <seed>
// validation_create <seed_key>
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCServer::doValidatorCreate(Json::Value& params) {
	NewcoinAddress	familySeed;
	NewcoinAddress	familyGenerator;
	NewcoinAddress	nodePublicKey;
	NewcoinAddress	nodePrivateKey;

	if (params.empty())
	{
		std::cerr << "Creating random validation seed." << std::endl;

		familySeed.setFamilySeedRandom();					// Get a random seed.
	}
	else if (!familySeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}

	// Derive generator from seed.
	familyGenerator.setFamilyGenerator(familySeed);

	// The node public and private is 0th of the sequence.
	nodePublicKey.setNodePublic(CKey(familyGenerator, 0).GetPubKey());
	nodePrivateKey.setNodePrivate(CKey(familyGenerator, familySeed.getFamilyPrivateKey(), 0).GetSecret());

	// Paranoia
	assert(1 == familySeed.setFamilySeed1751(familySeed.humanFamilySeed1751()));

	Json::Value obj(Json::objectValue);

	obj["validation_public_key"]	= nodePublicKey.humanNodePublic();
	obj["validation_seed"]			= familySeed.humanFamilySeed();
	obj["validation_key"]			= familySeed.humanFamilySeed1751();

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
Json::Value RPCServer::doWalletAccounts(Json::Value& params)
{
	NewcoinAddress	naSeed;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return "seed expected";
	}

	NewcoinAddress	naMasterGenerator;

	// Try the seed as a master seed.
	naMasterGenerator.setFamilyGenerator(naSeed);

	Json::Value jsonAccounts	= accounts(uLedger, naMasterGenerator);

	if (jsonAccounts.empty())
	{
		// No account via seed as master, try seed a regular.
		Json::Value	ret	= getMasterGenerator(uLedger, naSeed, naMasterGenerator);

		if (!ret.empty())
			return ret;

		ret["accounts"]	= accounts(uLedger, naMasterGenerator);

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
Json::Value RPCServer::doWalletAdd(Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;
	NewcoinAddress	naSrcAccountID;
	STAmount		saAmount;
	std::string		sDstCurrency;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (!naRegularSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return "regular seed expected";
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return RPCError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naMasterSeed.setFamilySeedGeneric(params[2u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else if (params.size() >= 4 && !saAmount.setValue(params[3u].asString(), sDstCurrency))
	{
		return RPCError(rpcDST_AMT_MALFORMED);
	}
	else
	{
		NewcoinAddress			naMasterGenerator;
		NewcoinAddress			naRegularGenerator;

		naMasterGenerator.setFamilyGenerator(naMasterSeed);
		naRegularGenerator.setFamilyGenerator(naRegularSeed);

		NewcoinAddress			naAccountPublic;
		NewcoinAddress			naAccountPrivate;
	    AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj			= authorize(uLedger, naRegularSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
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

				asNew	= mNetOps->getAccountState(uLedger, naNewAccountPublic);
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

			(void) mNetOps->processTransaction(trans);

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
Json::Value RPCServer::doWalletClaim(Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;

	if (!naMasterSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setFamilySeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		// Building:
		//   peer_wallet_claim <account_id> <authorized_key> <encrypted_master_public_generator> <generator_pubkey> <generator_signature>
		//		<source_tag> [<annotation>]
		//
		//
		// Which has no confidential information.

		// XXX Need better parsing.
		uint32		uSourceTag		= (params.size() == 2) ? 0 : boost::lexical_cast<uint32>(params[2u].asString());
		// XXX Annotation is ignored.
		std::string strAnnotation	= (params.size() == 3) ? "" : params[3u].asString();

		NewcoinAddress	naMasterGenerator;
		NewcoinAddress	naRegularGenerator;
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		NewcoinAddress	naAccountPublic;
		NewcoinAddress	naAccountPrivate;

		naMasterGenerator.setFamilyGenerator(naMasterSeed);
		naAccountPublic.setAccountPublic(naMasterGenerator, 0);
		naAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, 0);

		naRegularGenerator.setFamilyGenerator(naRegularSeed);

		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

		// Hash of regular account #0 public key.
		uint160						uGeneratorID		= naRegular0Public.getAccountID();
		std::vector<unsigned char>	vucGeneratorCipher	= naRegular0Private.accountPrivateEncrypt(naRegular0Public, naMasterGenerator.getFamilyGenerator());
		std::vector<unsigned char>	vucGeneratorSig;

		// Prove that we have the corresponding private key to the generator id.  So, we can get the generator id.
		// XXX Check result.
		naRegular0Private.accountPrivateSign(Serializer::getSHA512Half(vucGeneratorCipher), vucGeneratorSig);

		Transaction::pointer	trns	= Transaction::sharedClaim(
			naAccountPublic, naAccountPrivate,
			uSourceTag,
			vucGeneratorCipher,
			naRegular0Public.getAccountPublic(),
			vucGeneratorSig);

		(void) mNetOps->processTransaction(trns);

		Json::Value obj(Json::objectValue);

		// We "echo" the seeds so they can be checked.
		obj["master_seed"]		= naMasterSeed.humanFamilySeed();
		obj["master_key"]		= naMasterSeed.humanFamilySeed1751();
		obj["regular_seed"]		= naRegularSeed.humanFamilySeed();
		obj["regular_key"]		= naRegularSeed.humanFamilySeed1751();

		obj["account_id"]		= naAccountPublic.humanAccountID();
		obj["generator_id"]		= strHex(uGeneratorID);
		obj["generator"]		= strHex(vucGeneratorCipher);
		obj["annotation"]		= strAnnotation;

		obj["transaction"]		= trns->getSTransaction()->getJson(0);
		obj["status"]			= trns->getStatus();

		return obj;
	}
}

// wallet_create regular_seed paying_account account_id [initial_funds]
// We don't allow creating an account_id by default here because we want to make sure the person has a chance to write down the
// master seed of the account to be created.
// YYY Need annotation and source tag
Json::Value RPCServer::doWalletCreate(Json::Value& params)
{
	NewcoinAddress	naSrcAccountID;
	NewcoinAddress	naDstAccountID;
	NewcoinAddress	naSeed;
	uint256			uLedger		= mNetOps->getCurrentLedger();

	if (!naSeed.setFamilySeedGeneric(params[0u].asString()))
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
	else if (mNetOps->getAccountState(uLedger, naDstAccountID))
	{
		return RPCError(rpcACT_EXISTS);
	}

	// Trying to build:
	//   peer_wallet_create <paying_account> <paying_signature> <account_id> [<initial_funds>] [<annotation>]

	NewcoinAddress			naMasterGenerator;
	NewcoinAddress			naAccountPublic;
	NewcoinAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uLedger, naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_ACCOUNT_CREATE, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	STAmount				saInitialFunds	= (params.size() < 4) ? 0 : boost::lexical_cast<uint64>(params[3u].asString());

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

	(void) mNetOps->processTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// wallet_propose
Json::Value RPCServer::doWalletPropose(Json::Value& params)
{
	NewcoinAddress	naSeed;
	NewcoinAddress	naGenerator;
	NewcoinAddress	naAccount;

	naSeed.setFamilySeedRandom();
	naGenerator.setFamilyGenerator(naSeed);
	naAccount.setAccountPublic(naGenerator, 0);

	//
	// Extra functionality: generate a key pair
	//
	CKey	key;

	key.MakeNewKey();

	Json::Value obj(Json::objectValue);

	obj["master_seed"]		= naSeed.humanFamilySeed();
	obj["master_key"]		= naSeed.humanFamilySeed1751();
	obj["account_id"]		= naAccount.humanAccountID();
	obj["extra_public"]		= NewcoinAddress::createHumanAccountPublic(key.GetPubKey());
	obj["extra_private"]	= NewcoinAddress::createHumanAccountPrivate(key.GetSecret());

	return obj;
}

// wallet_seed [<seed>|<passphrase>|<passkey>]
Json::Value RPCServer::doWalletSeed(Json::Value& params)
{
	NewcoinAddress	naSeed;

	if (params.size()
		&& !naSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return RPCError(rpcBAD_SEED);
	}
	else
	{
		NewcoinAddress	naGenerator;
		NewcoinAddress	naAccount;

		if (!params.size())
		{
			naSeed.setFamilySeedRandom();
		}
		naGenerator.setFamilyGenerator(naSeed);
		naAccount.setAccountPublic(naGenerator, 0);

		Json::Value obj(Json::objectValue);

		obj["seed"]		= naSeed.humanFamilySeed();
		obj["key"]		= naSeed.humanFamilySeed1751();

		return obj;
	}
}

void RPCServer::validatorsResponse(const boost::system::error_code& err, std::string strResponse)
{
	std::cerr << "Fetch '" VALIDATORS_FILE_NAME "' complete." << std::endl;

	if (!err)
	{
		theApp->getUNL().nodeDefault(strResponse);
	}
	else
	{
		std::cerr << "Error: " << err.message() << std::endl;
	}
}

// Populate the UNL from a validators.txt file.
Json::Value RPCServer::doUnlDefault(Json::Value& params) {
	if (!params.size() || (1==params.size() && !params[0u].compare("network")))
	{
		bool			bNetwork	= 1 == params.size();
		std::string		strValidators;

		if (!bNetwork)
		{
			std::ifstream	ifsDefault(VALIDATORS_FILE_NAME, std::ios::in);

			if (!ifsDefault)
			{
				std::cerr << "Failed to open '" VALIDATORS_FILE_NAME "'." << std::endl;

				bNetwork	= true;
			}
			else
			{
				strValidators.assign((std::istreambuf_iterator<char>(ifsDefault)),
					std::istreambuf_iterator<char>());

				if (ifsDefault.bad())
				{
					std::cerr << "Failed to read '" VALIDATORS_FILE_NAME "'." << std::endl;

					bNetwork	= true;
				}
			}
		}

		if (bNetwork)
		{
			HttpsClient::httpsGet(
				theApp->getIOService(),
				VALIDATORS_SITE,
				443,
				VALIDATORS_FILE_PATH,
				VALIDATORS_FILE_BYTES_MAX,
				boost::posix_time::seconds(VALIDATORS_FETCH_SECONDS),
				boost::bind(&RPCServer::validatorsResponse, this, _1, _2));

			return "fetching " VALIDATORS_FILE_NAME;
		}
		else
		{
			theApp->getUNL().nodeDefault(strValidators);

			return "processing " VALIDATORS_FILE_NAME;
		}
	}
	else
		return RPCError(rpcINVALID_PARAMS);
}

// unl_delete <public_key>
Json::Value RPCServer::doUnlDelete(Json::Value& params) {
	std::string	strNodePublic = params[0u].asString();

	NewcoinAddress	naNodePublic;

	if (naNodePublic.setNodePublic(strNodePublic))
	{
		theApp->getUNL().nodeRemove(naNodePublic);

		return "removing node";
	}
	else
	{
		return "invalid public key";
	}
}

Json::Value RPCServer::doUnlList(Json::Value& params) {
	return theApp->getUNL().getUnlJson();
}

// unl_reset
Json::Value RPCServer::doUnlReset(Json::Value& params) {
	theApp->getUNL().nodeReset();

	return "removing nodes";
}

// unl_score
Json::Value RPCServer::doUnlScore(Json::Value& params) {
	theApp->getUNL().nodeScore();

	return "scoring requested";
}

Json::Value RPCServer::doStop(Json::Value& params) {
	theApp->stop();

	return SYSTEM_NAME " server stopping";
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	std::cerr << "RPC:" << command << std::endl;

	static struct {
		const char* pCommand;
		doFuncPtr	dfpFunc;
		int			iMinParams;
		int			iMaxParams;
		unsigned int	iOptions;
	} commandsA[] = {
		{	"account_email_set",	&RPCServer::doAccountEmailSet,		2, 3, optCurrent },
		{	"account_info",			&RPCServer::doAccountInfo,			1, 2, optCurrent },
		{	"account_lines",		&RPCServer::doAccountLines,			1, 2, optCurrent|optClosed },
		{	"account_message_set",	&RPCServer::doAccountMessageSet,	3, 3, optCurrent },
		{	"account_tx",			&RPCServer::doAccountTransactions,	2, 3, optNetwork },
		{	"account_wallet_set",	&RPCServer::doAccountWalletSet,		2, 3, optCurrent },
		{	"connect",				&RPCServer::doConnect,				1, 2,  },
		{	"credit_set",			&RPCServer::doCreditSet,			4, 6, optCurrent },
		{	"data_delete",			&RPCServer::doDataDelete,			1, 1,  },
		{	"data_fetch",			&RPCServer::doDataFetch,			1, 1,  },
		{	"data_store",			&RPCServer::doDataStore,			2, 2,  },
		{	"ledger",				&RPCServer::doLedger,				0, 2, optNetwork },
		{	"nickname_info",		&RPCServer::doNicknameInfo,			1, 1, optCurrent },
		{	"nickname_set",			&RPCServer::doNicknameSet,			2, 3, optCurrent },
		{	"password_fund",		&RPCServer::doPasswordFund,			2, 3, optCurrent },
		{	"password_set",			&RPCServer::doPasswordSet,			2, 3, optNetwork },
		{	"peers",				&RPCServer::doPeers,				0, 0,  },
		{	"send",					&RPCServer::doSend,					3, 7, optCurrent },
		{	"stop",					&RPCServer::doStop,					0, 0,  },
		{	"transit_set",			&RPCServer::doTransitSet,			5, 5, optCurrent },
		{	"tx",					&RPCServer::doTx,					1, 1,  },

		{	"unl_add",				&RPCServer::doUnlAdd,				1, 2,  },
		{	"unl_default",			&RPCServer::doUnlDefault,			0, 1,  },
		{	"unl_delete",			&RPCServer::doUnlDelete,			1, 1,  },
		{	"unl_list",				&RPCServer::doUnlList,				0, 0,  },
		{	"unl_reset",			&RPCServer::doUnlReset,				0, 0,  },
		{	"unl_score",			&RPCServer::doUnlScore,				0, 0,  },

		{	"validation_create",	&RPCServer::doValidatorCreate,		0, 1,  },

		{	"wallet_accounts",		&RPCServer::doWalletAccounts,		1, 1,  optCurrent },
		{	"wallet_add",			&RPCServer::doWalletAdd,			3, 5,  optCurrent },
		{	"wallet_claim",			&RPCServer::doWalletClaim,			2, 4,  optNetwork },
		{	"wallet_create",		&RPCServer::doWalletCreate,			3, 4,  optCurrent },
		{	"wallet_propose",		&RPCServer::doWalletPropose,		0, 0,  },
		{	"wallet_seed",			&RPCServer::doWalletSeed,			0, 1,  },
	};

	int		i = NUMBER(commandsA);

	while (i-- && command != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return RPCError(rpcUNKNOWN_COMMAND);
	}
	else if (params.size() < commandsA[i].iMinParams || params.size() > commandsA[i].iMaxParams)
	{
		return RPCError(rpcINVALID_PARAMS);
	}
	else if ((commandsA[i].iOptions & optNetwork) && !mNetOps->available())
	{
		return RPCError(rpcNO_NETWORK);
	}
	else if ((commandsA[i].iOptions & optCurrent) && mNetOps->getCurrentLedger().isZero())
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
			boost::bind(&RPCServer::handle_write, shared_from_this(),
			boost::asio::placeholders::error));
}



void RPCServer::handle_write(const boost::system::error_code& e)
{
	//std::cout << "async_write complete " << e << std::endl;

	if(!e)
	{
		// Initiate graceful connection closure.
		boost::system::error_code ignored_ec;
		mSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
	}

	if (e != boost::asio::error::operation_aborted)
	{
		//connection_manager_.stop(shared_from_this());
	}
}

// vim:ts=4
