
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

Json::Value RPCParser::parseAsIs(Json::Value jvReq, const Json::Value &params)
{
	return jvReq;
}

// Convert a command plus args to a json request.
Json::Value RPCParser::parseCommand(Json::Value jvRequest)
{
	std::string	strCommand	= jvRequest["method"].asString();
	Json::Value	jvParams	= jvRequest["params"];

	cLog(lsTRACE) << "RPC:" << strCommand;
	cLog(lsTRACE) << "RPC params:" << jvParams;

	static struct {
		const char*		pCommand;
		parseFuncPtr	pfpFunc;
		int				iMinParams;
		int				iMaxParams;
	} commandsA[] = {
		// Request-response methods
		{	"accept_ledger",		&RPCParser::parseAsIs,		0,	0	},
#if 0
		{	"account_info",			&RPCParser::doAccountInfo,			1,  2, false,	false,	optCurrent	},
		{	"account_tx",			&RPCParser::doAccountTransactions,	2,  3, false,	false,	optNetwork	},
		{	"connect",				&RPCParser::doConnect,				1,  2, true,	false,	optNone		},
		{	"data_delete",			&RPCParser::doDataDelete,			1,  1, true,	false,	optNone		},
		{	"data_fetch",			&RPCParser::doDataFetch,			1,  1, true,	false,	optNone		},
		{	"data_store",			&RPCParser::doDataStore,			2,  2, true,	false,	optNone		},
		{	"get_counts",			&RPCParser::doGetCounts,			0,	1, true,	false,	optNone		},
		{	"ledger",				&RPCParser::doLedger,				0,  2, false,	false,	optNetwork	},
		{	"ledger_accept",		&RPCParser::doLedgerAccept,		0,  0, true,	false,	optCurrent	},
		{	"ledger_closed",		&RPCParser::doLedgerClosed,		0,  0, false,	false,	optClosed	},
		{	"ledger_current",		&RPCParser::doLedgerCurrent,		0,  0, false,	false,	optCurrent	},
		{	"ledger_entry",			&RPCParser::doLedgerEntry,		   -1, -1, false,	false,	optCurrent	},
		{	"ledger_header",		&RPCParser::doLedgerHeader,	   -1, -1, false,	false,	optCurrent	},
		{	"log_level",			&RPCParser::doLogLevel,			0,  2, true,	false,	optNone		},
		{	"logrotate",			&RPCParser::doLogRotate,			0,  0, true,	false,	optNone		},
		{	"nickname_info",		&RPCParser::doNicknameInfo,		1,  1, false,	false,	optCurrent	},
		{	"owner_info",			&RPCParser::doOwnerInfo,			1,  2, false,	false,	optCurrent	},
		{	"peers",				&RPCParser::doPeers,				0,  0, true,	false,	optNone		},
		{	"profile",				&RPCParser::doProfile,				1,  9, false,	false,	optCurrent	},
		{	"ripple_lines_get",		&RPCParser::doRippleLinesGet,		1,  2, false,	false,	optCurrent	},
		{	"ripple_path_find",		&RPCParser::doRipplePathFind,	   -1, -1, false,	false,	optCurrent	},
		{	"submit",				&RPCParser::doSubmit,				2,  2, false,	false,	optCurrent	},
		{	"submit_json",			&RPCParser::doSubmitJson,			-1,  -1, false,	false,	optCurrent	},
		{	"server_info",			&RPCParser::doServerInfo,			0,  0, true,	false,	optNone		},
		{	"stop",					&RPCParser::doStop,				0,  0, true,	false,	optNone		},
		{	"transaction_entry",	&RPCParser::doTransactionEntry,	-1,  -1, false,	false,	optCurrent	},
		{	"tx",					&RPCParser::doTx,					1,  1, true,	false,	optNone		},
		{	"tx_history",			&RPCParser::doTxHistory,			1,  1, false,	false,	optNone		},

		{	"unl_add",				&RPCParser::doUnlAdd,				1,  2, true,	false,	optNone		},
		{	"unl_delete",			&RPCParser::doUnlDelete,			1,  1, true,	false,	optNone		},
		{	"unl_list",				&RPCParser::doUnlList,				0,  0, true,	false,	optNone		},
		{	"unl_load",				&RPCParser::doUnlLoad,				0,  0, true,	false,	optNone		},
		{	"unl_network",			&RPCParser::doUnlNetwork,			0,  0, true,	false,	optNone		},
		{	"unl_reset",			&RPCParser::doUnlReset,			0,  0, true,	false,	optNone		},
		{	"unl_score",			&RPCParser::doUnlScore,			0,  0, true,	false,	optNone		},

		{	"validation_create",	&RPCParser::doValidationCreate,	0,  1, false,	false,	optNone		},
		{	"validation_seed",		&RPCParser::doValidationSeed,		0,  1, false,	false,	optNone		},

		{	"wallet_accounts",		&RPCParser::doWalletAccounts,		1,  1, false,	false,	optCurrent	},
		{	"wallet_propose",		&RPCParser::doWalletPropose,		0,  1, false,	false,	optNone		},
		{	"wallet_seed",			&RPCParser::doWalletSeed,			0,  1, false,	false,	optNone		},

		{	"login",				&RPCParser::doLogin,				2,  2, true,	false,	optNone		},

		// Evented methods
		{	"subscribe",			&RPCParser::doSubscribe,			-1,	-1,	false,	true,	optNone		},
		{	"unsubscribe",			&RPCParser::doUnsubscribe,			-1,	-1,	false,	true,	optNone		},
#endif
	};

	int		i = NUMBER(commandsA);

	while (i-- && strCommand != commandsA[i].pCommand)
		;

	if (i < 0)
	{
		return rpcError(rpcBAD_SYNTAX, jvRequest);
	}
	else if (commandsA[i].iMinParams >= 0
		? commandsA[i].iMaxParams
			? (jvParams.size() < commandsA[i].iMinParams
				|| (commandsA[i].iMaxParams >= 0 && jvParams.size() > commandsA[i].iMaxParams))
			: false
		: jvParams.isArray())
	{
		return rpcError(rpcBAD_SYNTAX, jvRequest);
	}

	return (this->*(commandsA[i].pfpFunc))(jvRequest, jvParams);
}

int commandLineRPC(const std::vector<std::string>& vCmd)
{
	Json::Value jvOutput;
	int			nRet = 0;
	Json::Value	jvRequest(Json::objectValue);

	try
	{
		RPCParser	rpParser;
		Json::Value jvCliParams(Json::arrayValue);

		if (vCmd.empty()) return 1;												// 1 = print usage.

		jvRequest["method"]		= vCmd[0];

		for (int i = 1; i != vCmd.size(); i++)
			jvCliParams.append(vCmd[i]);

		jvRequest["params"]	= jvCliParams;

		jvRequest	= rpParser.parseCommand(jvRequest);

		// std::cerr << "Request: " << jvRequest << std::endl;

		Json::Value jvParams(Json::arrayValue);

		jvParams.append(jvRequest);

		Json::Value jvResult;

		jvResult	= jvRequest.isMember("error")
						? jvRequest												// Parse failed, return error.
						: callRPC(jvRequest["method"].asString(), jvParams);	// Parsed, execute.

		if (jvResult.isMember("error"))
		{
			nRet		= jvResult.isMember("error_code")
							? lexical_cast_s<int>(jvResult["error_code"].asString())
							: 1;
		}

		jvOutput	= jvResult;
	}
	catch (std::exception& e)
	{
		jvRequest["error_what"]	= e.what();

		jvOutput	= rpcError(rpcINTERNAL, jvRequest);
		nRet		= rpcINTERNAL;
	}
	catch (...)
	{
		jvRequest["error_what"]	= "exception";

		jvOutput	= rpcError(rpcINTERNAL, jvRequest);
		nRet		= rpcINTERNAL;
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
