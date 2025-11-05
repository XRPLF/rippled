#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/Output.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/server/detail/JSONRPCUtil.h>

#include <ctime>
#include <string>

namespace ripple {

std::string
getHTTPHeaderTimestamp()
{
    // CHECKME This is probably called often enough that optimizing it makes
    //         sense. There's no point in doing all this work if this function
    //         gets called multiple times a second.
    char buffer[96];
    time_t now;
    time(&now);
    struct tm now_gmt
    {
    };
#ifndef _MSC_VER
    gmtime_r(&now, &now_gmt);
#else
    gmtime_s(&now_gmt, &now);
#endif
    strftime(
        buffer,
        sizeof(buffer),
        "Date: %a, %d %b %Y %H:%M:%S +0000\r\n",
        &now_gmt);
    return std::string(buffer);
}

void
HTTPReply(
    int nStatus,
    std::string const& content,
    Json::Output const& output,
    beast::Journal j)
{
    JLOG(j.trace()) << "HTTP Reply " << nStatus << " " << content;

    if (content.empty() && nStatus == 401)
    {
        output("HTTP/1.0 401 Authorization Required\r\n");
        output(getHTTPHeaderTimestamp());

        // CHECKME this returns a different version than the replies below. Is
        //         this by design or an accident or should it be using
        //         BuildInfo::getFullVersionString () as well?
        output("Server: " + systemName() + "-json-rpc/v1");
        output("\r\n");

        // Be careful in modifying this! If you change the contents you MUST
        // update the Content-Length header as well to indicate the correct
        // size of the data.
        output(
            "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 296\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 "
            "Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd"
            "\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' "
            "CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n");

        return;
    }

    switch (nStatus)
    {
        case 200:
            output("HTTP/1.1 200 OK\r\n");
            break;
        case 202:
            output("HTTP/1.1 202 Accepted\r\n");
            break;
        case 400:
            output("HTTP/1.1 400 Bad Request\r\n");
            break;
        case 401:
            output("HTTP/1.1 401 Authorization Required\r\n");
            break;
        case 403:
            output("HTTP/1.1 403 Forbidden\r\n");
            break;
        case 404:
            output("HTTP/1.1 404 Not Found\r\n");
            break;
        case 405:
            output("HTTP/1.1 405 Method Not Allowed\r\n");
            break;
        case 429:
            output("HTTP/1.1 429 Too Many Requests\r\n");
            break;
        case 500:
            output("HTTP/1.1 500 Internal Server Error\r\n");
            break;
        case 501:
            output("HTTP/1.1 501 Not Implemented\r\n");
            break;
        case 503:
            output("HTTP/1.1 503 Server is overloaded\r\n");
            break;
    }

    output(getHTTPHeaderTimestamp());

    output(
        "Connection: Keep-Alive\r\n"
        "Content-Length: ");

    // VFALCO TODO Determine if/when this header should be added
    // if (context.app.config().RPC_ALLOW_REMOTE)
    //    output ("Access-Control-Allow-Origin: *\r\n");

    output(std::to_string(content.size() + 2));
    output(
        "\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n");

    output("Server: " + systemName() + "-json-rpc/");
    output(BuildInfo::getFullVersionString());
    output(
        "\r\n"
        "\r\n");
    output(content);
    output("\r\n");
}

}  // namespace ripple
