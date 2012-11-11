#include "Log.h"
#include "NetworkOPs.h"
#include "RPCHandler.h"
#include "Application.h"
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
		{ rpcNO_EVENTS,				"noEvents",			"Current transport does not support events."			},
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
	mInfoSub=NULL;
}

RPCHandler::RPCHandler(NetworkOPs* netOps, InfoSub* infoSub)
{
	mNetOps=netOps;
	mInfoSub=infoSub;
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
		cLog(lsINFO) << "authorize: Insufficient funds for fees: fee=" << saFee.getText() << " balance=" << saSrcBalance.getText();

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

	uint256			uAccepted		= mNetOps->getClosedLedgerHash();
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

	uint256			uAccepted	= mNetOps->getClosedLedgerHash();
	Json::Value		jAccepted	= accountFromString(uAccepted, naAccount, bIndex, strIdent, iIndex);

	ret["accepted"]	= jAccepted.empty() ? mNetOps->getOwnerInfo(uAccepted, naAccount) : jAccepted;

	Json::Value		jCurrent	= accountFromString(uint256(0), naAccount, bIndex, strIdent, iIndex);

	ret["current"]	= jCurrent.empty() ? mNetOps->getOwnerInfo(uint256(0), naAccount) : jCurrent;

	return ret;
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
			tpOfferA	= mNetOps->submitTransactionSync(tpOfferA);
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

// ripple_lines_get <account>|<nickname>|<account_public_key> [<index>]
Json::Value RPCHandler::doRippleLinesGet(const Json::Value &params)
{
	//	uint256			uAccepted	= mNetOps->getClosedLedgerHash();

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
// submit private_key json
Json::Value RPCHandler::doSubmit(const Json::Value& params)
{
	Json::Value		txJSON;
	Json::Reader	reader;

	if (reader.parse(params[1u].asString(), txJSON))
	{
		Json::Value	jvRequest;

		jvRequest["secret"]		= params[0u].asString();
		jvRequest["tx_json"]	= txJSON;

		return handleJSONSubmit(jvRequest);
	}

	return rpcError(rpcINVALID_PARAMS);
}

Json::Value RPCHandler::doSubmitJson(const Json::Value& jvRequest)
{
	return handleJSONSubmit(jvRequest);
}


Json::Value RPCHandler::handleJSONSubmit(const Json::Value& jvRequest)
{
	Json::Value		jvResult;
	RippleAddress	naSeed;
	RippleAddress	srcAddress;
	Json::Value		txJSON		= jvRequest["tx_json"];

	if (!naSeed.setSeedGeneric(jvRequest["secret"].asString()))
	{
		return rpcError(rpcBAD_SEED);
	}
	if (!txJSON.isMember("Account"))
	{
		return rpcError(rpcSRC_ACT_MISSING);
	}
	if (!srcAddress.setAccountID(txJSON["Account"].asString()))
	{
		return rpcError(rpcSRC_ACT_MALFORMED);
	}

	AccountState::pointer asSrc	= mNetOps->getAccountState(uint256(0), srcAddress);

	if( txJSON["type"]=="Payment")
	{
		txJSON["TransactionType"]=0;

		RippleAddress dstAccountID;

		if (!txJSON.isMember("Destination"))
		{
			return rpcError(rpcDST_ACT_MISSING);
		}
		if (!dstAccountID.setAccountID(txJSON["Destination"].asString()))
		{
			return rpcError(rpcDST_ACT_MALFORMED);
		}

		if(!txJSON.isMember("Fee"))
		{
			if(mNetOps->getAccountState(uint256(0), dstAccountID))
				txJSON["Fee"]=(int)theConfig.FEE_DEFAULT;
			else txJSON["Fee"]=(int)theConfig.FEE_ACCOUNT_CREATE;
		}

		if(!txJSON.isMember("Paths") && (!jvRequest.isMember("build_path") || jvRequest["build_path"].asBool()))
		{
			if(txJSON["Amount"].isObject() || txJSON.isMember("SendMax") )
			{  // we need a ripple path
				STPathSet	spsPaths;
				uint160		srcCurrencyID;
				if(txJSON.isMember("SendMax") && txJSON["SendMax"].isMember("currency"))
				{
					STAmount::currencyFromString(srcCurrencyID, txJSON["SendMax"]["currency"].asString());
				}
				else
				{
					srcCurrencyID	= CURRENCY_XRP;
				}

				STAmount dstAmount;
				if(txJSON["Amount"].isObject())
				{
					std::string issuerStr;
					if( txJSON["Amount"].isMember("issuer")) issuerStr=txJSON["Amount"]["issuer"].asString();
					if( !txJSON["Amount"].isMember("value") || !txJSON["Amount"].isMember("currency")) return rpcError(rpcDST_AMT_MALFORMED);
					if (!dstAmount.setFullValue(txJSON["Amount"]["value"].asString(), txJSON["Amount"]["currency"].asString(), issuerStr))
					{
						return rpcError(rpcDST_AMT_MALFORMED);
					}
				}else if (!dstAmount.setFullValue(txJSON["Amount"].asString()))
				{
					return rpcError(rpcDST_AMT_MALFORMED);
				}

				Pathfinder pf(srcAddress, dstAccountID, srcCurrencyID, dstAmount);
				pf.findPaths(5, 1, spsPaths);
				txJSON["Paths"]=spsPaths.getJson(0);
				if(txJSON.isMember("Flags")) txJSON["Flags"]=txJSON["Flags"].asUInt() | 2;
				else txJSON["Flags"]=2;
			}
		}

	}else if( txJSON["type"]=="OfferCreate" )
	{
		txJSON["TransactionType"]=7;
		if(!txJSON.isMember("Fee")) txJSON["Fee"]=(int)theConfig.FEE_DEFAULT;
	}else if( txJSON["type"]=="TrustSet")
	{
		txJSON["TransactionType"]=20;
		if(!txJSON.isMember("Fee")) txJSON["Fee"]=(int)theConfig.FEE_DEFAULT;
	}else if( txJSON["type"]=="OfferCancel")
	{
		txJSON["TransactionType"]=8;
		if(!txJSON.isMember("Fee")) txJSON["Fee"]=(int)theConfig.FEE_DEFAULT;
	}

	txJSON.removeMember("type");

	if(!txJSON.isMember("Sequence")) txJSON["Sequence"]=asSrc->getSeq();
	if(!txJSON.isMember("Flags")) txJSON["Flags"]=0;

	Ledger::pointer	lpCurrent		= mNetOps->getCurrentLedger();
	SLE::pointer	sleAccountRoot	= mNetOps->getSLE(lpCurrent, Ledger::getAccountRootIndex(srcAddress.getAccountID()));

	if (!sleAccountRoot)
	{
		// XXX Ignore transactions for accounts not created.
		return rpcError(rpcSRC_ACT_MISSING);
	}

	bool			bHaveAuthKey	= false;
	RippleAddress	naAuthorizedPublic;


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

		Log(lsWARNING) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID() << " : " << srcAddress.humanAccountID();

		bFound	= srcAddress.getAccountID() == naMasterAccountPublic.getAccountID();
		if (!bFound)
			++iIndex;
	}

	if (!bFound)
	{
		return rpcError(rpcSRC_ACT_MISSING);
	}

	// Use the generator to determine the associated public and private keys.
	RippleAddress	naGenerator			= RippleAddress::createGeneratorPublic(naSecret);
	RippleAddress	naAccountPublic		= RippleAddress::createAccountPublic(naGenerator, iIndex);
	RippleAddress	naAccountPrivate	= RippleAddress::createAccountPrivate(naGenerator, naSecret, iIndex);

	if (bHaveAuthKey
		// The generated pair must match authorized...
		&& naAuthorizedPublic.getAccountID() != naAccountPublic.getAccountID()
		// ... or the master key must have been used.
		&& srcAddress.getAccountID() != naAccountPublic.getAccountID())
	{
		// std::cerr << "iIndex: " << iIndex << std::endl;
		// std::cerr << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID()) << std::endl;
		// std::cerr << "naAccountPublic: " << strHex(naAccountPublic.getAccountID()) << std::endl;

		return rpcError(rpcSRC_ACT_MISSING);
	}

	std::auto_ptr<STObject>	sopTrans;

	try
	{
		sopTrans = STObject::parseJson(txJSON);
	}
	catch (std::exception& e)
	{
		jvResult["error"]			= "malformedTransaction";
		jvResult["error_exception"]	= e.what();
		return jvResult;
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
		return jvResult;
	}

	// FIXME: Transactions should not be signed in this code path
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
		return jvResult;
	}

	try
	{
		tpTrans	= mNetOps->submitTransactionSync(tpTrans); // FIXME: Should use asynch interface

		if (!tpTrans) {
			jvResult["error"]			= "invalidTransaction";
			jvResult["error_exception"]	= "Unable to sterilize transaction.";
			return jvResult;
		}
	}
	catch (std::exception& e)
	{
		jvResult["error"]			= "internalSubmit";
		jvResult["error_exception"]	= e.what();
		return jvResult;
	}

	try
	{
		jvResult["tx_json"]		= tpTrans->getJson(0);

		if (temUNCERTAIN != tpTrans->getResult())
		{
			std::string	sToken;
			std::string	sHuman;

			transResultInfo(tpTrans->getResult(), sToken, sHuman);

			jvResult["engine_result"]			= sToken;
			jvResult["engine_result_code"]		= tpTrans->getResult();
			jvResult["engine_result_message"]	= sHuman;
		}
		return jvResult;
	}
	catch (std::exception& e)
	{
		jvResult["error"]			= "internalJson";
		jvResult["error_exception"]	= e.what();
		return jvResult;
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

Json::Value RPCHandler::doLedgerClosed(const Json::Value& params)
{
	Json::Value jvResult;
	uint256	uLedger	= mNetOps->getClosedLedgerHash();

	jvResult["ledger_index"]		= mNetOps->getLedgerID(uLedger);
	jvResult["ledger_hash"]			= uLedger.ToString();
	//jvResult["ledger_time"]		= uLedger.
	return jvResult;
}

Json::Value RPCHandler::doLedgerCurrent(const Json::Value& params)
{
	Json::Value jvResult;
	jvResult["ledger_current_index"]	= mNetOps->getCurrentLedgerID();
	return jvResult;
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

Json::Value RPCHandler::doLogRotate(const Json::Value& params)
{
	return Log::rotateLog();
}

Json::Value RPCHandler::doCommand(const std::string& command, Json::Value& params, int role)
{
	cLog(lsTRACE) << "RPC:" << command;
	cLog(lsTRACE) << "RPC params:" << params;

	static struct {
		const char*		pCommand;
		doFuncPtr		dfpFunc;
		int				iMinParams;
		int				iMaxParams;
		bool			mAdminRequired;
		bool			mEvented;
		unsigned int	iOptions;
	} commandsA[] = {
		// Request-response methods
		{	"accept_ledger",		&RPCHandler::doAcceptLedger,		0,	0, true					},
		{	"account_info",			&RPCHandler::doAccountInfo,			1,  2, false,	false,	optCurrent	},
		{	"account_tx",			&RPCHandler::doAccountTransactions,	2,  3, false,	false,	optNetwork	},
		{	"connect",				&RPCHandler::doConnect,				1,  2, true							},
		{	"data_delete",			&RPCHandler::doDataDelete,			1,  1, true							},
		{	"data_fetch",			&RPCHandler::doDataFetch,			1,  1, true							},
		{	"data_store",			&RPCHandler::doDataStore,			2,  2, true							},
		{	"get_counts",			&RPCHandler::doGetCounts,			0,	1, true							},
		{	"ledger",				&RPCHandler::doLedger,				0,  2, false,	false,	optNetwork	},
		{	"ledger_accept",		&RPCHandler::doLedgerAccept,		0,  0, true,	false,	optCurrent	},
		{	"ledger_closed",		&RPCHandler::doLedgerClosed,		0,  0, false,	false,	optClosed	},
		{	"ledger_current",		&RPCHandler::doLedgerCurrent,		0,  0, false,	false,	optCurrent	},
		{	"ledger_entry",			&RPCHandler::doLedgerEntry,			-1,  -1, false,	false,	optCurrent	},
		{	"log_level",			&RPCHandler::doLogLevel,			0,  2, true							},
		{	"logrotate",			&RPCHandler::doLogRotate,			0,  0, true							},
		{	"nickname_info",		&RPCHandler::doNicknameInfo,		1,  1, false,	false,	optCurrent	},
		{	"owner_info",			&RPCHandler::doOwnerInfo,			1,  2, false,	false,	optCurrent	},
		{	"peers",				&RPCHandler::doPeers,				0,  0, true					},
		{	"profile",				&RPCHandler::doProfile,				1,  9, false,	false,	optCurrent	},
		{	"ripple_lines_get",		&RPCHandler::doRippleLinesGet,		1,  2, false,	false,	optCurrent	},
		{	"submit",				&RPCHandler::doSubmit,				2,  2, false,	false,	optCurrent	},
		{	"submit_json",			&RPCHandler::doSubmitJson,			-1,  -1, false,	false,	optCurrent	},
		{	"server_info",			&RPCHandler::doServerInfo,			0,  0, true							},
		{	"stop",					&RPCHandler::doStop,				0,  0, true							},
		{	"transaction_entry",	&RPCHandler::doTransactionEntry,	-1,  -1, false,	false,	optCurrent	},
		{	"tx",					&RPCHandler::doTx,					1,  1, true							},
		{	"tx_history",			&RPCHandler::doTxHistory,			1,  1, false,						},

		{	"unl_add",				&RPCHandler::doUnlAdd,				1,  2, true							},
		{	"unl_delete",			&RPCHandler::doUnlDelete,			1,  1, true							},
		{	"unl_list",				&RPCHandler::doUnlList,				0,  0, true							},
		{	"unl_load",				&RPCHandler::doUnlLoad,				0,  0, true							},
		{	"unl_network",			&RPCHandler::doUnlNetwork,			0,  0, true							},
		{	"unl_reset",			&RPCHandler::doUnlReset,			0,  0, true							},
		{	"unl_score",			&RPCHandler::doUnlScore,			0,  0, true							},

		{	"validation_create",	&RPCHandler::doValidationCreate,	0,  1, false						},
		{	"validation_seed",		&RPCHandler::doValidationSeed,		0,  1, false						},

		{	"wallet_accounts",		&RPCHandler::doWalletAccounts,		1,  1, false,	false,	optCurrent	},
		{	"wallet_propose",		&RPCHandler::doWalletPropose,		0,  1, false,						},
		{	"wallet_seed",			&RPCHandler::doWalletSeed,			0,  1, false,						},

		{	"login",				&RPCHandler::doLogin,				2,  2, true							},

		// Evented methods
		{	"subscribe",			&RPCHandler::doSubscribe,			-1,	-1,	false,	true				},
		{	"unsubscribe",			&RPCHandler::doUnsubscribe,			-1,	-1,	false,	true				},	};

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
	else if (commandsA[i].mEvented && mInfoSub == NULL)
	{
		return rpcError(rpcNO_EVENTS);
	}
	else if (commandsA[i].iMinParams >= 0
		? commandsA[i].iMaxParams
			? (params.size() < commandsA[i].iMinParams
				|| (commandsA[i].iMaxParams >= 0 && params.size() > commandsA[i].iMaxParams))
			: false
		: params.isArray())
	{
		return rpcError(rpcINVALID_PARAMS);
	}
	else if ((commandsA[i].iOptions & optNetwork) && !mNetOps->available())
	{
		return rpcError(rpcNO_NETWORK);
	}
	// XXX Should verify we have a current ledger.

	boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());
	if ((commandsA[i].iOptions & optCurrent) && false)
	{
		return rpcError(rpcNO_CURRENT);
	}
	else if ((commandsA[i].iOptions & optClosed) && !mNetOps->getClosedLedger())
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
	int minCount = 10;
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

Json::Value RPCHandler::doLedgerAccept(const Json::Value& )
{
	Json::Value jvResult;

	if (!theConfig.RUN_STANDALONE)
	{
		jvResult["error"]	= "notStandAlone";
	}
	else
	{
		mNetOps->acceptLedger();

		jvResult["ledger_current_index"]	= mNetOps->getCurrentLedgerID();
	}

	return jvResult;
}

Json::Value RPCHandler::doTransactionEntry(const Json::Value& jvRequest)
{
	Json::Value jvResult;

	if (!jvRequest.isMember("tx_hash"))
	{
		jvResult["error"]	= "fieldNotFoundTransaction";
	}
	if (!jvRequest.isMember("ledger_hash"))
	{
		jvResult["error"]	= "notYetImplemented";	// XXX We don't support any transaction yet.
	}
	else
	{
		uint256						uTransID;
		// XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
		uTransID.SetHex(jvRequest["tx_hash"].asString());

		uint256						uLedgerID;
		// XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
		uLedgerID.SetHex(jvRequest["ledger_hash"].asString());

		Ledger::pointer				lpLedger	= theApp->getMasterLedger().getLedgerByHash(uLedgerID);

		if (!lpLedger) {
			jvResult["error"]	= "ledgerNotFound";
		}
		else
		{
			Transaction::pointer		tpTrans;
			TransactionMetaSet::pointer	tmTrans;

			if (!lpLedger->getTransaction(uTransID, tpTrans, tmTrans))
			{
				jvResult["error"]	= "transactionNotFound";
			}
			else
			{
				jvResult["tx_json"]		= tpTrans->getJson(0);
				jvResult["metadata"]	= tmTrans->getJson(0);
				// 'accounts'
				// 'engine_...'
				// 'ledger_...'
			}
		}
	}

	return jvResult;
}

Json::Value RPCHandler::doLedgerEntry(const Json::Value& jvRequest)
{
	Json::Value jvResult;

	uint256	uLedger			= jvRequest.isMember("ledger_hash") ? uint256(jvRequest["ledger_hash"].asString()) : 0;
	uint32	uLedgerIndex	= jvRequest.isMember("ledger_index") && jvRequest["ledger_index"].isNumeric() ? jvRequest["ledger_index"].asUInt() : 0;

	Ledger::pointer	 lpLedger;

	if (!!uLedger)
	{
		// Ledger directly specified.
		lpLedger	= mNetOps->getLedgerByHash(uLedger);

		if (!lpLedger)
		{
			jvResult["error"]	= "ledgerNotFound";
			return jvResult;
		}

		uLedgerIndex	= lpLedger->getLedgerSeq();	// Set the current index, override if needed.
	}
	else if (!!uLedgerIndex)
	{
		lpLedger		= mNetOps->getLedgerBySeq(uLedgerIndex);

		if (!lpLedger)
		{
			jvResult["error"]	= "ledgerNotFound";	// ledger_index from future?
			return jvResult;
		}
	}
	else
	{
		// Default to current ledger.
		lpLedger		= mNetOps->getCurrentLedger();
		uLedgerIndex	= lpLedger->getLedgerSeq();	// Set the current index.
	}

	if (lpLedger->isClosed())
	{
		if (!!uLedger)
			jvResult["ledger_hash"]			= uLedger.ToString();

		jvResult["ledger_index"]		= uLedgerIndex;
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
		SLE::pointer	sleNode	= mNetOps->getSLE(lpLedger, uNodeIndex);

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

	return jvResult;
}


boost::unordered_set<RippleAddress> RPCHandler::parseAccountIds(const Json::Value& jvArray)
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

/*
server : Sends a message anytime the server status changes such as network connectivity.
ledger : Sends a message at every ledger close.
transactions : Sends a message for every transaction that makes it into a ledger.
rt_transactions
accounts
rt_accounts
*/
Json::Value RPCHandler::doSubscribe(const Json::Value& jvRequest)
{
	Json::Value jvResult(Json::objectValue);

	if (jvRequest.isMember("streams"))
	{
		for (Json::Value::iterator it = jvRequest["streams"].begin(); it != jvRequest["streams"].end(); it++)
		{
			if ((*it).isString())
			{
				std::string streamName=(*it).asString();

				if(streamName=="server")
				{
					mNetOps->subServer(mInfoSub, jvResult);
				}else if(streamName=="ledger")
				{
					mNetOps->subLedger(mInfoSub, jvResult);
				}else if(streamName=="transactions")
				{
					mNetOps->subTransactions(mInfoSub);
				}else if(streamName=="rt_transactions")
				{
					mNetOps->subRTTransactions(mInfoSub);
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
			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mInfoSub->insertSubAccountInfo(naAccountID);
			}

			mNetOps->subAccount(mInfoSub, usnaAccoundIds, true);
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
			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mInfoSub->insertSubAccountInfo(naAccountID);
			}

			mNetOps->subAccount(mInfoSub, usnaAccoundIds, false);
		}
	}

	return jvResult;
}

Json::Value RPCHandler::doUnsubscribe(const Json::Value& jvRequest)
{
	Json::Value jvResult(Json::objectValue);

	if (jvRequest.isMember("streams"))
	{
		for (Json::Value::iterator it = jvRequest["streams"].begin(); it != jvRequest["streams"].end(); it++)
		{
			if ((*it).isString() )
			{
				std::string streamName=(*it).asString();

				if(streamName=="server")
				{
					mNetOps->unsubServer(mInfoSub);
				}else if(streamName=="ledger")
				{
					mNetOps->unsubLedger(mInfoSub);
				}else if(streamName=="transactions")
				{
					mNetOps->unsubTransactions(mInfoSub);
				}else if(streamName=="rt_transactions")
				{
					mNetOps->unsubRTTransactions(mInfoSub);
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
			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mInfoSub->insertSubAccountInfo(naAccountID);
			}

			mNetOps->unsubAccount(mInfoSub, usnaAccoundIds,true);
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
			BOOST_FOREACH(const RippleAddress& naAccountID, usnaAccoundIds)
			{
				mInfoSub->insertSubAccountInfo(naAccountID);
			}

			mNetOps->unsubAccount(mInfoSub, usnaAccoundIds,false);
		}
	}

	return jvResult;
}

// vim:ts=4
