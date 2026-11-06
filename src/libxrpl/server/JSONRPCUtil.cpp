#include <xrpl/server/detail/JSONRPCUtil.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/Output.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/SystemParameters.h>

#include <boost/beast/http/status.hpp>

#include <ctime>
#include <format>
#include <string>

namespace xrpl {

std::string
getHTTPHeaderTimestamp()
{
    // CHECKME This is probably called often enough that optimizing it makes
    //         sense. There's no point in doing all this work if this function
    //         gets called multiple times a second.
    char buffer[96];
    time_t now = 0;
    time(&now);
    struct tm nowGmt{};
#ifndef _MSC_VER
    gmtime_r(&now, &nowGmt);
#else
    gmtime_s(&nowGmt, &now);
#endif
    strftime(buffer, sizeof(buffer), "Date: %a, %d %b %Y %H:%M:%S +0000\r\n", &nowGmt);
    return std::string(buffer);
}

void
httpReply(int nStatus, std::string const& content, json::Output const& output, beast::Journal j)
{
    // The status only. The body is a reply, which carries a credential when the
    // command is a keygen, and this library cannot mask one: the caller logs the
    // masked body at debug.
    JLOG(j.trace()) << "HTTP Reply " << nStatus;

    if (content.empty() && nStatus == 401)
    {
        output("HTTP/1.0 401 Authorization Required\r\n");
        output(getHTTPHeaderTimestamp());

        // CHECKME this returns a different version than the replies below. Is
        //         this by design or an accident or should it be using
        //         build_info::getFullVersionString () as well?
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
        // The status every successful reply carries, so a literal rather than a format call.
        case 200:
            output("HTTP/1.1 200 OK\r\n");
            break;
        // Two statuses this server phrases itself rather than taking from the registry.
        case 401:
            output("HTTP/1.1 401 Authorization Required\r\n");
            break;
        case 503:
            output("HTTP/1.1 503 Server is overloaded\r\n");
            break;
        default:
            // A reply whose first line is a header is not an HTTP response. Beast knows the whole
            // registry, so a status the error table gains needs no case here.
            output(
                std::format(
                    "HTTP/1.1 {} {}\r\n",
                    nStatus,
                    boost::beast::http::obsolete_reason(
                        static_cast<boost::beast::http::status>(nStatus))));
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
    output(build_info::getFullVersionString());
    output(
        "\r\n"
        "\r\n");
    output(content);
    output("\r\n");
}

}  // namespace xrpl
