//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/common/jsonrpc_fields.h>
#include <ripple/data/protocol/BuildInfo.h>
#include <ripple/core/SystemParameters.h>
#include <beast/crypto/base64.h>

namespace ripple {

namespace {

unsigned int const gMaxHTTPHeaderSize = 0x02000000;

std::string const versionNumber = "v1";

std::string getHTTPHeaderTimestamp ()
{
    // CHECKME This is probably called often enough that optimizing it makes
    //         sense. There's no point in doing all this work if this function
    //         gets called multiple times a second.
    char buffer[96];
    time_t now;
    time (&now);
    struct tm* now_gmt = gmtime (&now);
    strftime (buffer, sizeof (buffer),
        "Date: %a, %d %b %Y %H:%M:%S +0000\r\n",
        now_gmt);
    return std::string (buffer);
}

} // namespace

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string createHTTPPost (
    std::string const& strHost,
    std::string const& strPath,
    std::string const& strMsg,
    HTTPHeaders const& mapRequestHeaders)
{
    std::string s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s += "POST ";
    s += strPath.empty () ? "/" : strPath;
    s += " HTTP/1.0\r\n"
            "User-Agent: " SYSTEM_NAME "-json-rpc/";
    s += versionNumber;
    s += "\r\n"
            "Host: ";
    s += strHost;
    s += "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: ";
    s += std::to_string (strMsg.size ());
    s += "\r\n"
            "Accept: application/json\r\n";

    for (auto const& item : mapRequestHeaders)
        (((s += item.first) += ": ") += item.second) += "\r\n";

    (s += "\r\n") += strMsg;

    return s;
}

std::string HTTPReply (int nStatus, std::string const& strMsg)
{
    if (ShouldLog (lsTRACE, RPC))
    {
        WriteLog (lsTRACE, RPC) << "HTTP Reply " << nStatus << " " << strMsg;
    }

    std::string ret;

    if (nStatus == 401)
    {
        ret.reserve (512);

        ret.append ("HTTP/1.0 401 Authorization Required\r\n");
        ret.append (getHTTPHeaderTimestamp ());

        // CHECKME this returns a different version than the replies below. Is
        //         this by design or an accident or should it be using
        //         BuildInfo::getFullVersionString () as well?
        ret.append ("Server: " SYSTEM_NAME "-json-rpc/");
        ret.append (versionNumber);
        ret.append ("\r\n");

        // Be careful in modifying this! If you change the contents you MUST
        // update the Content-Length header as well to indicate the correct
        // size of the data.
        ret.append (
            "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 296\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN"
            "\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; "
            "charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n");
    }
    else
    {

        ret.reserve(256 + strMsg.length());

        switch (nStatus)
        {
        case 200: ret.append ("HTTP/1.1 200 OK\r\n"); break;
        case 400: ret.append ("HTTP/1.1 400 Bad Request\r\n"); break;
        case 403: ret.append ("HTTP/1.1 403 Forbidden\r\n"); break;
        case 404: ret.append ("HTTP/1.1 404 Not Found\r\n"); break;
        case 500: ret.append ("HTTP/1.1 500 Internal Server Error\r\n"); break;
        }

        ret.append (getHTTPHeaderTimestamp ());

        ret.append ("Connection: Keep-Alive\r\n");

        if (getConfig ().RPC_ALLOW_REMOTE)
            ret.append ("Access-Control-Allow-Origin: *\r\n");

        ret.append ("Content-Length: ");
        ret.append (std::to_string(strMsg.size () + 2));
        ret.append ("\r\n"
                    "Content-Type: application/json; charset=UTF-8\r\n"
                    "Server: " SYSTEM_NAME "-json-rpc/");
        ret.append (BuildInfo::getFullVersionString ());
        ret.append ("\r\n"
                    "\r\n");
        ret.append (strMsg);
        ret.append ("\r\n");
    }

    return ret;
}

bool HTTPAuthorized (HTTPHeaders const& mapHeaders)
{
    bool const credentialsRequired (! getConfig().RPC_USER.empty() &&
        ! getConfig().RPC_PASSWORD.empty());
    if (! credentialsRequired)
        return true;

    auto const it = mapHeaders.find ("authorization");
    if ((it == mapHeaders.end ()) || (it->second.substr (0, 6) != "Basic "))
        return false;

    std::string strUserPass64 = it->second.substr (6);
    boost::trim (strUserPass64);
    std::string strUserPass = beast::base64_decode (strUserPass64);
    std::string::size_type nColon = strUserPass.find (":");

    if (nColon == std::string::npos)
        return false;

    std::string strUser = strUserPass.substr (0, nColon);
    std::string strPassword = strUserPass.substr (nColon + 1);
    return strUser == getConfig ().RPC_USER &&
            strPassword == getConfig ().RPC_PASSWORD;
}

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

static std::string toStringWithCarriageReturn (Json::Value const& value)
{
    auto s = to_string (value);
    s += '\n';
    return s;
}

std::string JSONRPCRequest (
    std::string const& method, Json::Value const& params, Json::Value const& id)
{
    Json::Value request;
    request[jss::method] = method;
    request[jss::params] = params;
    request[jss::id] = id;

    return toStringWithCarriageReturn (request);
}

std::string JSONRPCReply (
    Json::Value const& result, Json::Value const& error, Json::Value const& id)
{
    Json::Value reply (Json::objectValue);
    reply[jss::result] = result;

    return toStringWithCarriageReturn (reply);
}

} // ripple
