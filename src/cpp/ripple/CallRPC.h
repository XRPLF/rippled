#ifndef __CALLRPC__
#define __CALLRPC__


#include <string>

#include "../json/value.h"

class RPCParser
{
protected:
	typedef Json::Value (RPCParser::*parseFuncPtr)(Json::Value jvRet, const Json::Value &jvParams);

	Json::Value parseAsIs(Json::Value jvRet, const Json::Value &jvParams);

public:
	Json::Value parseCommand(Json::Value jvRequest);
};

extern int commandLineRPC(const std::vector<std::string>& vCmd);
extern Json::Value callRPC(const std::string& strMethod, const Json::Value& params);

#endif

// vim:ts=4
