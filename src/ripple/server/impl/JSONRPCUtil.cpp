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

#include <ripple/basics/Log.h>
#include <ripple/server/impl/JSONRPCUtil.h>
#include <ripple/common/jsonrpc_fields.h>
#include <ripple/data/protocol/BuildInfo.h>
#include <ripple/core/SystemParameters.h>
#include <boost/algorithm/string.hpp>

namespace ripple {

unsigned int const gMaxHTTPHeaderSize = 0x02000000;

Json::Value JSONRPCError (int code, std::string const& message)
{
    Json::Value error (Json::objectValue);

    error[jss::code]       = Json::Value (code);
    error[jss::message]    = Json::Value (message);

    return error;
}

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
        ret.append ("Server: " SYSTEM_NAME "-json-rpc/v1");
        ret.append ("\r\n");

        // Be careful in modifying this! If you change the contents you MUST
        // update the Content-Length header as well to indicate the correct
        // size of the data.
        ret.append ("WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: 296\r\n"
                    "\r\n"
                    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
                    "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
                    "<HTML>\r\n"
                    "<HEAD>\r\n"
                    "<TITLE>Error</TITLE>\r\n"
                    "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
                    "</HEAD>\r\n"
                    "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n");

        return ret;
    }

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

    // VFALCO TODO Determine if/when this header should be added
    //if (getConfig ().RPC_ALLOW_REMOTE)
    //    ret.append ("Access-Control-Allow-Origin: *\r\n");

    ret.append ("Content-Length: ");
    ret.append (std::to_string(strMsg.size () + 2));
    ret.append ("\r\n");

    ret.append ("Content-Type: application/json; charset=UTF-8\r\n");

    ret.append ("Server: " SYSTEM_NAME "-json-rpc/");
    ret.append (BuildInfo::getFullVersionString ());
    ret.append ("\r\n");

    ret.append ("\r\n");
    ret.append (strMsg);
    ret.append ("\r\n");

    return ret;
}

int ReadHTTPStatus (std::basic_istream<char>& stream)
{
    std::string str;
    getline (stream, str);
    std::vector<std::string> vWords;
    boost::split (vWords, str, boost::is_any_of (" "));

    if (vWords.size () < 2)
        return 500;

    return atoi (vWords[1].c_str ());
}

int ReadHTTPHeader (std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet)
{
    int nLen = 0;

    for (;;)
    {
        std::string str;
        std::getline (stream, str);

        if (str.empty () || str == "\r")
            break;

        std::string::size_type nColon = str.find (":");

        if (nColon != std::string::npos)
        {
            std::string strHeader = str.substr (0, nColon);
            boost::trim (strHeader);
            boost::to_lower (strHeader);
            std::string strValue = str.substr (nColon + 1);
            boost::trim (strValue);
            mapHeadersRet[strHeader] = strValue;

            if (strHeader == "content-length")
                nLen = atoi (strValue.c_str ());
        }
    }

    return nLen;
}

int ReadHTTP (std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet,
              std::string& strMessageRet)
{
    mapHeadersRet.clear ();
    strMessageRet = "";

    // Read status
    int nStatus = ReadHTTPStatus (stream);

    // Read header
    int nLen = ReadHTTPHeader (stream, mapHeadersRet);

    if (nLen < 0 || nLen > gMaxHTTPHeaderSize)
        return 500;

    // Read message
    if (nLen > 0)
    {
        std::vector<char> vch (nLen);
        stream.read (&vch[0], nLen);
        strMessageRet = std::string (vch.begin (), vch.end ());
    }

    return nStatus;
}

std::string JSONRPCReply (Json::Value const& result, Json::Value const& error, Json::Value const& id)
{
    Json::Value reply (Json::objectValue);
    reply[jss::result] = result;
    //reply["error"]=error;
    //reply["id"]=id;
    return to_string (reply) + "\n";
}

} // ripple
