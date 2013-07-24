//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPC_H_INCLUDED
#define RIPPLE_RPC_H_INCLUDED

// VFALCO TODO Wrap these up into a class. It looks like they just do some
//             convenience packaging of JSON data from the pieces. It looks
//             Ripple client protocol-specific.
//
extern std::string JSONRPCRequest (const std::string& strMethod, const Json::Value& params,
                                   const Json::Value& id);

extern std::string JSONRPCReply (const Json::Value& result, const Json::Value& error, const Json::Value& id);

extern Json::Value JSONRPCError (int code, const std::string& message);

extern std::string createHTTPPost (const std::string& strHost, const std::string& strPath, const std::string& strMsg,
                                   const std::map<std::string, std::string>& mapRequestHeaders);

extern std::string HTTPReply (int nStatus, const std::string& strMsg);

// VFALCO TODO Create a HTTPHeaders class with a nice interface instead of the std::map
//
extern bool HTTPAuthorized (std::map <std::string, std::string> const& mapHeaders);

// VFALCO NOTE This one looks like it does some sort of stream i/o
//
extern int ReadHTTP (std::basic_istream<char>& stream,
                     std::map<std::string, std::string>& mapHeadersRet,
                     std::string& strMessageRet);

#endif
