//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================


#include <boost/asio.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "RPC.h"

// Used for logging
struct RPC
{
};

SETUP_LOG (RPC)

using namespace boost;
using namespace boost::asio;

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

    error["code"]       = Json::Value (code);
    error["message"]    = Json::Value (message);

    return error;
}

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string createHTTPPost (const std::string& strHost, const std::string& strPath, const std::string& strMsg, const std::map<std::string, std::string>& mapRequestHeaders)
{
    std::ostringstream s;

    s << "POST "
      << (strPath.empty () ? "/" : strPath)
      << " HTTP/1.0\r\n"
      << "User-Agent: " SYSTEM_NAME "-json-rpc/" << FormatFullVersion () << "\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size () << "\r\n"
      << "Accept: application/json\r\n";

    typedef std::map<std::string, std::string>::value_type HeaderType;

    BOOST_FOREACH (const HeaderType & item, mapRequestHeaders)
    s << item.first << ": " << item.second << "\r\n";

    s << "\r\n" << strMsg;

    return s.str ();
}

std::string rfc1123Time ()
{
    char buffer[64];
    time_t now;
    time (&now);
    struct tm* now_gmt = gmtime (&now);
    std::string locale (setlocale (LC_TIME, NULL));
    setlocale (LC_TIME, "C"); // we want posix (aka "C") weekday/month strings
    strftime (buffer, sizeof (buffer), "%a, %d %b %Y %H:%M:%S +0000", now_gmt);
    setlocale (LC_TIME, locale.c_str ());
    return std::string (buffer);
}

std::string HTTPReply (int nStatus, const std::string& strMsg)
{
    WriteLog (lsTRACE, RPC) << "HTTP Reply " << nStatus << " " << strMsg;

    if (nStatus == 401)
        return strprintf ("HTTP/1.0 401 Authorization Required\r\n"
                          "Date: %s\r\n"
                          "Server: " SYSTEM_NAME "-json-rpc/%s\r\n"
                          "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
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
                          "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
                          "</HTML>\r\n", rfc1123Time ().c_str (), FormatFullVersion ().c_str ());

    std::string strStatus;

    if (nStatus == 200) strStatus = "OK";
    else if (nStatus == 400) strStatus = "Bad Request";
    else if (nStatus == 403) strStatus = "Forbidden";
    else if (nStatus == 404) strStatus = "Not Found";
    else if (nStatus == 500) strStatus = "Internal Server Error";

    std::string access;

    if (theConfig.RPC_ALLOW_REMOTE) access = "Access-Control-Allow-Origin: *\r\n";
    else access = "";

    return strprintf (
               "HTTP/1.1 %d %s\r\n"
               "Date: %s\r\n"
               "Connection: Keep-Alive\r\n"
               "%s"
               "Content-Length: %d\r\n"
               "Content-Type: application/json; charset=UTF-8\r\n"
               "Server: " SYSTEM_NAME "-json-rpc/%s\r\n"
               "\r\n"
               "%s\r\n",
               nStatus,
               strStatus.c_str (),
               rfc1123Time ().c_str (),
               access.c_str (),
               strMsg.size () + 2,
               SERVER_VERSION,
               strMsg.c_str ());
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
        return theConfig.RPC_USER.empty () && theConfig.RPC_PASSWORD.empty ();

    std::string strUserPass64 = it->second.substr (6);
    boost::trim (strUserPass64);
    std::string strUserPass = DecodeBase64 (strUserPass64);
    std::string::size_type nColon = strUserPass.find (":");

    if (nColon == std::string::npos)
        return false;

    std::string strUser = strUserPass.substr (0, nColon);
    std::string strPassword = strUserPass.substr (nColon + 1);
    return (strUser == theConfig.RPC_USER) && (strPassword == theConfig.RPC_PASSWORD);
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
    request["method"] = strMethod;
    request["params"] = params;
    request["id"] = id;
    Json::FastWriter writer;
    return writer.write (request) + "\n";
}

std::string JSONRPCReply (const Json::Value& result, const Json::Value& error, const Json::Value& id)
{
    Json::Value reply (Json::objectValue);
    reply["result"] = result;
    //reply["error"]=error;
    //reply["id"]=id;
    Json::FastWriter writer;
    return writer.write (reply) + "\n";
}

void ErrorReply (std::ostream& stream, const Json::Value& objError, const Json::Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = 500;
    int code = objError["code"].asInt ();

    if (code == -32600) nStatus = 400;
    else if (code == -32601) nStatus = 404;

    std::string strReply = JSONRPCReply (Json::Value (), objError, id);
    stream << HTTPReply (nStatus, strReply) << std::flush;
}

// vim:ts=4
