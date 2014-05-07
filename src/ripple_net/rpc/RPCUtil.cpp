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

namespace ripple {

// Used for logging
struct RPCLog;

SETUP_LOGN (RPCLog, "RPC")

unsigned int const gMaxHTTPHeaderSize = 0x02000000;

std::string gFormatStr ("v1");

// VFALCO TODO clean up this nonsense
std::string FormatFullVersion ()
{
    return (gFormatStr);
}


Json::Value JSONRPCError (int code, const std::string& message)
{
    Json::Value error (Json::objectValue);

    error[jss::code]       = Json::Value (code);
    error[jss::message]    = Json::Value (message);

    return error;
}

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
    std::map<std::string, std::string> const& mapRequestHeaders)
{
    std::ostringstream s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s << "POST "
      << (strPath.empty () ? "/" : strPath)
      << " HTTP/1.0\r\n"
      << "User-Agent: " SYSTEM_NAME "-json-rpc/" << FormatFullVersion () << "\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size () << "\r\n"
      << "Accept: application/json\r\n";

    for (auto const& item : mapRequestHeaders)
        s << item.first << ": " << item.second << "\r\n";

    s << "\r\n" << strMsg;

    return s.str ();
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
    std::string locale (setlocale (LC_TIME, nullptr));
    setlocale (LC_TIME, "C"); // we want posix (aka "C") weekday/month strings
    strftime (buffer, sizeof (buffer), 
        "Date: %a, %d %b %Y %H:%M:%S +0000\r\n", 
        now_gmt);
    setlocale (LC_TIME, locale.c_str ());
    return std::string (buffer);
}

std::string HTTPReply (int nStatus, const std::string& strMsg)
{
    if (ShouldLog (lsTRACE, RPCLog))
    {
        WriteLog (lsTRACE, RPCLog) << "HTTP Reply " << nStatus << " " << strMsg;
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
        ret.append (FormatFullVersion ());
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

    if (getConfig ().RPC_ALLOW_REMOTE)
        ret.append ("Access-Control-Allow-Origin: *\r\n");
    
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

std::string DecodeBase64 (std::string s)
{
    // FIXME: This performs badly
    BIO* b64, *bmem;

    char* buffer = static_cast<char*> (calloc (s.size (), sizeof (char)));

    b64 = BIO_new (BIO_f_base64 ());
    BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf (const_cast<char*> (s.data ()), s.size ());
    bmem = BIO_push (b64, bmem);
    BIO_read (bmem, buffer, s.size ());
    BIO_free_all (bmem);

    std::string result (buffer);
    free (buffer);
    return result;
}

bool HTTPAuthorized (const std::map<std::string, std::string>& mapHeaders)
{
    std::map<std::string, std::string>::const_iterator it = mapHeaders.find ("authorization");

    if ((it == mapHeaders.end ()) || (it->second.substr (0, 6) != "Basic "))
        return getConfig ().RPC_USER.empty () && getConfig ().RPC_PASSWORD.empty ();

    std::string strUserPass64 = it->second.substr (6);
    boost::trim (strUserPass64);
    std::string strUserPass = DecodeBase64 (strUserPass64);
    std::string::size_type nColon = strUserPass.find (":");

    if (nColon == std::string::npos)
        return false;

    std::string strUser = strUserPass.substr (0, nColon);
    std::string strPassword = strUserPass.substr (nColon + 1);
    return (strUser == getConfig ().RPC_USER) && (strPassword == getConfig ().RPC_PASSWORD);
}

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

std::string JSONRPCRequest (const std::string& strMethod, const Json::Value& params, const Json::Value& id)
{
    Json::Value request;
    request[jss::method] = strMethod;
    request[jss::params] = params;
    request[jss::id] = id;
    Json::FastWriter writer;
    return writer.write (request) + "\n";
}

std::string JSONRPCReply (const Json::Value& result, const Json::Value& error, const Json::Value& id)
{
    Json::Value reply (Json::objectValue);
    reply[jss::result] = result;
    //reply["error"]=error;
    //reply["id"]=id;
    Json::FastWriter writer;
    return writer.write (reply) + "\n";
}

void ErrorReply (std::ostream& stream, const Json::Value& objError, const Json::Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = 500;
    int code = objError[jss::code].asInt ();

    if (code == -32600) nStatus = 400;
    else if (code == -32601) nStatus = 404;

    std::string strReply = JSONRPCReply (Json::Value (), objError, id);
    stream << HTTPReply (nStatus, strReply) << std::flush;
}

} // ripple

