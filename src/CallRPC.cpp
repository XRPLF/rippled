
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

#include "CallRPC.h"
#include "RPC.h"
#include "Config.h"
#include "BitcoinUtil.h"

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
	BIO_write(b64, s.c_str(), s.size());
	(void) BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	std::string result(bptr->data, bptr->length);
	BIO_free_all(b64);

	return result;
}

int commandLineRPC(const std::vector<std::string>& vCmd)
{
	std::string strPrint;
	int nRet = 0;
	try
	{
		if (vCmd.empty()) return 1;

		std::string strMethod = vCmd[0];

		// Parameters default to strings
		Json::Value params(Json::arrayValue);
		for (int i = 1; i != vCmd.size(); i++)
			params.append(vCmd[i]);

		// Execute
		Json::Value reply = callRPC(strMethod, params);

		// Parse reply
		Json::Value result = reply.get("result", Json::Value());
		Json::Value error = reply.get("error", Json::Value());

		if (result.isString() && (result.asString() == "unknown command"))
			nRet=1;

		if (!error.isNull())
		{ // Error
			strPrint = "error: " + error.toStyledString();
			int code = error["code"].asInt();
			nRet = abs(code);
		}
		else
		{ // Result
			if (result.isNull())
				strPrint = "";
			else if (result.isString())
				strPrint = result.asString();
			else
				strPrint = result.toStyledString();
		}
	}
	catch (std::exception& e)
	{
		strPrint = std::string("error: ") + e.what();
		nRet = 87;
	}
	catch (...)
	{
		std::cout << "Exception CommandLineRPC()" << std::endl;
	}

	if (strPrint != "")
	{
		std::cout << strPrint << std::endl;
	}
	return nRet;
}


Json::Value callRPC(const std::string& strMethod, const Json::Value& params)
{
	if (theConfig.RPC_USER.empty() && theConfig.RPC_PASSWORD.empty())
		throw std::runtime_error("You must set rpcpassword=<password> in the configuration file"
		"If the file does not exist, create it with owner-readable-only file permissions.");

	// Connect to localhost
	std::cout << "Connecting to port:" << theConfig.RPC_PORT << std::endl;
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
	std::cout << "send request " << strMethod << " : " << strRequest << std::endl;
	std::string strPost = createHTTPPost(strRequest, mapRequestHeaders);
	stream << strPost << std::flush;

	std::cout << "post  " << strPost << std::endl;

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
	std::cout << "RPC reply: " << strReply << std::endl;
	Json::Reader reader;
	Json::Value valReply;
	if (!reader.parse(strReply, valReply))
		throw std::runtime_error("couldn't parse reply from server");
	if (valReply.isNull())
		throw std::runtime_error("expected reply to have result, error and id properties");

	return valReply;
}

// vim:ts=4
