
#include <iostream>
#include <cstdlib>

#include <boost/asio.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "../json/value.h"
#include "../json/reader.h"

#include "RPC.h"
#include "Log.h"
#include "RPCErr.h"
#include "Config.h"
#include "BitcoinUtil.h"

#include "CallRPC.h"

SETUP_LOG();

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

Json::Value RPCParser::parseAsIs(const Json::Value& jvParams)
{
	return Json::Value(Json::objectValue);
}

// account_info <account>|<nickname>|<account_public_key>
// account_info <seed>|<pass_phrase>|<key> [<index>]
Json::Value RPCParser::parseAccountInfo(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);
	std::string		strIdent	= jvParams[0u].asString();
	// YYY This could be more strict and report casting errors.
	int				iIndex		= 2 == jvParams.size() ? lexical_cast_s<int>(jvParams[1u].asString()) : 0;

	RippleAddress	raAddress;

	if (!raAddress.setAccountPublic(strIdent) && !raAddress.setAccountID(strIdent) && !raAddress.setSeedGeneric(strIdent))
		return rpcError(rpcACT_MALFORMED);

	jvRequest["ident"]	= strIdent;
	jvRequest["index"]	= iIndex;

	return jvRequest;
}

// account_tx <account> <minledger> <maxledger>
// account_tx <account> <ledger>
Json::Value RPCParser::parseAccountTransactions(const Json::Value& jvParams)
{
	Json::Value		jvRequest(Json::objectValue);
	RippleAddress	raAccount;

	if (jvParams.size() < 2 || jvParams.size() > 3)
		return rpcError(rpcINVALID_PARAMS);

	if (!raAccount.setAccountID(jvParams[0u].asString()))
		return rpcError(rpcACT_MALFORMED);

	// YYY This could be more strict and report casting errors.
	if (jvParams.size() == 2)
	{
		jvRequest["ledger"]		= jvParams[1u].asUInt();
	}
	else
	{
		uint32	uLedgerMin	= jvParams[1u].asUInt();
		uint32	uLedgerMax	= jvParams[2u].asUInt();

		if ((uLedgerMax < uLedgerMin) || (uLedgerMax == 0))
		{
			return rpcError(rpcLGR_IDXS_INVALID);
		}

		jvRequest["ledger_min"]	= uLedgerMin;
		jvRequest["ledger_max"]	= uLedgerMax;
	}

	jvRequest["account"]	= raAccount.humanAccountID();

	return jvRequest;
}

// submit any transaction to the network
// submit private_key json
Json::Value RPCParser::parseSubmit(const Json::Value& jvParams)
{
	Json::Value		txJSON;
	Json::Reader	reader;

	if (reader.parse(jvParams[1u].asString(), txJSON))
	{
		Json::Value	jvRequest;

		jvRequest["secret"]		= params[0u].asString();
		jvRequest["tx_json"]	= txJSON;

		return jvRequest;
	}

	return rpcError(rpcINVALID_PARAMS);
}

Json::Value RPCParser::parseEvented(const Json::Value& jvParams)
{
	return rpcError(rpcNO_EVENTS);
}

// Convert a rpc method and params to a request.
// <-- { method: xyz, params: [... ] } or { error: ..., ... }
Json::Value RPCParser::parseCommand(std::string strMethod, Json::Value jvParams)
{
	cLog(lsTRACE) << "RPC method:" << strMethod;
	cLog(lsTRACE) << "RPC params:" << jvParams;

	static struct {
		const char*		pCommand;
		parseFuncPtr	pfpFunc;
		int				iMinParams;
		int				iMaxParams;
	} commandsA[] = {
		// Request-response methods
		// - Returns an error, or the request.
		// - To modify the method, provide a new method in the request.
		{	"accept_ledger",		&RPCParser::parseAsIs,					0,	0	},
		{	"account_info",			&RPCParser::parseAccountInfo,			1,  2	},
		{	"account_tx",			&RPCParser::parseAccountTransactions,	2,  3	},
//		{	"connect",				&RPCParser::doConnect,				1,  2, true,	false,	optNone		},
//		{	"data_delete",			&RPCParser::doDataDelete,			1,  1, true,	false,	optNone		},
//		{	"data_fetch",			&RPCParser::doDataFetch,			1,  1, true,	false,	optNone		},
//		{	"data_store",			&RPCParser::doDataStore,			2,  2, true,	false,	optNone		},
//		{	"get_counts",			&RPCParser::doGetCounts,			0,	1, true,	false,	optNone		},
//		{	"ledger",				&RPCParser::doLedger,				0,  2, false,	false,	optNetwork	},
		{	"ledger_accept",		&RPCParser::parseAsIs,					0,  0	},
		{	"ledger_closed",		&RPCParser::parseAsIs,					0,  0	},
		{	"ledger_current",		&RPCParser::parseAsIs,					0,  0	},
//		{	"ledger_entry",			&RPCParser::doLedgerEntry,		   -1, -1, false,	false,	optCurrent	},
//		{	"ledger_header",		&RPCParser::doLedgerHeader,	   -1, -1, false,	false,	optCurrent	},
//		{	"log_level",			&RPCParser::doLogLevel,			0,  2, true,	false,	optNone		},
		{	"logrotate",			&RPCParser::parseAsIs,					0,  0	},
//		{	"nickname_info",		&RPCParser::doNicknameInfo,		1,  1, false,	false,	optCurrent	},
//		{	"owner_info",			&RPCParser::doOwnerInfo,			1,  2, false,	false,	optCurrent	},
		{	"peers",				&RPCParser::parseAsIs,					0,  0	},
//		{	"profile",				&RPCParser::doProfile,				1,  9, false,	false,	optCurrent	},
//		{	"ripple_lines_get",		&RPCParser::doRippleLinesGet,		1,  2, false,	false,	optCurrent	},
//		{	"ripple_path_find",		&RPCParser::doRipplePathFind,	   -1, -1, false,	false,	optCurrent	},
		{	"submit",				&RPCParser::parseSubmit,				2,  2	},
		{	"server_info",			&RPCParser::parseAsIs,					0,  0	},
		{	"stop",					&RPCParser::parseAsIs,					0,  0	},
//		{	"transaction_entry",	&RPCParser::doTransactionEntry,	-1,  -1, false,	false,	optCurrent	},
//		{	"tx",					&RPCParser::doTx,					1,  1, true,	false,	optNone		},
//		{	"tx_history",			&RPCParser::doTxHistory,			1,  1, false,	false,	optNone		},
//
//		{	"unl_add",				&RPCParser::doUnlAdd,				1,  2, true,	false,	optNone		},
//		{	"unl_delete",			&RPCParser::doUnlDelete,			1,  1, true,	false,	optNone		},
		{	"unl_list",				&RPCParser::parseAsIs,					0,	0	},
		{	"unl_load",				&RPCParser::parseAsIs,					0,	0	},
		{	"unl_network",			&RPCParser::parseAsIs,					0,	0	},
		{	"unl_reset",			&RPCParser::parseAsIs,					0,	0	},
		{	"unl_score",			&RPCParser::parseAsIs,					0,	0	},

//		{	"validation_create",	&RPCParser::doValidationCreate,	0,  1, false,	false,	optNone		},
//		{	"validation_seed",		&RPCParser::doValidationSeed,		0,  1, false,	false,	optNone		},

//		{	"wallet_accounts",		&RPCParser::doWalletAccounts,		1,  1, false,	false,	optCurrent	},
//		{	"wallet_propose",		&RPCParser::doWalletPropose,		0,  1, false,	false,	optNone		},
//		{	"wallet_seed",			&RPCParser::doWalletSeed,			0,  1, false,	false,	optNone		},
//
//		{	"login",				&RPCParser::doLogin,				2,  2, true,	false,	optNone		},

		// Evented methods
		{	"subscribe",			&RPCParser::parseEvented,				-1,	-1	},
		{	"unsubscribe",			&RPCParser::parseEvented,				-1,	-1	},
	};

	int		i = NUMBER(commandsA);

	while (i-- && strMethod != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return rpcError(rpcBAD_SYNTAX);
	}
	else if ((commandsA[i].iMinParams >= 0 && jvParams.size() < commandsA[i].iMinParams)
		|| (commandsA[i].iMaxParams >= 0 && jvParams.size() > commandsA[i].iMaxParams))
	{
		return rpcError(rpcBAD_SYNTAX);
	}

	return (this->*(commandsA[i].pfpFunc))(jvParams);
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

		// std::cerr << "Request: " << jvRequest << std::endl;

		if (jvRequest.isMember("error"))
		{
			jvOutput			= jvRequest;
			jvOutput["rpc"]		= jvRpc;
		}
		else
		{
			Json::Value	jvParams(Json::arrayValue);

			jvParams.append(jvRequest);

			jvOutput	= callRPC(
				jvRequest.isMember("method")			// Allow parser to rewrite method.
					? jvRequest["method"].asString()
					: vCmd[0],
				jvParams);								// Parsed, execute.

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

Json::Value callRPC(const std::string& strMethod, const Json::Value& params)
{
	if (theConfig.RPC_USER.empty() && theConfig.RPC_PASSWORD.empty())
		throw std::runtime_error("You must set rpcpassword=<password> in the configuration file. "
		"If the file does not exist, create it with owner-readable-only file permissions.");

	// Connect to localhost
	if (!theConfig.QUIET)
		std::cerr << "Connecting to: " << theConfig.RPC_IP << ":" << theConfig.RPC_PORT << std::endl;

	boost::asio::ip::tcp::endpoint
		endpoint(boost::asio::ip::address::from_string(theConfig.RPC_IP), theConfig.RPC_PORT);
	boost::asio::ip::tcp::iostream stream;
	stream.connect(endpoint);
	if (stream.fail())
		throw std::runtime_error("couldn't connect to server");

	// HTTP basic authentication
	std::string strUserPass64 = EncodeBase64(theConfig.RPC_USER + ":" + theConfig.RPC_PASSWORD);
	std::map<std::string, std::string> mapRequestHeaders;
	mapRequestHeaders["Authorization"] = std::string("Basic ") + strUserPass64;

	// Send request
	std::string strRequest = JSONRPCRequest(strMethod, params, Json::Value(1));
	cLog(lsDEBUG) << "send request " << strMethod << " : " << strRequest << std::endl;
	std::string strPost = createHTTPPost(strRequest, mapRequestHeaders);
	stream << strPost << std::flush;

	// std::cerr << "post  " << strPost << std::endl;

	// Receive reply
	std::map<std::string, std::string> mapHeaders;
	std::string strReply;
	int nStatus = ReadHTTP(stream, mapHeaders, strReply);
	if (nStatus == 401)
		throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
	else if ((nStatus >= 400) && (nStatus != 400) && (nStatus != 404) && (nStatus != 500)) // ?
		throw std::runtime_error(strprintf("server returned HTTP error %d", nStatus));
	else if (strReply.empty())
		throw std::runtime_error("no response from server");

	// Parse reply
	cLog(lsDEBUG) << "RPC reply: " << strReply << std::endl;

	Json::Reader reader;
	Json::Value valReply;

	if (!reader.parse(strReply, valReply))
		throw std::runtime_error("couldn't parse reply from server");

	if (valReply.isNull())
		throw std::runtime_error("expected reply to have result, error and id properties");

	return valReply;
}

// vim:ts=4
