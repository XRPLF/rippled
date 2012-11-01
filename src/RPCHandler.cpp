#include "NetworkOPs.h"
#include "RPCHandler.h"
#include "Application.h"
#include "Log.h"
#include "RippleLines.h"
#include "Wallet.h"
#include "RippleAddress.h"
#include "AccountState.h"
#include "NicknameState.h"
#include "InstanceCounter.h"

#include "Pathfinder.h"
#include <boost/foreach.hpp>
#include <openssl/md5.h>
/*
carries out the RPC 

*/

SETUP_LOG();


Json::Value RPCHandler::rpcError(int iError)
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
		{ rpcNOT_STANDALONE,		"notStandAlone",	"Operation valid in debug mode only."					},
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

	Json::Value	jsonResult(Json::objectValue);

	jsonResult["error"]			= i >= 0 ? errorInfoA[i].pToken : lexical_cast_i(iError);
	jsonResult["error_message"]	= i >= 0 ? errorInfoA[i].pMessage : lexical_cast_i(iError);
	jsonResult["error_code"]	= iError;
	if (i >= 0)
		std::cerr << "rpcError: "
		<< errorInfoA[i].pToken << ": " << errorInfoA[i].pMessage << std::endl;

	return jsonResult;
}


RPCHandler::RPCHandler(NetworkOPs* netOps)
{
	mNetOps=netOps;
}

int RPCHandler::getParamCount(const Json::Value& params)
{ // If non-array, only counts strings
	if (params.isNull()) return 0;
	if (params.isArray()) return params.size();
	if (!params.isConvertibleTo(Json::stringValue))
		return 0;
	return 1;
}
bool RPCHandler::extractString(std::string& param, const Json::Value& params, int index)
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
Json::Value RPCHandler::getMasterGenerator(const uint256& uLedger, const RippleAddress& naRegularSeed, RippleAddress& naMasterGenerator)
{
	RippleAddress		na0Public;		// To find the generator's index.
	RippleAddress		na0Private;		// To decrypt the master generator's cipher.
	RippleAddress		naGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);

	na0Public.setAccountPublic(naGenerator, 0);
	na0Private.setAccountPrivate(naGenerator, naRegularSeed, 0);

	SLE::pointer		sleGen			= mNetOps->getGenerator(uLedger, na0Public.getAccountID());

	if (!sleGen)
	{
		// No account has been claimed or has had it password set for seed.
		return rpcError(rpcNO_ACCOUNT);
	}

	std::vector<unsigned char>	vucCipher			= sleGen->getFieldVL(sfGenerator);
	std::vector<unsigned char>	vucMasterGenerator	= na0Private.accountPrivateDecrypt(na0Public, vucCipher);
	if (vucMasterGenerator.empty())
	{
		return rpcError(rpcFAIL_GEN_DECRPYT);
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
Json::Value RPCHandler::authorize(const uint256& uLedger,
	const RippleAddress& naRegularSeed, const RippleAddress& naSrcAccountID,
	RippleAddress& naAccountPublic, RippleAddress& naAccountPrivate,
	STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
	const RippleAddress& naVerifyGenerator)
{
	// Source/paying account must exist.
	asSrc	= mNetOps->getAccountState(uLedger, naSrcAccountID);
	if (!asSrc)
	{
		return rpcError(rpcSRC_ACT_MISSING);
	}

	RippleAddress	naMasterGenerator;

	if (asSrc->bHaveAuthorizedKey())
	{
		Json::Value	obj	= getMasterGenerator(uLedger, naRegularSeed, naMasterGenerator);

		if (!obj.empty())
			return obj;
	}
	else
	{
		// Try the seed as a master seed.
		naMasterGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);
	}

	// If naVerifyGenerator is provided, make sure it is the master generator.
	if (naVerifyGenerator.isValid() && naMasterGenerator != naVerifyGenerator)
	{
		return rpcError(rpcWRONG_SEED);
	}

	// Find the index of the account from the master generator, so we can generate the public and private keys.
	RippleAddress		naMasterAccountPublic;
	unsigned int		iIndex	= 0;
	bool				bFound	= false;

	// Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
	// related.
	while (!bFound && iIndex != theConfig.ACCOUNT_PROBE_MAX)
	{
		naMasterAccountPublic.setAccountPublic(naMasterGenerator, iIndex);

		cLog(lsDEBUG) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID() << " : " << naSrcAccountID.humanAccountID();

		bFound	= naSrcAccountID.getAccountID() == naMasterAccountPublic.getAccountID();
		if (!bFound)
			++iIndex;
	}

	if (!bFound)
	{
		return rpcError(rpcACT_NOT_FOUND);
	}

	// Use the regular generator to determine the associated public and private keys.
	RippleAddress		naGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);

	naAccountPublic.setAccountPublic(naGenerator, iIndex);
	naAccountPrivate.setAccountPrivate(naGenerator, naRegularSeed, iIndex);

	if (asSrc->bHaveAuthorizedKey() && (asSrc->getAuthorizedKey().getAccountID() != naAccountPublic.getAccountID()))
	{
		// std::cerr << "iIndex: " << iIndex << std::endl;
		// std::cerr << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID()) << std::endl;
		// std::cerr << "naAccountPublic: " << strHex(naAccountPublic.getAccountID()) << std::endl;

		return rpcError(rpcPASSWD_CHANGED);
	}

	saSrcBalance	= asSrc->getBalance();

	if (saSrcBalance < saFee)
	{
		cLog(lsINFO) << "authorize: Insufficent funds for fees: fee=" << saFee.getText() << " balance=" << saSrcBalance.getText();

		return rpcError(rpcINSUF_FUNDS);
	}
	else
	{
		saSrcBalance -= saFee;
	}

	return Json::Value();
}

// --> strIdent: public key, account ID, or regular seed.
// <-- bIndex: true if iIndex > 0 and used the index.
Json::Value RPCHandler::accountFromString(const uint256& uLedger, RippleAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex)
{
	RippleAddress	naSeed;

	if (naAccount.setAccountPublic(strIdent) || naAccount.setAccountID(strIdent))
	{
		// Got the account.
		bIndex	= false;
	}
	// Must be a seed.
	else if (!naSeed.setSeedGeneric(strIdent))
	{
		return rpcError(rpcBAD_SEED);
	}
	else
	{
		// We allow the use of the seeds to access #0.
		// This is poor practice and merely for debuging convenience.
		RippleAddress		naRegular0Public;
		RippleAddress		naRegular0Private;

		RippleAddress		naGenerator		= RippleAddress::createGeneratorPublic(naSeed);

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
			std::vector<unsigned char>	vucCipher				= sleGen->getFieldVL(sfGenerator);
			std::vector<unsigned char>	vucMasterGenerator		= naRegular0Private.accountPrivateDecrypt(naRegular0Public, vucCipher);
			if (vucMasterGenerator.empty())
			{
				rpcError(rpcNO_GEN_DECRPYT);
			}

			naGenerator.setGenerator(vucMasterGenerator);
		}

		bIndex	= !iIndex;

		naAccount.setAccountPublic(naGenerator, iIndex);
	}

	return Json::Value(Json::objectValue);
}

Json::Value RPCHandler::doAcceptLedger(const Json::Value &params)
{
	if (!theConfig.RUN_STANDALONE)
		return rpcError(rpcNOT_STANDALONE);

	Json::Value obj(Json::objectValue);
	obj["newLedger"] = theApp->getOPs().acceptLedger();
	return obj;
}

// account_domain_set <seed> <paying_account> [<domain>]
Json::Value RPCHandler::doAccountDomainSet(const Json::Value &params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress			naVerifyGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
		RippleAddress(),
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
Json::Value RPCHandler::doAccountEmailSet(const Json::Value &params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress			naVerifyGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
		RippleAddress(),
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
Json::Value RPCHandler::doAccountInfo(const Json::Value &params)
{
	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;
	RippleAddress	naAccount;

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
Json::Value RPCHandler::doAccountMessageSet(const Json::Value& params) {
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;
	RippleAddress	naMessagePubKey;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naMessagePubKey.setAccountPublic(params[2u].asString()))
	{
		return rpcError(rpcPUBLIC_MALFORMED);
	}

	RippleAddress				naVerifyGenerator;
	RippleAddress				naAccountPublic;
	RippleAddress				naAccountPrivate;
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
Json::Value RPCHandler::doAccountPublishSet(const Json::Value &params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress			naVerifyGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
		RippleAddress(),
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
Json::Value RPCHandler::doAccountRateSet(const Json::Value &params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress			naVerifyGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
		RippleAddress(),
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
Json::Value RPCHandler::doAccountWalletSet(const Json::Value& params) {
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress				naMasterGenerator;
	RippleAddress				naAccountPublic;
	RippleAddress				naAccountPrivate;
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
		RippleAddress(),
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

Json::Value RPCHandler::doConnect(const Json::Value& params)
{
	if (theConfig.RUN_STANDALONE)
		return "cannot connect in standalone mode";

	// connect <ip> [port]
	std::string strIp;
	int			iPort	= -1;

	// XXX Might allow domain for manual connections.
	if (!extractString(strIp, params, 0))
		return rpcError(rpcHOST_IP_MALFORMED);

	if (params.size() == 2)
	{
		std::string strPort;

		// YYY Should make an extract int.
		if (!extractString(strPort, params, 1))
			return rpcError(rpcPORT_MALFORMED);

		iPort	= lexical_cast_s<int>(strPort);
	}

	// XXX Validate legal IP and port
	theApp->getConnectionPool().connectTo(strIp, iPort);

	return "connecting";
}

// data_delete <key>
Json::Value RPCHandler::doDataDelete(const Json::Value& params)
{
	std::string	strKey = params[0u].asString();

	Json::Value	ret = Json::Value(Json::objectValue);

	if (theApp->getWallet().dataDelete(strKey))
	{
		ret["key"]		= strKey;
	}
	else
	{
		ret	= rpcError(rpcINTERNAL);
	}

	return ret;
}

// data_fetch <key>
Json::Value RPCHandler::doDataFetch(const Json::Value& params)
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
Json::Value RPCHandler::doDataStore(const Json::Value& params)
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
		ret	= rpcError(rpcINTERNAL);
	}

	return ret;
}

// nickname_info <nickname>
// Note: Nicknames are not automatically looked up by commands as they are advisory and can be changed.
Json::Value RPCHandler::doNicknameInfo(const Json::Value& params)
{
	std::string	strNickname	= params[0u].asString();
	boost::trim(strNickname);

	if (strNickname.empty())
	{
		return rpcError(rpcNICKNAME_MALFORMED);
	}

	NicknameState::pointer	nsSrc	= mNetOps->getNicknameState(uint256(0), strNickname);
	if (!nsSrc)
	{
		return rpcError(rpcNICKNAME_MISSING);
	}

	Json::Value ret(Json::objectValue);

	ret["nickname"]	= strNickname;

	nsSrc->addJson(ret);

	return ret;
}

// nickname_set <seed> <paying_account> <nickname> [<offer_minimum>] [<authorization>]
Json::Value RPCHandler::doNicknameSet(const Json::Value& params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	STAmount					saMinimumOffer;
	bool						bSetOffer		= params.size() >= 4;
	std::string					strOfferCurrency;
	std::string					strNickname		= params[2u].asString();
	boost::trim(strNickname);

	if (strNickname.empty())
	{
		return rpcError(rpcNICKNAME_MALFORMED);
	}
	else if (params.size() >= 4 && !saMinimumOffer.setFullValue(params[3u].asString(), strOfferCurrency))
	{
		return rpcError(rpcDST_AMT_MALFORMED);
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
		return rpcError(rpcNICKNAME_PERM);
	}
	else
	{
		// Setting the minimum offer.
		saFee	= theConfig.FEE_DEFAULT;
	}

	RippleAddress			naMasterGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
		saMinimumOffer);

	trans	= mNetOps->submitTransaction(trans);

	obj["transaction"]	= trans->getSTransaction()->getJson(0);
	obj["status"]		= trans->getStatus();

	return obj;
}

// offer_create <seed> <paying_account> <takers_gets_amount> <takers_gets_currency> <takers_gets_issuer> <taker_pays_amount> <taker_pays_currency> <taker_pays_issuer> <expires> [passive]
// *offering* for *wants*
Json::Value RPCHandler::doOfferCreate(const Json::Value &params)
{
	RippleAddress	naSeed;
	RippleAddress	naSrcAccountID;
	STAmount		saTakerPays;
	STAmount		saTakerGets;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!saTakerGets.setFullValue(params[2u].asString(), params[3u].asString(), params[4u].asString()))
	{
		return rpcError(rpcGETS_AMT_MALFORMED);
	}
	else if (!saTakerPays.setFullValue(params[5u].asString(), params[6u].asString(), params[7u].asString()))
	{
		return rpcError(rpcPAYS_AMT_MALFORMED);
	}
	else if (params.size() == 10 && params[9u].asString() != "passive")
	{
		return rpcError(rpcINVALID_PARAMS);
	}

	uint32					uExpiration	= lexical_cast_s<int>(params[8u].asString());
	bool					bPassive	= params.size() == 10;

	RippleAddress			naMasterGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
Json::Value RPCHandler::doOfferCancel(const Json::Value &params)
{
	RippleAddress	naSeed;
	RippleAddress	naSrcAccountID;
	uint32			uSequence	= lexical_cast_s<int>(params[2u].asString());

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	RippleAddress			naMasterGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
Json::Value RPCHandler::doOwnerInfo(const Json::Value& params)
{
	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;
	RippleAddress	naAccount;

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
Json::Value RPCHandler::doPasswordFund(const Json::Value &params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naDstAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[params.size() == 3 ? 2u : 1u].asString()))
	{
		return rpcError(rpcDST_ACT_MALFORMED);
	}

	RippleAddress			naMasterGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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
Json::Value RPCHandler::doPasswordSet(const Json::Value& params)
{
	RippleAddress	naMasterSeed;
	RippleAddress	naRegularSeed;
	RippleAddress	naAccountID;

	if (!naMasterSeed.setSeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return rpcError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setSeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return rpcError(rpcBAD_SEED);
	}
	// YYY Might use account from string to be more flexible.
	else if (params.size() >= 3 && !naAccountID.setAccountID(params[2u].asString()))
	{
		return rpcError(rpcACT_MALFORMED);
	}
	else
	{
		RippleAddress	naMasterGenerator	= RippleAddress::createGeneratorPublic(naMasterSeed);
		RippleAddress	naRegularGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);
		RippleAddress	naRegular0Public;
		RippleAddress	naRegular0Private;

		RippleAddress	naAccountPublic;
		RippleAddress	naAccountPrivate;

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

		RippleAddress		naMasterXPublic;
		RippleAddress		naRegularXPublic;
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
			return rpcError(rpcACT_NOT_FOUND);
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

Json::Value RPCHandler::doPeers(const Json::Value& params)
{
	// peers
	Json::Value obj(Json::objectValue);
	obj["peers"]=theApp->getConnectionPool().getPeersJson();
	return obj;
}

// profile offers <pass_a> <account_a> <currency_offer_a> <account_b> <currency_offer_b> <count> [submit]
// profile 0:offers 1:pass_a 2:account_a 3:currency_offer_a 4:account_b 5:currency_offer_b 6:<count> 7:[submit]
// issuer is the offering account
// --> submit: 'submit|true|false': defaults to false
// Prior to running allow each to have a credit line of what they will be getting from the other account.
Json::Value RPCHandler::doProfile(const Json::Value &params)
{
	int				iArgs	= params.size();
	RippleAddress	naSeedA;
	RippleAddress	naAccountA;
	uint160			uCurrencyOfferA;
	RippleAddress	naSeedB;
	RippleAddress	naAccountB;
	uint160			uCurrencyOfferB;
	uint32			iCount	= 100;
	bool			bSubmit	= false;

	if (iArgs < 6 || "offers" != params[0u].asString())
	{
		return rpcError(rpcINVALID_PARAMS);
	}

	if (!naSeedA.setSeedGeneric(params[1u].asString()))							// <pass_a>
		return rpcError(rpcINVALID_PARAMS);

	naAccountA.setAccountID(params[2u].asString());								// <account_a>

	if (!STAmount::currencyFromString(uCurrencyOfferA, params[3u].asString()))	// <currency_offer_a>
		return rpcError(rpcINVALID_PARAMS);

	naAccountB.setAccountID(params[4u].asString());								// <account_b>
	if (!STAmount::currencyFromString(uCurrencyOfferB, params[5u].asString()))	// <currency_offer_b>
		return rpcError(rpcINVALID_PARAMS);

	iCount	= lexical_cast_s<uint32>(params[6u].asString());

	if (iArgs >= 8 && "false" != params[7u].asString())
		bSubmit	= true;

	Log::setMinSeverity(lsFATAL,true);

	boost::posix_time::ptime			ptStart(boost::posix_time::microsec_clock::local_time());

	for(unsigned int n=0; n<iCount; n++) 
	{
		RippleAddress			naMasterGeneratorA;
		RippleAddress			naAccountPublicA;
		RippleAddress			naAccountPrivateA;
		AccountState::pointer	asSrcA;
		STAmount				saSrcBalanceA;

		Json::Value				jvObjA		= authorize(uint256(0), naSeedA, naAccountA, naAccountPublicA, naAccountPrivateA,
			saSrcBalanceA, theConfig.FEE_DEFAULT, asSrcA, naMasterGeneratorA);

		if (!jvObjA.empty())
			return jvObjA;

		Transaction::pointer	tpOfferA	= Transaction::sharedOfferCreate(
			naAccountPublicA, naAccountPrivateA,
			naAccountA,													// naSourceAccount,
			asSrcA->getSeq(),											// uSeq
			theConfig.FEE_DEFAULT,
			0,															// uSourceTag,
			false,														// bPassive
			STAmount(uCurrencyOfferA, naAccountA.getAccountID(), 1),	// saTakerPays
			STAmount(uCurrencyOfferB, naAccountB.getAccountID(), 1+n),	// saTakerGets
			0);															// uExpiration

		if(bSubmit)
			tpOfferA	= mNetOps->submitTransaction(tpOfferA);
	}

	boost::posix_time::ptime			ptEnd(boost::posix_time::microsec_clock::local_time());
	boost::posix_time::time_duration	tdInterval		= ptEnd-ptStart;
	long								lMicroseconds	= tdInterval.total_microseconds();
	int									iTransactions	= iCount;
	float								fRate			= lMicroseconds ? iTransactions/(lMicroseconds/1000000.0) : 0.0;

	Json::Value obj(Json::objectValue);

	obj["transactions"]		= iTransactions;
	obj["submit"]			= bSubmit;
	obj["start"]			= boost::posix_time::to_simple_string(ptStart);
	obj["end"]				= boost::posix_time::to_simple_string(ptEnd);
	obj["interval"]			= boost::posix_time::to_simple_string(tdInterval);
	obj["rate_per_second"]	= fRate;

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
Json::Value RPCHandler::doRipple(const Json::Value &params)
{
	RippleAddress	naSeed;
	STAmount		saSrcAmountMax;
	uint160			uSrcCurrencyID;
	RippleAddress	naSrcAccountID;
	RippleAddress	naSrcIssuerID;
	bool			bPartial;
	bool			bFull;
	bool			bLimit;
	bool			bAverage;
	RippleAddress	naDstAccountID;
	STAmount		saDstAmount;
	uint160			uDstCurrencyID;

	STPathSet		spsPaths;

	naSrcIssuerID.setAccountID(params[4u].asString());							// <source_issuerID>

	if (!naSeed.setSeedGeneric(params[0u].asString()))							// <regular_seed>
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))				// <paying_account>
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	// <source_max> <source_currency> [<source_issuerID>]
	else if (!saSrcAmountMax.setFullValue(params[2u].asString(), params[3u].asString(), params[naSrcIssuerID.isValid() ? 4u : 1u].asString()))
	{
		// Log(lsINFO) << "naSrcIssuerID.isValid(): " << naSrcIssuerID.isValid();
		// Log(lsINFO) << "source_max: " << params[2u].asString();
		// Log(lsINFO) << "source_currency: " << params[3u].asString();
		// Log(lsINFO) << "source_issuer: " << params[naSrcIssuerID.isValid() ? 4u : 2u].asString();

		return rpcError(rpcSRC_AMT_MALFORMED);
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
				RippleAddress	naIssuerID;

				++iArg;

				if (!STAmount::currencyFromString(uCurrencyID, params[iArg++].asString()))	// <currency>
				{
					return rpcError(rpcINVALID_PARAMS);
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
				RippleAddress	naAccountID;
				uint160			uCurrencyID;
				RippleAddress	naIssuerID;

				++iArg;

				if (!naAccountID.setAccountID(params[iArg++].asString()))				// <accountID>
				{
					return rpcError(rpcINVALID_PARAMS);
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
				return rpcError(rpcINVALID_PARAMS);
			}
		}

		if (spPath.isEmpty())
		{
			return rpcError(rpcINVALID_PARAMS);
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
		return rpcError(rpcINVALID_PARAMS);
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
		return rpcError(rpcINVALID_PARAMS);
	}
	else
	{
		++iArg;
	}

	if (params.size() != iArg && !naDstAccountID.setAccountID(params[iArg++].asString()))		// <dest_account>
	{
		return rpcError(rpcDST_ACT_MALFORMED);
	}

	const unsigned int uDstIssuer	= params.size() == iArg + 3 ? iArg+2 : iArg-1;

	// <dest_amount> <dest_currency> <dest_issuerID>
	if (params.size() != iArg + 2 && params.size() != iArg + 3)
	{
		// Log(lsINFO) << "params.size(): " << params.size();

		return rpcError(rpcDST_AMT_MALFORMED);
	}
	else if (!saDstAmount.setFullValue(params[iArg].asString(), params[iArg+1].asString(), params[uDstIssuer].asString()))
	{
		// Log(lsINFO) << "  Amount: " << params[iArg].asString();
		// Log(lsINFO) << "Currency: " << params[iArg+1].asString();
		// Log(lsINFO) << "  Issuer: " << params[uDstIssuer].asString();

		return rpcError(rpcDST_AMT_MALFORMED);
	}

	AccountState::pointer	asDst	= mNetOps->getAccountState(uint256(0), naDstAccountID);
	STAmount				saFee	= theConfig.FEE_DEFAULT;

	RippleAddress			naVerifyGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
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

		return rpcError(rpcDST_ACT_MISSING);
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
Json::Value RPCHandler::doRippleLineSet(const Json::Value& params)
{
	RippleAddress	naSeed;
	RippleAddress	naSrcAccountID;
	RippleAddress	naDstAccountID;
	STAmount		saLimitAmount;
	bool			bQualityIn		= params.size() >= 6;
	bool			bQualityOut		= params.size() >= 7;
	uint32			uQualityIn		= 0;
	uint32			uQualityOut		= 0;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return rpcError(rpcDST_ACT_MALFORMED);
	}
	else if (!saLimitAmount.setFullValue(params[3u].asString(), params.size() >= 5 ? params[4u].asString() : "", params[2u].asString()))
	{
		return rpcError(rpcSRC_AMT_MALFORMED);
	}
	else if (bQualityIn && !parseQuality(params[5u].asString(), uQualityIn))
	{
		return rpcError(rpcQUALITY_MALFORMED);
	}
	else if (bQualityOut && !parseQuality(params[6u].asString(), uQualityOut))
	{
		return rpcError(rpcQUALITY_MALFORMED);
	}
	else
	{
		RippleAddress			naMasterGenerator;
		RippleAddress			naAccountPublic;
		RippleAddress			naAccountPrivate;
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
			saLimitAmount,
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
Json::Value RPCHandler::doRippleLinesGet(const Json::Value &params)
{
	//	uint256			uAccepted	= mNetOps->getClosedLedger();

	std::string		strIdent	= params[0u].asString();
	bool			bIndex;
	int				iIndex		= 2 == params.size() ? lexical_cast_s<int>(params[1u].asString()) : 0;

	RippleAddress	naAccount;

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
		ret	= rpcError(rpcACT_NOT_FOUND);
	}

	return ret;
}

// submit any transaction to the network
Json::Value RPCHandler::doSubmit(const Json::Value& params)
{
	// TODO
	return rpcError(rpcSRC_ACT_MALFORMED);
}

// send regular_seed paying_account account_id amount [currency] [issuer] [send_max] [send_currency] [send_issuer]
Json::Value RPCHandler::doSend(const Json::Value& params)
{
	RippleAddress	naSeed;
	RippleAddress	naSrcAccountID;
	RippleAddress	naDstAccountID;
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

	if (params.size() >= 8)
		sSrcCurrency	= params[7u].asString();

	if (params.size() >= 9)
		sSrcIssuer		= params[8u].asString();

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return rpcError(rpcDST_ACT_MALFORMED);
	}
	else if (!saDstAmount.setFullValue(params[3u].asString(), sDstCurrency, sDstIssuer))
	{
		return rpcError(rpcDST_AMT_MALFORMED);
	}
	else if (params.size() >= 7 && !saSrcAmountMax.setFullValue(params[6u].asString(), sSrcCurrency, sSrcIssuer))
	{
		return rpcError(rpcSRC_AMT_MALFORMED);
	}
	else
	{
		AccountState::pointer	asDst	= mNetOps->getAccountState(uint256(0), naDstAccountID);
		bool					bCreate	= !asDst;
		STAmount				saFee	= bCreate ? theConfig.FEE_ACCOUNT_CREATE : theConfig.FEE_DEFAULT;

		RippleAddress			naVerifyGenerator;
		RippleAddress			naAccountPublic;
		RippleAddress			naAccountPrivate;
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

			return rpcError(rpcINSUF_FUNDS);
		}
		else if (saDstAmount.isNative() && saSrcAmountMax < saDstAmount)
		{
			// Not enough native currency.

			Log(lsINFO) << "doSend: Insufficient funds: src=" << saSrcAmountMax.getText() << " dst=" << saDstAmount.getText();

			return rpcError(rpcINSUF_FUNDS);
		}
		// XXX Don't allow send to self of same currency.

		Transaction::pointer	trans;
		if (asDst) {
			// Destination exists, ordinary send.

			STPathSet	spsPaths;
			uint160		srcCurrencyID;

			if (!saSrcAmountMax.isNative() || !saDstAmount.isNative())
			{
				STAmount::currencyFromString(srcCurrencyID, sSrcCurrency);
				Pathfinder pf(naSrcAccountID, naDstAccountID, srcCurrencyID, saDstAmount);
				pf.findPaths(5, 1, spsPaths);
			}

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

Json::Value RPCHandler::doServerInfo(const Json::Value& params)
{
	Json::Value ret(Json::objectValue);

	ret["info"]	= theApp->getOPs().getServerInfo();

	return ret;
}

Json::Value RPCHandler::doTxHistory(const Json::Value& params)
{
	if (params.size() == 1)
	{
		unsigned int startIndex = params[0u].asInt();
		Json::Value	obj;
		Json::Value	txs;

		obj["index"]=startIndex;

		std::string sql =
			str(boost::format("SELECT * FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,20")
			% startIndex);

		{
			Database* db = theApp->getTxnDB()->getDB();
			ScopedLock dbLock = theApp->getTxnDB()->getDBLock();

			SQL_FOREACH(db, sql)
			{
				Transaction::pointer trans=Transaction::transactionFromSQL(db, false);
				if(trans) txs.append(trans->getJson(0));
			}
		}

		obj["txs"]=txs;

		return obj;
	}

	return rpcError(rpcSRC_ACT_MALFORMED);
}

Json::Value RPCHandler::doTx(const Json::Value& params)
{
	// tx <txID>
	// tx <account>

	std::string param1, param2;
	if (!extractString(param1, params, 0))
	{
		return rpcError(rpcINVALID_PARAMS);
	}

	if (Transaction::isHexTxID(param1))
	{ // transaction by ID
		Json::Value ret;
		uint256 txid(param1);

		Transaction::pointer txn = theApp->getMasterTransaction().fetch(txid, true);

		if (!txn) return rpcError(rpcTXN_NOT_FOUND);

		return txn->getJson(0);
	}

	return rpcError(rpcNOT_IMPL);
}

// ledger [id|current|lastclosed] [full]
Json::Value RPCHandler::doLedger(const Json::Value& params)
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
		return rpcError(rpcLGR_NOT_FOUND);

	bool full = extractString(param, params, 1) && (param == "full");
	Json::Value ret(Json::objectValue);
	ledger->addJson(ret, full ? LEDGER_JSON_FULL : 0);
	return ret;
}

// account_tx <account> <minledger> <maxledger>
// account_tx <account> <ledger>
Json::Value RPCHandler::doAccountTransactions(const Json::Value& params)
{
	std::string param;
	uint32 minLedger, maxLedger;

	if (!extractString(param, params, 0))
		return rpcError(rpcINVALID_PARAMS);

	RippleAddress account;
	if (!account.setAccountID(param))
		return rpcError(rpcACT_MALFORMED);

	if (!extractString(param, params, 1))
		return rpcError(rpcLGR_IDX_MALFORMED);

	minLedger = lexical_cast_s<uint32>(param);

	if ((params.size() == 3) && extractString(param, params, 2))
		maxLedger = lexical_cast_s<uint32>(param);
	else
		maxLedger = minLedger;

	if ((maxLedger < minLedger) || (maxLedger == 0))
	{
		std::cerr << "minL=" << minLedger << ", maxL=" << maxLedger << std::endl;

		return rpcError(rpcLGR_IDXS_INVALID);
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
			{
				ret["transactions"].append(it->second.GetHex());
			}
			else
			{
				txn->setLedger(it->first);
				ret["transactions"].append(txn->getJson(0));
			}

		}
		return ret;
#ifndef DEBUG
	}
	catch (...)
	{
		return rpcError(rpcINTERNAL);
	}
#endif
}

// unl_add <domain>|<node_public> [<comment>]
Json::Value RPCHandler::doUnlAdd(const Json::Value& params)
{
	std::string	strNode		= params[0u].asString();
	std::string strComment	= (params.size() == 2) ? params[1u].asString() : "";

	RippleAddress	naNodePublic;

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
Json::Value RPCHandler::doValidationCreate(const Json::Value& params) {
	RippleAddress	naSeed;
	Json::Value		obj(Json::objectValue);

	if (params.empty())
	{
		std::cerr << "Creating random validation seed." << std::endl;

		naSeed.setSeedRandom();					// Get a random seed.
	}
	else if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}

	obj["validation_public_key"]	= RippleAddress::createNodePublic(naSeed).humanNodePublic();
	obj["validation_seed"]			= naSeed.humanSeed();
	obj["validation_key"]			= naSeed.humanSeed1751();

	return obj;
}

// validation_seed [<pass_phrase>|<seed>|<seed_key>]
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCHandler::doValidationSeed(const Json::Value& params) {
	Json::Value obj(Json::objectValue);

	if (params.empty())
	{
		std::cerr << "Unset validation seed." << std::endl;

		theConfig.VALIDATION_SEED.clear();
		theConfig.VALIDATION_PUB.clear();
		theConfig.VALIDATION_PRIV.clear();
	}
	else if (!theConfig.VALIDATION_SEED.setSeedGeneric(params[0u].asString()))
	{
		theConfig.VALIDATION_PUB.clear();
		theConfig.VALIDATION_PRIV.clear();
		return rpcError(rpcBAD_SEED);
	}
	else
	{
		theConfig.VALIDATION_PUB = RippleAddress::createNodePublic(theConfig.VALIDATION_SEED);
		theConfig.VALIDATION_PRIV = RippleAddress::createNodePrivate(theConfig.VALIDATION_SEED);
		obj["validation_public_key"]	= theConfig.VALIDATION_PUB.humanNodePublic();
		obj["validation_seed"]			= theConfig.VALIDATION_SEED.humanSeed();
		obj["validation_key"]			= theConfig.VALIDATION_SEED.humanSeed1751();
	}

	return obj;
}

Json::Value RPCHandler::accounts(const uint256& uLedger, const RippleAddress& naMasterGenerator)
{
	Json::Value jsonAccounts(Json::arrayValue);

	// YYY Don't want to leak to thin server that these accounts are related.
	// YYY Would be best to alternate requests to servers and to cache results.
	unsigned int	uIndex	= 0;

	do {
		RippleAddress		naAccount;

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
Json::Value RPCHandler::doWalletAccounts(const Json::Value& params)
{
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}

	// Try the seed as a master seed.
	RippleAddress	naMasterGenerator	= RippleAddress::createGeneratorPublic(naSeed);

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
Json::Value RPCHandler::doWalletAdd(const Json::Value& params)
{
	RippleAddress	naMasterSeed;
	RippleAddress	naRegularSeed;
	RippleAddress	naSrcAccountID;
	STAmount		saAmount;
	std::string		sDstCurrency;

	if (!naRegularSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naMasterSeed.setSeedGeneric(params[2u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (params.size() >= 4 && !saAmount.setFullValue(params[3u].asString(), sDstCurrency))
	{
		return rpcError(rpcDST_AMT_MALFORMED);
	}
	else
	{
		RippleAddress			naMasterGenerator	= RippleAddress::createGeneratorPublic(naMasterSeed);
		RippleAddress			naRegularGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);

		RippleAddress			naAccountPublic;
		RippleAddress			naAccountPrivate;
		AccountState::pointer	asSrc;
		STAmount				saSrcBalance;
		Json::Value				obj			= authorize(uint256(0), naRegularSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
			saSrcBalance, theConfig.FEE_ACCOUNT_CREATE, asSrc, naMasterGenerator);

		if (!obj.empty())
			return obj;

		if (saSrcBalance < saAmount)
		{
			return rpcError(rpcINSUF_FUNDS);
		}
		else
		{
			RippleAddress				naNewAccountPublic;
			RippleAddress				naNewAccountPrivate;
			RippleAddress				naAuthKeyID;
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
Json::Value RPCHandler::doWalletClaim(const Json::Value& params)
{
	RippleAddress	naMasterSeed;
	RippleAddress	naRegularSeed;

	if (!naMasterSeed.setSeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return rpcError(rpcBAD_SEED);
	}
	else if (!naRegularSeed.setSeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return rpcError(rpcBAD_SEED);
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

		RippleAddress	naMasterGenerator	= RippleAddress::createGeneratorPublic(naMasterSeed);
		RippleAddress	naRegularGenerator	= RippleAddress::createGeneratorPublic(naRegularSeed);
		RippleAddress	naRegular0Public;
		RippleAddress	naRegular0Private;

		RippleAddress	naAccountPublic;
		RippleAddress	naAccountPrivate;

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
Json::Value RPCHandler::doWalletCreate(const Json::Value& params)
{
	RippleAddress	naSrcAccountID;
	RippleAddress	naDstAccountID;
	RippleAddress	naSeed;

	if (!naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else if (!naSrcAccountID.setAccountID(params[1u].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}
	else if (!naDstAccountID.setAccountID(params[2u].asString()))
	{
		return rpcError(rpcDST_ACT_MALFORMED);
	}
	else if (mNetOps->getAccountState(uint256(0), naDstAccountID))
	{
		return rpcError(rpcACT_EXISTS);
	}

	// Trying to build:
	//	 peer_wallet_create <paying_account> <paying_signature> <account_id> [<initial_funds>] [<annotation>]

	RippleAddress			naMasterGenerator;
	RippleAddress			naAccountPublic;
	RippleAddress			naAccountPrivate;
	AccountState::pointer	asSrc;
	STAmount				saSrcBalance;
	Json::Value				obj				= authorize(uint256(0), naSeed, naSrcAccountID, naAccountPublic, naAccountPrivate,
		saSrcBalance, theConfig.FEE_ACCOUNT_CREATE, asSrc, naMasterGenerator);

	if (!obj.empty())
		return obj;

	STAmount				saInitialFunds	= (params.size() < 4) ? 0 : lexical_cast_s<uint64>(params[3u].asString());

	if (saSrcBalance < saInitialFunds)
		return rpcError(rpcINSUF_FUNDS);

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


Json::Value RPCHandler::doLogRotate(const Json::Value& params) 
{
	return Log::rotateLog();
}

Json::Value RPCHandler::doCommand(const std::string& command, Json::Value& params,int role)
{
	cLog(lsTRACE) << "RPC:" << command;

	static struct {
		const char* pCommand;
		doFuncPtr	dfpFunc;
		int			iMinParams;
		int			iMaxParams;
		bool		mAdminRequired;
		unsigned int	iOptions;
	} commandsA[] = {
		{	"accept_ledger",		&RPCHandler::doAcceptLedger,		0,	0, true					},
		{	"account_domain_set",	&RPCHandler::doAccountDomainSet,	2,  3, false,	optCurrent	},
		{	"account_email_set",	&RPCHandler::doAccountEmailSet,		2,  3, false,	optCurrent	},
		{	"account_info",			&RPCHandler::doAccountInfo,			1,  2, false,	optCurrent	},
		{	"account_message_set",	&RPCHandler::doAccountMessageSet,	3,  3, false,	optCurrent	},
		{	"account_publish_set",	&RPCHandler::doAccountPublishSet,	4,  4, false,	optCurrent	},
		{	"account_rate_set",		&RPCHandler::doAccountRateSet,		3,  3, false,	optCurrent	},
		{	"account_tx",			&RPCHandler::doAccountTransactions,	2,  3, false,	optNetwork	},
		{	"account_wallet_set",	&RPCHandler::doAccountWalletSet,	2,  3, false,	optCurrent	},
		{	"connect",				&RPCHandler::doConnect,				1,  2, true					},
		{	"data_delete",			&RPCHandler::doDataDelete,			1,  1, true					},
		{	"data_fetch",			&RPCHandler::doDataFetch,			1,  1, true					},
		{	"data_store",			&RPCHandler::doDataStore,			2,  2, true					},
		{	"get_counts",			&RPCHandler::doGetCounts,			0,	1, true					},
		{	"ledger",				&RPCHandler::doLedger,				0,  2, false,	optNetwork	},
		{	"log_level",			&RPCHandler::doLogLevel,			0,  2, true					},
		{	"logrotate",			&RPCHandler::doLogRotate,			0,  0, true					},
		{	"nickname_info",		&RPCHandler::doNicknameInfo,		1,  1, false,	optCurrent	},
		{	"nickname_set",			&RPCHandler::doNicknameSet,			2,  3, false,	optCurrent	},
		{	"offer_create",			&RPCHandler::doOfferCreate,			9, 10, false,	optCurrent	},
		{	"offer_cancel",			&RPCHandler::doOfferCancel,			3,  3, false,	optCurrent	},
		{	"owner_info",			&RPCHandler::doOwnerInfo,			1,  2, false,	optCurrent	},
		{	"password_fund",		&RPCHandler::doPasswordFund,		2,  3, false,	optCurrent	},
		{	"password_set",			&RPCHandler::doPasswordSet,			2,  3, false,	optNetwork	},
		{	"peers",				&RPCHandler::doPeers,				0,  0, true					},
		{	"profile",				&RPCHandler::doProfile,				1,  9, false,	optCurrent	},
		{	"ripple",				&RPCHandler::doRipple,				9, -1, false,	optCurrent|optClosed },
		{	"ripple_lines_get",		&RPCHandler::doRippleLinesGet,		1,  2, false,	optCurrent	},
		{	"ripple_line_set",		&RPCHandler::doRippleLineSet,		4,  7, false,	optCurrent	},
		{	"send",					&RPCHandler::doSend,				3,  9, false,	optCurrent	},
		{	"server_info",			&RPCHandler::doServerInfo,			0,  0, true					},
		{	"stop",					&RPCHandler::doStop,				0,  0, true					},
		{	"tx",					&RPCHandler::doTx,					1,  1, true					},
		{	"tx_history",			&RPCHandler::doTxHistory,			1,  1, false,				},

		{	"unl_add",				&RPCHandler::doUnlAdd,				1,  2, true					},
		{	"unl_delete",			&RPCHandler::doUnlDelete,			1,  1, true					},
		{	"unl_list",				&RPCHandler::doUnlList,				0,  0, true					},
		{	"unl_load",				&RPCHandler::doUnlLoad,				0,  0, true					},
		{	"unl_network",			&RPCHandler::doUnlNetwork,			0,  0, true					},
		{	"unl_reset",			&RPCHandler::doUnlReset,			0,  0, true					},
		{	"unl_score",			&RPCHandler::doUnlScore,			0,  0, true					},

		{	"validation_create",	&RPCHandler::doValidationCreate,	0,  1, false				},
		{	"validation_seed",		&RPCHandler::doValidationSeed,		0,  1, false				},

		{	"wallet_accounts",		&RPCHandler::doWalletAccounts,		1,  1, false,	optCurrent	},
		{	"wallet_add",			&RPCHandler::doWalletAdd,			3,  5, false,	optCurrent	},
		{	"wallet_claim",			&RPCHandler::doWalletClaim,			2,  4, false,	optNetwork	},
		{	"wallet_create",		&RPCHandler::doWalletCreate,		3,  4, false,	optCurrent	},
		{	"wallet_propose",		&RPCHandler::doWalletPropose,		0,  1, false,				},
		{	"wallet_seed",			&RPCHandler::doWalletSeed,			0,  1, false,				},

		{	"login",				&RPCHandler::doLogin,				2,  2, true					},
	};

	int		i = NUMBER(commandsA);

	while (i-- && command != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return rpcError(rpcUNKNOWN_COMMAND);
	}
	else if (commandsA[i].mAdminRequired && role != ADMIN)
	{
		return rpcError(rpcNO_PERMISSION);
	}
	else if (params.size() < commandsA[i].iMinParams
		|| (commandsA[i].iMaxParams >= 0 && params.size() > commandsA[i].iMaxParams))
	{
		return rpcError(rpcINVALID_PARAMS);
	}
	else if ((commandsA[i].iOptions & optNetwork) && !mNetOps->available())
	{
		return rpcError(rpcNO_NETWORK);
	}
	// XXX Should verify we have a current ledger.
	else if ((commandsA[i].iOptions & optCurrent) && false)
	{
		return rpcError(rpcNO_CURRENT);
	}
	else if ((commandsA[i].iOptions & optClosed) && mNetOps->getClosedLedger().isZero())
	{
		return rpcError(rpcNO_CLOSED);
	}
	else
	{
		try {
			return (this->*(commandsA[i].dfpFunc))(params);
		}
		catch (std::exception& e)
		{
			cLog(lsINFO) << "Caught throw: " << e.what();

			return rpcError(rpcINTERNAL);
		}
	}
}




// wallet_propose [<passphrase>]
// <passphrase> is only for testing. Master seeds should only be generated randomly.
Json::Value RPCHandler::doWalletPropose(const Json::Value& params)
{
	RippleAddress	naSeed;
	RippleAddress	naAccount;

	if (params.empty())
	{
		naSeed.setSeedRandom();
	}
	else
	{
		naSeed	= RippleAddress::createSeedGeneric(params[0u].asString());
	}

	RippleAddress	naGenerator	= RippleAddress::createGeneratorPublic(naSeed);
	naAccount.setAccountPublic(naGenerator, 0);

	Json::Value obj(Json::objectValue);

	obj["master_seed"]		= naSeed.humanSeed();
	//obj["master_key"]		= naSeed.humanSeed1751();
	obj["account_id"]		= naAccount.humanAccountID();

	return obj;
}

// wallet_seed [<seed>|<passphrase>|<passkey>]
Json::Value RPCHandler::doWalletSeed(const Json::Value& params)
{
	RippleAddress	naSeed;

	if (params.size()
		&& !naSeed.setSeedGeneric(params[0u].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	else
	{
		RippleAddress	naAccount;

		if (!params.size())
		{
			naSeed.setSeedRandom();
		}

		RippleAddress	naGenerator	= RippleAddress::createGeneratorPublic(naSeed);

		naAccount.setAccountPublic(naGenerator, 0);

		Json::Value obj(Json::objectValue);

		obj["seed"]		= naSeed.humanSeed();
		obj["key"]		= naSeed.humanSeed1751();

		return obj;
	}
}


// TODO: for now this simply checks if this is the admin account
// TODO: need to prevent them hammering this over and over
// TODO: maybe a better way is only allow admin from local host
Json::Value RPCHandler::doLogin(const Json::Value& params)
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

Json::Value RPCHandler::doGetCounts(const Json::Value& params)
{
	int minCount = 1;
	if (params.size() > 0)
		minCount = params[0u].asInt();

	std::vector<InstanceType::InstanceCount> count = InstanceType::getInstanceCounts(minCount);

	Json::Value ret(Json::objectValue);
	BOOST_FOREACH(InstanceType::InstanceCount& it, count)
		ret[it.first] = it.second;
	return ret;
}

Json::Value RPCHandler::doLogLevel(const Json::Value& params)
{
	if (params.size() == 0)
	{ // get log severities
		Json::Value ret = Json::objectValue;

		ret["base"] = Log::severityToString(Log::getMinSeverity());

		std::vector< std::pair<std::string, std::string> > logTable = LogPartition::getSeverities();
		typedef std::pair<std::string, std::string> stringPair;
		BOOST_FOREACH(const stringPair& it, logTable)
			ret[it.first] = it.second;
		return ret;
	}

	if (params.size() == 1)
	{ // set base log severity
		LogSeverity sv = Log::stringToSeverity(params[0u].asString());
		if (sv == lsINVALID)
			return rpcError(rpcINVALID_PARAMS);
		Log::setMinSeverity(sv,true);
		return rpcError(rpcSUCCESS);
	}

	if (params.size() == 2)
	{ // set partition severity
		LogSeverity sv = Log::stringToSeverity(params[1u].asString());
		if (sv == lsINVALID)
			return rpcError(rpcINVALID_PARAMS);
		if (params[2u].asString() == "base")
			Log::setMinSeverity(sv,false);
		else if (!LogPartition::setSeverity(params[0u].asString(), sv))
			return rpcError(rpcINVALID_PARAMS);
		return rpcError(rpcSUCCESS);
	}

	assert(false);
	return rpcError(rpcINVALID_PARAMS);
}



// Populate the UNL from ripple.com's validators.txt file.
Json::Value RPCHandler::doUnlNetwork(const Json::Value& params)
{
	theApp->getUNL().nodeNetwork();

	return "fetching";
}

// unl_reset
Json::Value RPCHandler::doUnlReset(const Json::Value& params)
{
	theApp->getUNL().nodeReset();

	return "removing nodes";
}

// unl_score
Json::Value RPCHandler::doUnlScore(const Json::Value& params)
{
	theApp->getUNL().nodeScore();

	return "scoring requested";
}

Json::Value RPCHandler::doStop(const Json::Value& params)
{
	theApp->stop();

	return SYSTEM_NAME " server stopping";
}

// unl_delete <domain>|<public_key>
Json::Value RPCHandler::doUnlDelete(const Json::Value& params)
{
	std::string	strNode		= params[0u].asString();

	RippleAddress	naNodePublic;

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

Json::Value RPCHandler::doUnlList(const Json::Value& params)
{
	Json::Value obj(Json::objectValue);

	obj["unl"]=theApp->getUNL().getUnlJson();

	return obj;
}

// Populate the UNL from a local validators.txt file.
Json::Value RPCHandler::doUnlLoad(const Json::Value& params)
{
	if (theConfig.UNL_DEFAULT.empty() || !theApp->getUNL().nodeLoad(theConfig.UNL_DEFAULT))
	{
		return rpcError(rpcLOAD_FAILED);
	}

	return "loading";
}

// vim:ts=4
