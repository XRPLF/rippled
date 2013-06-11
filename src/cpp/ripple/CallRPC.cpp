//
// This a trusted interface, the user is expected to provide valid input to perform valid requests.
// Error catching and reporting is not a requirement of this command line interface.
//
// Improvements to be more strict and to provide better diagnostics are welcome.
//

#include <iostream>
#include <cstdlib>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "RPC.h"
#include "RPCErr.h"

#include "CallRPC.h"

SETUP_LOG (RPCParser)

static inline bool isSwitchChar(char c)
{
#ifdef __WXMSW__
	return c == '-' || c == '/';
#else
	return c == '-';
#endif
}

std::string EncodeBase64(const std::string& s)
{ // FIXME: This performs terribly
	BIO *b64, *bmem;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, s.data(), s.size());
	(void) BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	std::string result(bptr->data, bptr->length);
	BIO_free_all(b64);

	return result;
}

// TODO New routine for parsing ledger parameters, other routines should standardize on this.
static bool jvParseLedger(Json::Value& jvRequest, const std::string& strLedger)
{
	if (strLedger == "current" || strLedger == "closed" || strLedger == "validated")
	{
		jvRequest["ledger_index"]	= strLedger;
	}
	else if (strLedger.length() == 64)
	{
		// YYY Could confirm this is a uint256.
		jvRequest["ledger_hash"]	= strLedger;
	}
	else
	{
		jvRequest["ledger_index"]	= lexical_cast_s<uint32>(strLedger);
	}

	return true;
}

// Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
static Json::Value jvParseCurrencyIssuer(const std::string& strCurrencyIssuer)
{
	static boost::regex	reCurIss("\\`([[:alpha:]]{3})(?:/(.+))?\\'");

	boost::smatch	smMatch;

	if (boost::regex_match(strCurrencyIssuer, smMatch, reCurIss))
	{
		Json::Value	jvResult(Json::objectValue);
		std::string	strCurrency	= smMatch[1];
		std::string	strIssuer	= smMatch[2];

		jvResult["currency"]	= strCurrency;

		if (strIssuer.length())
		{
			// Could confirm issuer is a valid Ripple address.
			jvResult["issuer"]		= strIssuer;
		}

		return jvResult;
	}
	else
	{
		return rpcError(rpcINVALID_PARAMS);
	}
}

Json::Value RPCParser::parseAsIs(const Json::Value& jvParams)
{
	Json::Value v(Json::objectValue);

	if (jvParams.isArray() && (jvParams.size() > 0))
		v["params"] = jvParams;

	return v;
}

Json::Value RPCParser::parseInternal(const Json::Value& jvParams)
{
	Json::Value v(Json::objectValue);
	v["internal_command"] = jvParams[0u];

	Json::Value params(Json::arrayValue);
	for (unsigned i = 1; i < jvParams.size(); ++i)
		params.append(jvParams[i]);
	v["params"] = params;

	return v;
}

// account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]
Json::Value RPCParser::parseAccountTransactions(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);
	RippleAddress	raAccount;
	unsigned int	iParams = jvParams.size();

	if (!raAccount.setAccountID(jvParams[0u].asString()))
		return rpcError(rpcACT_MALFORMED);

	jvRequest["account"]	= raAccount.humanAccountID();

	bool			bDone	= false;

	while (!bDone && iParams >= 2) {
		if (jvParams[iParams-1].asString() == "binary")
		{
			jvRequest["binary"]		= true;
			--iParams;
		}
		else if (jvParams[iParams-1].asString() == "count")
		{
			jvRequest["count"]		= true;
			--iParams;
		}
		else if (jvParams[iParams-1].asString() == "descending")
		{
			jvRequest["descending"]	= true;
			--iParams;
		}
		else
		{
			bDone	= true;
		}
	}

	if (1 == iParams)
	{
		nothing();
	}
	else if (2 == iParams)
	{
		if (!jvParseLedger(jvRequest, jvParams[1u].asString()))
			return jvRequest;
	}
	else
	{
		int64	uLedgerMin	= jvParams[1u].asInt();
		int64	uLedgerMax	= jvParams[2u].asInt();

		if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
		{
			return rpcError(rpcLGR_IDXS_INVALID);
		}

		jvRequest["ledger_index_min"]	= jvParams[1u].asInt();
		jvRequest["ledger_index_max"]	= jvParams[2u].asInt();

		if (iParams >= 4)
			jvRequest["limit"]	= jvParams[3u].asInt();

		if (iParams >= 5)
			jvRequest["offset"]	= jvParams[4u].asInt();
	}

	return jvRequest;
}

// book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit> [<proof> [<marker>]]]]]
// limit: 0 = no limit
// proof: 0 or 1
//
// Mnemonic: taker pays --> offer --> taker gets
Json::Value RPCParser::parseBookOffers(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	Json::Value		jvTakerPays	= jvParseCurrencyIssuer(jvParams[0u].asString());
	Json::Value		jvTakerGets	= jvParseCurrencyIssuer(jvParams[1u].asString());

	if (isRpcError(jvTakerPays))
	{
		return jvTakerPays;
	}
	else
	{
		jvRequest["taker_pays"]	= jvTakerPays;
	}

	if (isRpcError(jvTakerGets))
	{
		return jvTakerGets;
	}
	else
	{
		jvRequest["taker_gets"]	= jvTakerGets;
	}

	if (jvParams.size() >= 3)
	{
		jvRequest["issuer"]	= jvParams[2u].asString();
	}

	if (jvParams.size() >= 4 && !jvParseLedger(jvRequest, jvParams[3u].asString()))
		return jvRequest;

	if (jvParams.size() >= 5)
	{
		int		iLimit	= jvParams[5u].asInt();

		if (iLimit > 0)
			jvRequest["limit"]	= iLimit;
	}

	if (jvParams.size() >= 6 && jvParams[5u].asInt())
	{
		jvRequest["proof"]	= true;
	}

	if (jvParams.size() == 7)
		jvRequest["marker"]	= jvParams[6u];

	return jvRequest;
}

// connect <ip> [port]
Json::Value RPCParser::parseConnect(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	jvRequest["ip"]	= jvParams[0u].asString();

	if (jvParams.size() == 2)
		jvRequest["port"]	= jvParams[1u].asUInt();

	return jvRequest;
}

#if ENABLE_INSECURE
// data_delete <key>
Json::Value RPCParser::parseDataDelete(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	jvRequest["key"]	= jvParams[0u].asString();

	return jvRequest;
}
#endif

#if ENABLE_INSECURE
// data_fetch <key>
Json::Value RPCParser::parseDataFetch(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	jvRequest["key"]	= jvParams[0u].asString();

	return jvRequest;
}
#endif

#if ENABLE_INSECURE
// data_store <key> <value>
Json::Value RPCParser::parseDataStore(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	jvRequest["key"]	= jvParams[0u].asString();
	jvRequest["value"]	= jvParams[1u].asString();

	return jvRequest;
}
#endif

// Return an error for attemping to subscribe/unsubscribe via RPC.
Json::Value RPCParser::parseEvented(const Json::Value& jvParams)
{
	return rpcError(rpcNO_EVENTS);
}

// feature [<feature>] [true|false]
Json::Value RPCParser::parseFeature(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	if (jvParams.size() > 0)
		jvRequest["feature"]	= jvParams[0u].asString();

	if (jvParams.size() > 1)
		jvRequest["vote"]		= boost::lexical_cast<bool>(jvParams[1u].asString());

	return jvRequest;
}

// get_counts [<min_count>]
Json::Value RPCParser::parseGetCounts(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	if (jvParams.size())
		jvRequest["min_count"]	= jvParams[0u].asUInt();

	return jvRequest;
}

// json <command> <json>
Json::Value RPCParser::parseJson(const Json::Value& jvParams)
{
	Json::Reader	reader;
	Json::Value		jvRequest;

	WriteLog (lsTRACE, RPCParser) << "RPC method: " << jvParams[0u];
	WriteLog (lsTRACE, RPCParser) << "RPC json: " << jvParams[1u];

	if (reader.parse(jvParams[1u].asString(), jvRequest))
	{
		jvRequest["method"]	= jvParams[0u];

		return jvRequest;
	}

	return rpcError(rpcINVALID_PARAMS);
}

// ledger [id|index|current|closed|validated] [full]
Json::Value RPCParser::parseLedger(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	if (!jvParams.size())
	{
		return jvRequest;
	}

	jvParseLedger(jvRequest, jvParams[0u].asString());

	if (2 == jvParams.size() && jvParams[1u].asString() == "full")
	{
		jvRequest["full"]	= bool(1);
	}

	return jvRequest;
}

// ledger_header <id>|<index>
Json::Value RPCParser::parseLedgerId(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	std::string		strLedger	= jvParams[0u].asString();

	if (strLedger.length() == 32)
	{
		jvRequest["ledger_hash"]	= strLedger;
	}
	else
	{
		jvRequest["ledger_index"]	= lexical_cast_s<uint32>(strLedger);
	}

	return jvRequest;
}

#if ENABLE_INSECURE
// login <username> <password>
Json::Value RPCParser::parseLogin(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	jvRequest["username"]	= jvParams[0u].asString();
	jvRequest["password"]	= jvParams[1u].asString();

	return jvRequest;
}
#endif

// log_level:							Get log levels
// log_level <severity>:				Set master log level to the specified severity
// log_level <partition> <severity>:	Set specified partition to specified severity
Json::Value RPCParser::parseLogLevel(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);

	if (jvParams.size() == 1)
	{
		jvRequest["severity"] = jvParams[0u].asString();
	}
	else if (jvParams.size() == 2)
	{
		jvRequest["partition"] = jvParams[0u].asString();
		jvRequest["severity"] = jvParams[1u].asString();
	}

	return jvRequest;
}

// owner_info <account>|<nickname>|<account_public_key>
// owner_info <seed>|<pass_phrase>|<key> [<ledfer>]
// account_info <account>|<nickname>|<account_public_key>
// account_info <seed>|<pass_phrase>|<key> [<ledger>]
// account_offers <account>|<nickname>|<account_public_key> [<ledger>]
Json::Value RPCParser::parseAccountItems(const Json::Value& jvParams)
{
	return parseAccountRaw(jvParams, false);
}

// account_lines <account> <account>|"" [<ledger>]
Json::Value RPCParser::parseAccountLines(const Json::Value& jvParams)
{
	return parseAccountRaw(jvParams, true);
}

// TODO: Get index from an alternate syntax: rXYZ:<index>
Json::Value RPCParser::parseAccountRaw(const Json::Value& jvParams, bool bPeer)
{
	std::string		strIdent	= jvParams[0u].asString();
	unsigned int	iCursor		= jvParams.size();
	bool			bStrict		= false;
	std::string		strPeer;

	if (!bPeer && iCursor >= 2 && jvParams[iCursor-1] == "strict")
	{
		bStrict	= true;
		--iCursor;
	}

	if (bPeer && iCursor >= 2)
		strPeer	= jvParams[iCursor].asString();

	int				iIndex		= 0;
//	int				iIndex		= jvParams.size() >= 2 ? lexical_cast_s<int>(jvParams[1u].asString()) : 0;

	RippleAddress	raAddress;

	if (!raAddress.setAccountPublic(strIdent) && !raAddress.setAccountID(strIdent) && !raAddress.setSeedGeneric(strIdent))
		return rpcError(rpcACT_MALFORMED);

	// Get info on account.
	Json::Value jvRequest(Json::objectValue);

	jvRequest["account"]	= strIdent;

	if (bStrict)
		jvRequest["strict"]		= 1;

	if (iIndex)
		jvRequest["account_index"]	= iIndex;

	if (!strPeer.empty())
	{
		RippleAddress	raPeer;

		if (!raPeer.setAccountPublic(strPeer) && !raPeer.setAccountID(strPeer) && !raPeer.setSeedGeneric(strPeer))
			return rpcError(rpcACT_MALFORMED);

		jvRequest["peer"]	= strPeer;
	}

	if (iCursor == (2+bPeer) && !jvParseLedger(jvRequest, jvParams[1u+bPeer].asString()))
		return rpcError(rpcLGR_IDX_MALFORMED);

	return jvRequest;
}

// proof_create [<difficulty>] [<secret>]
Json::Value RPCParser::parseProofCreate(const Json::Value& jvParams)
{
	Json::Value		jvRequest;

	if (jvParams.size() >= 1)
		jvRequest["difficulty"] = jvParams[0u].asInt();

	if (jvParams.size() >= 2)
		jvRequest["secret"] = jvParams[1u].asString();

	return jvRequest;
}

// proof_solve <token>
Json::Value RPCParser::parseProofSolve(const Json::Value& jvParams)
{
	Json::Value		jvRequest;

	jvRequest["token"] = jvParams[0u].asString();

	return jvRequest;
}

// proof_verify <token> <solution> [<difficulty>] [<secret>]
Json::Value RPCParser::parseProofVerify(const Json::Value& jvParams)
{
	Json::Value		jvRequest;

	jvRequest["token"] = jvParams[0u].asString();
	jvRequest["solution"] = jvParams[1u].asString();

	if (jvParams.size() >= 3)
		jvRequest["difficulty"] = jvParams[2u].asInt();

	if (jvParams.size() >= 4)
		jvRequest["secret"] = jvParams[3u].asString();

	return jvRequest;
}

// ripple_path_find <json> [<ledger>]
Json::Value RPCParser::parseRipplePathFind(const Json::Value& jvParams)
{
	Json::Reader	reader;
	Json::Value		jvRequest;
	bool			bLedger		= 2 == jvParams.size();

	WriteLog (lsTRACE, RPCParser) << "RPC json: " << jvParams[0u];

	if (reader.parse(jvParams[0u].asString(), jvRequest))
	{
		if (bLedger)
		{
			jvParseLedger(jvRequest, jvParams[1u].asString());
		}

		return jvRequest;
	}

	return rpcError(rpcINVALID_PARAMS);
}

// sign/submit any transaction to the network
//
// sign <private_key> <json> offline
// submit <private_key> <json>
// submit <tx_blob>
Json::Value RPCParser::parseSignSubmit(const Json::Value& jvParams)
{
	Json::Value		txJSON;
	Json::Reader	reader;
	bool			bOffline	= 3 == jvParams.size() && jvParams[2u].asString() == "offline";

	if (1 == jvParams.size())
	{
		// Submitting tx_blob

		Json::Value	jvRequest;

		jvRequest["tx_blob"]	= jvParams[0u].asString();

		return jvRequest;
	}
	else if ((2 == jvParams.size() || bOffline)
		&& reader.parse(jvParams[1u].asString(), txJSON))
	{
		// Signing or submitting tx_json.
		Json::Value	jvRequest;

		jvRequest["secret"]		= jvParams[0u].asString();
		jvRequest["tx_json"]	= txJSON;
		if (bOffline)
			jvRequest["offline"]	= true;

		return jvRequest;
	}

	return rpcError(rpcINVALID_PARAMS);
}

// sms <text>
Json::Value RPCParser::parseSMS(const Json::Value& jvParams)
{
	Json::Value		jvRequest;

	jvRequest["text"]	= jvParams[0u].asString();

	return jvRequest;
}

// tx <transaction_id>
Json::Value RPCParser::parseTx(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	if (jvParams.size() > 1)
	{
		if (jvParams[1u].asString() == "binary")
			jvRequest["binary"] = true;
	}

	jvRequest["transaction"]	= jvParams[0u].asString();
	return jvRequest;
}

// tx_history <index>
Json::Value RPCParser::parseTxHistory(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	jvRequest["start"]	= jvParams[0u].asUInt();

	return jvRequest;
}

// unl_add <domain>|<node_public> [<comment>]
Json::Value RPCParser::parseUnlAdd(const Json::Value& jvParams)
{
	std::string	strNode		= jvParams[0u].asString();
	std::string strComment	= (jvParams.size() == 2) ? jvParams[1u].asString() : "";

	RippleAddress	naNodePublic;

	if (strNode.length())
	{
		Json::Value	jvRequest;

		jvRequest["node"]		= strNode;

		if (strComment.length())
			jvRequest["comment"]	= strComment;

		return jvRequest;
	}

	return rpcError(rpcINVALID_PARAMS);
}

// unl_delete <domain>|<public_key>
Json::Value RPCParser::parseUnlDelete(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	jvRequest["node"]		= jvParams[0u].asString();

	return jvRequest;
}

// validation_create [<pass_phrase>|<seed>|<seed_key>]
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCParser::parseValidationCreate(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	if (jvParams.size())
		jvRequest["secret"]		= jvParams[0u].asString();

	return jvRequest;
}

// validation_seed [<pass_phrase>|<seed>|<seed_key>]
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCParser::parseValidationSeed(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	if (jvParams.size())
		jvRequest["secret"]		= jvParams[0u].asString();

	return jvRequest;
}

// wallet_accounts <seed>
Json::Value RPCParser::parseWalletAccounts(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	jvRequest["seed"]		= jvParams[0u].asString();

	return jvRequest;
}

// wallet_propose [<passphrase>]
// <passphrase> is only for testing. Master seeds should only be generated randomly.
Json::Value RPCParser::parseWalletPropose(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	if (jvParams.size())
		jvRequest["passphrase"]		= jvParams[0u].asString();

	return jvRequest;
}

// wallet_seed [<seed>|<passphrase>|<passkey>]
Json::Value RPCParser::parseWalletSeed(const Json::Value& jvParams)
{
	Json::Value	jvRequest;

	if (jvParams.size())
		jvRequest["secret"]		= jvParams[0u].asString();

	return jvRequest;
}

//
// parseCommand
//

// Convert a rpc method and params to a request.
// <-- { method: xyz, params: [... ] } or { error: ..., ... }
Json::Value RPCParser::parseCommand(std::string strMethod, Json::Value jvParams)
{
	WriteLog (lsTRACE, RPCParser) << "RPC method:" << strMethod;
	WriteLog (lsTRACE, RPCParser) << "RPC params:" << jvParams;

	static struct {
		const char*		pCommand;
		parseFuncPtr	pfpFunc;
		int				iMinParams;
		int				iMaxParams;
	} commandsA[] = {
		// Request-response methods
		// - Returns an error, or the request.
		// - To modify the method, provide a new method in the request.
		{	"account_info",			&RPCParser::parseAccountItems,			1,  2	},
		{	"account_lines",		&RPCParser::parseAccountLines,			1,  3	},
		{	"account_offers",		&RPCParser::parseAccountItems,			1,  2	},
		{	"account_tx",			&RPCParser::parseAccountTransactions,	1,  8	},
		{	"book_offers",			&RPCParser::parseBookOffers,			2,  7	},
		{	"connect",				&RPCParser::parseConnect,				1,  2	},
		{	"consensus_info",		&RPCParser::parseAsIs,					0,	0	},
		{	"feature",				&RPCParser::parseFeature,				0,	2	},
		{	"get_counts",			&RPCParser::parseGetCounts,				0,	1	},
		{	"json",					&RPCParser::parseJson,					2,  2	},
		{	"ledger",				&RPCParser::parseLedger,				0,  2	},
		{	"ledger_accept",		&RPCParser::parseAsIs,					0,  0	},
		{	"ledger_closed",		&RPCParser::parseAsIs,					0,  0	},
		{	"ledger_current",		&RPCParser::parseAsIs,					0,  0	},
//		{	"ledger_entry",			&RPCParser::parseLedgerEntry,		   -1, -1	},
		{	"ledger_header",		&RPCParser::parseLedgerId,				1,  1	},
		{	"log_level",			&RPCParser::parseLogLevel,				0,  2	},
		{	"logrotate",			&RPCParser::parseAsIs,					0,  0	},
//		{	"nickname_info",		&RPCParser::parseNicknameInfo,			1,  1	},
		{	"owner_info",			&RPCParser::parseAccountItems,			1,  2	},
		{	"peers",				&RPCParser::parseAsIs,					0,  0	},
		{	"ping",					&RPCParser::parseAsIs,					0,  0	},
//		{	"profile",				&RPCParser::parseProfile,				1,  9	},
		{	"proof_create",			&RPCParser::parseProofCreate,			0,  2	},
		{	"proof_solve",			&RPCParser::parseProofSolve,			1,  1	},
		{	"proof_verify",			&RPCParser::parseProofVerify,			2,  4	},
		{	"random",				&RPCParser::parseAsIs,					0,  0	},
		{	"ripple_path_find",		&RPCParser::parseRipplePathFind,	    1,  2	},
		{	"sign",					&RPCParser::parseSignSubmit,			2,  3	},
		{	"sms",					&RPCParser::parseSMS,					1,  1	},
		{	"submit",				&RPCParser::parseSignSubmit,			1,  2	},
		{	"server_info",			&RPCParser::parseAsIs,					0,  0	},
		{	"server_state",			&RPCParser::parseAsIs,					0,	0	},
		{	"stop",					&RPCParser::parseAsIs,					0,  0	},
//		{	"transaction_entry",	&RPCParser::parseTransactionEntry,	   -1,  -1	},
		{	"tx",					&RPCParser::parseTx,					1,  2	},
		{	"tx_history",			&RPCParser::parseTxHistory,				1,  1	},

		{	"unl_add",				&RPCParser::parseUnlAdd,				1,  2	},
		{	"unl_delete",			&RPCParser::parseUnlDelete,				1,  1	},
		{	"unl_list",				&RPCParser::parseAsIs,					0,	0	},
		{	"unl_load",				&RPCParser::parseAsIs,					0,	0	},
		{	"unl_network",			&RPCParser::parseAsIs,					0,	0	},
		{	"unl_reset",			&RPCParser::parseAsIs,					0,	0	},
		{	"unl_score",			&RPCParser::parseAsIs,					0,	0	},

		{	"validation_create",	&RPCParser::parseValidationCreate,		0,  1	},
		{	"validation_seed",		&RPCParser::parseValidationSeed,		0,  1	},

		{	"wallet_accounts",		&RPCParser::parseWalletAccounts,	    1,  1	},
		{	"wallet_propose",		&RPCParser::parseWalletPropose,			0,  1	},
		{	"wallet_seed",			&RPCParser::parseWalletSeed,			0,  1	},

		{	"internal",				&RPCParser::parseInternal,				1,	-1	},

#if ENABLE_INSECURE
		// XXX Unnecessary commands which should be removed.
		{	"login",				&RPCParser::parseLogin,					2,  2	},
		{	"data_delete",			&RPCParser::parseDataDelete,			1,  1	},
		{	"data_fetch",			&RPCParser::parseDataFetch,				1,  1	},
		{	"data_store",			&RPCParser::parseDataStore,				2,  2	},
#endif

		// Evented methods
		{	"path_find",			&RPCParser::parseEvented,				-1, -1	},
		{	"subscribe",			&RPCParser::parseEvented,				-1,	-1	},
		{	"unsubscribe",			&RPCParser::parseEvented,				-1,	-1	},
	};

	int		i = NUMBER(commandsA);

	while (i-- && strMethod != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return rpcError(rpcUNKNOWN_COMMAND);
	}
	else if ((commandsA[i].iMinParams >= 0 && jvParams.size() < commandsA[i].iMinParams)
		|| (commandsA[i].iMaxParams >= 0 && jvParams.size() > commandsA[i].iMaxParams))
	{
		WriteLog (lsWARNING, RPCParser) << "Wrong number of parameters: minimum=" << commandsA[i].iMinParams
			<< " maximum=" << commandsA[i].iMaxParams
			<< " actual=" << jvParams.size();

		return rpcError(rpcBAD_SYNTAX);
	}

	return (this->*(commandsA[i].pfpFunc))(jvParams);
}

// Place the async result somewhere useful.
void callRPCHandler(Json::Value* jvOutput, const Json::Value& jvInput)
{
	(*jvOutput)	= jvInput;
}

int commandLineRPC(const std::vector<std::string>& vCmd)
{
	Json::Value jvOutput;
	int			nRet = 0;
	Json::Value	jvRequest(Json::objectValue);

	try
	{
		RPCParser	rpParser;
		Json::Value jvRpcParams(Json::arrayValue);

		if (vCmd.empty()) return 1;												// 1 = print usage.

		for (int i = 1; i != vCmd.size(); i++)
			jvRpcParams.append(vCmd[i]);

		Json::Value	jvRpc	= Json::Value(Json::objectValue);

		jvRpc["method"]	= vCmd[0];
		jvRpc["params"]	= jvRpcParams;

		jvRequest	= rpParser.parseCommand(vCmd[0], jvRpcParams);

		WriteLog (lsTRACE, RPCParser) << "RPC Request: " << jvRequest << std::endl;

		if (jvRequest.isMember("error"))
		{
			jvOutput			= jvRequest;
			jvOutput["rpc"]		= jvRpc;
		}
		else
		{
			Json::Value	jvParams(Json::arrayValue);

			jvParams.append(jvRequest);

			if (!theConfig.RPC_ADMIN_USER.empty())
				jvRequest["admin_user"]		= theConfig.RPC_ADMIN_USER;

			if (!theConfig.RPC_ADMIN_PASSWORD.empty())
				jvRequest["admin_password"]	= theConfig.RPC_ADMIN_PASSWORD;

			boost::asio::io_service			isService;

			callRPC(
				isService,
				theConfig.RPC_IP, theConfig.RPC_PORT,
				theConfig.RPC_USER, theConfig.RPC_PASSWORD,
				"",
				jvRequest.isMember("method")			// Allow parser to rewrite method.
					? jvRequest["method"].asString()
					: vCmd[0],
				jvParams,								// Parsed, execute.
				false,
				BIND_TYPE(callRPCHandler, &jvOutput, P_1));

			isService.run(); // This blocks until there is no more outstanding async calls.

			if (jvOutput.isMember("result"))
			{
				// Had a successful JSON-RPC 2.0 call.
				jvOutput	= jvOutput["result"];

				// jvOutput may report a server side error.
				// It should report "status".
			}
			else
			{
				// Transport error.
				Json::Value	jvRpcError	= jvOutput;

				jvOutput			= rpcError(rpcJSON_RPC);
				jvOutput["result"]	= jvRpcError;
			}

			// If had an error, supply invokation in result.
			if (jvOutput.isMember("error"))
			{
				jvOutput["rpc"]				= jvRpc;			// How the command was seen as method + params.
				jvOutput["request_sent"]	= jvRequest;		// How the command was translated.
			}
		}

		if (jvOutput.isMember("error"))
		{
			jvOutput["status"]	= "error";

			nRet	= jvOutput.isMember("error_code")
						? lexical_cast_s<int>(jvOutput["error_code"].asString())
						: 1;
		}

		// YYY We could have a command line flag for single line output for scripts.
		// YYY We would intercept output here and simplify it.
	}
	catch (std::exception& e)
	{
		jvOutput				= rpcError(rpcINTERNAL);
		jvOutput["error_what"]	= e.what();
		nRet					= rpcINTERNAL;
	}
	catch (...)
	{
		jvOutput				= rpcError(rpcINTERNAL);
		jvOutput["error_what"]	= "exception";
		nRet					= rpcINTERNAL;
	}

	std::cout << jvOutput.toStyledString();

	return nRet;
}

#define RPC_REPLY_MAX_BYTES		(128*1024*1024)
#define RPC_NOTIFY_SECONDS		30

bool responseRPC(
	FUNCTION_TYPE<void(const Json::Value& jvInput)> callbackFuncP,
	const boost::system::error_code& ecResult, int iStatus, const std::string& strData)
{
	if (callbackFuncP)
	{
		// Only care about the result, if we care to deliver it callbackFuncP.

		// Receive reply
		if (iStatus == 401)
			throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
		else if ((iStatus >= 400) && (iStatus != 400) && (iStatus != 404) && (iStatus != 500)) // ?
			throw std::runtime_error(strprintf("server returned HTTP error %d", iStatus));
		else if (strData.empty())
			throw std::runtime_error("no response from server");

		// Parse reply
		WriteLog (lsDEBUG, RPCParser) << "RPC reply: " << strData << std::endl;

		Json::Reader	reader;
		Json::Value		jvReply;

		if (!reader.parse(strData, jvReply))
			throw std::runtime_error("couldn't parse reply from server");

		if (jvReply.isNull())
			throw std::runtime_error("expected reply to have result, error and id properties");

		Json::Value		jvResult(Json::objectValue);

		jvResult["result"] = jvReply;

		(callbackFuncP)(jvResult);
	}

	return false;
}

// Build the request.
void requestRPC(const std::string& strMethod, const Json::Value& jvParams,
	const std::map<std::string, std::string>& mHeaders, const std::string& strPath,
	boost::asio::streambuf& sb, const std::string& strHost)
{
	WriteLog (lsDEBUG, RPCParser) << "requestRPC: strPath='" << strPath << "'";

	std::ostream	osRequest(&sb);

	osRequest <<
		createHTTPPost(
			strHost,
			strPath,
			JSONRPCRequest(strMethod, jvParams, Json::Value(1)),
			mHeaders);
}

void callRPC(
	boost::asio::io_service& io_service,
	const std::string& strIp, const int iPort,
	const std::string& strUsername, const std::string& strPassword,
	const std::string& strPath, const std::string& strMethod,
	const Json::Value& jvParams, const bool bSSL,
	FUNCTION_TYPE<void(const Json::Value& jvInput)> callbackFuncP)
{
	// Connect to localhost
	if (!theConfig.QUIET)
	{
		std::cerr << "Connecting to: " << strIp << ":" << iPort << std::endl;
//		std::cerr << "Username: " << strUsername << ":" << strPassword << std::endl;
//		std::cerr << "Path: " << strPath << std::endl;
//		std::cerr << "Method: " << strMethod << std::endl;
	}

	// HTTP basic authentication
	std::string strUserPass64 = EncodeBase64(strUsername + ":" + strPassword);

	std::map<std::string, std::string> mapRequestHeaders;

	mapRequestHeaders["Authorization"] = std::string("Basic ") + strUserPass64;

	// Send request
	// Log(lsDEBUG) << "requesting" << std::endl;
	// WriteLog (lsDEBUG, RPCParser) << "send request " << strMethod << " : " << strRequest << std::endl;

	HttpsClient::httpsRequest(
		bSSL,
		io_service,
		strIp,
		iPort,
		BIND_TYPE(
			&requestRPC,
			strMethod,
			jvParams,
			mapRequestHeaders,
			strPath, P_1, P_2),
		RPC_REPLY_MAX_BYTES,
		boost::posix_time::seconds(RPC_NOTIFY_SECONDS),
		BIND_TYPE(&responseRPC, callbackFuncP, P_1, P_2, P_3));
}

// vim:ts=4
