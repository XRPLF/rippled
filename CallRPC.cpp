#include "CallRPC.h"
#include "RPC.h"

#include "Config.h"
#include "BitcoinUtil.h"

#include <boost/asio.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include "string.h"
#include <stdlib.h>

#include <openssl/buffer.h>
//#include <openssl/ecdsa.h>
#include <openssl/evp.h>
//#include <openssl/rand.h>
//#include <openssl/sha.h>
//#include <openssl/ripemd.h>

using namespace std;
using namespace boost::asio;

inline bool isSwitchChar(char c)
{
#ifdef __WXMSW__
	return c == '-' || c == '/';
#else
	return c == '-';
#endif
}

string EncodeBase64(string s)
{
	BIO *b64, *bmem;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, s.c_str(), s.size());
	BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	string result(bptr->data, bptr->length);
	BIO_free_all(b64);

	return result;
}

#if 0 
int commandLineRPC(int argc, char *argv[])
{
	string strPrint;
	int nRet = 0;
	try
	{
		// Skip switches
		while(argc > 1 && isSwitchChar(argv[1][0]))
		{
			argc--;
			argv++;
		}

		if(argc < 2) return(0);

		string strMethod = argv[1];

		// Parameters default to strings
		json_spirit::Array params;
		for (int i = 2; i < argc; i++)
			params.push_back(argv[i]);

		// Execute
		json_spirit::Object reply = callRPC(strMethod, params);

		// Parse reply
		const json_spirit::Value& result = find_value(reply, "result");
		const json_spirit::Value& error  = find_value(reply, "error");

		if(error.type() != json_spirit::null_type)
		{
			// Error
			strPrint = "error: " + write_string(error, false);
			int code = find_value(error.get_obj(), "code").get_int();
			nRet = abs(code);
		}else
		{
			// Result
			if (result.type() == json_spirit::null_type)
				strPrint = "";
			else if (result.type() == json_spirit::str_type)
				strPrint = result.get_str();
			else
				strPrint = write_string(result, true);
		}
	}
	catch (std::exception& e)
	{
		strPrint = string("error: ") + e.what();
		nRet = 87;
	}
	catch (...)
	{
		cout << "Exception CommandLineRPC()" << endl;
	}

	if(strPrint != "")
	{
		cout << strPrint << endl;
	}
	return nRet;
}


json_spirit::Object callRPC(const string& strMethod, const json_spirit::Array& params)
{
	if(theConfig.RPC_USER == "" && theConfig.RPC_PASSWORD == "")
		throw runtime_error("You must set rpcpassword=<password> in the configuration file"
		"If the file does not exist, create it with owner-readable-only file permissions.");

	// Connect to localhost

	cout << "Connecting to port:" << theConfig.RPC_PORT << endl;
	ip::tcp::endpoint endpoint( ip::address::from_string("127.0.0.1"), theConfig.RPC_PORT);
	ip::tcp::iostream stream;
	stream.connect(endpoint);
	if(stream.fail())
		throw runtime_error("couldn't connect to server");



	// HTTP basic authentication
	string strUserPass64 = EncodeBase64(theConfig.RPC_USER + ":" + theConfig.RPC_PASSWORD);
	map<string, string> mapRequestHeaders;
	mapRequestHeaders["Authorization"] = string("Basic ") + strUserPass64;


	// Send request
	string strRequest = JSONRPCRequest(strMethod, params, 1);
	cout << "send request " << strMethod << " : " << strRequest << endl; 
	string strPost = createHTTPPost(strRequest, mapRequestHeaders);
	stream << strPost << std::flush;

	cout << "post  " << strPost << endl; 

	// Receive reply
	map<string, string> mapHeaders;
	string strReply;
	int nStatus = ReadHTTP(stream, mapHeaders, strReply);
	if (nStatus == 401)
		throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
	else if (nStatus >= 400 && nStatus != 400 && nStatus != 404 && nStatus != 500)
		throw runtime_error(strprintf("server returned HTTP error %d", nStatus));
	else if (strReply.empty())
		throw runtime_error("no response from server");

	// Parse reply
	json_spirit::Value valReply;
	if (!json_spirit::read_string(strReply, valReply))
		throw runtime_error("couldn't parse reply from server");
	const json_spirit::Object& reply = valReply.get_obj();
	if (reply.empty())
		throw runtime_error("expected reply to have result, error and id properties");

	return reply;
}
#endif

