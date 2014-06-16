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

#include <beast/module/core/text/LexicalCast.h>

#include <string>

namespace ripple {

SETUP_LOG (HTTPRequest)

// Logic to handle incoming HTTP reqests

void HTTPRequest::reset ()
{
    mHeaders.clear ();
    sRequestBody.clear ();
    sAuthorization.clear ();
    iDataSize = 0;
    bShouldClose = true;
    eState = await_request;
}

HTTPRequest::Action HTTPRequest::requestDone (bool forceClose)
{
    if (forceClose || bShouldClose)
        return haCLOSE_CONN;

    reset ();
    return haREAD_LINE;
}

std::string HTTPRequest::getReplyHeaders (bool forceClose)
{
    if (forceClose || bShouldClose)
        return "Connection: close\r\n";
    else
        return "Connection: Keep-Alive\r\n";
}

HTTPRequest::Action HTTPRequest::consume (boost::asio::streambuf& buf)
{
    std::string line;
    std::istream is (&buf);
    std::getline (is, line);
    boost::trim (line);

    //  WriteLog (lsTRACE, HTTPRequest) << "HTTPRequest line: " << line;

    if (eState == await_request)
    {
        // VERB URL PROTO
        if (line.empty ())
            return haREAD_LINE;

        sRequest = line;
        bShouldClose = sRequest.find ("HTTP/1.1") == std::string::npos;

        eState = await_header;
        return haREAD_LINE;
    }

    if (eState == await_header)
    {
        // HEADER_NAME: HEADER_BODY
        if (line.empty ()) // empty line or bare \r
        {
            if (iDataSize == 0)
            {
                // no body
                eState = do_request;
                return haDO_REQUEST;
            }

            eState = getting_body;
            return haREAD_RAW;
        }

        size_t colon = line.find (':');

        if (colon != std::string::npos)
        {
            std::string headerName = line.substr (0, colon);
            boost::trim (headerName);
            boost::to_lower (headerName);

            std::string headerValue = line.substr (colon + 1);
            boost::trim (headerValue);

            mHeaders[headerName] += headerValue;

            if (headerName == "connection")
            {
                boost::to_lower (headerValue);

                if ((headerValue == "keep-alive") || (headerValue == "keepalive"))
                    bShouldClose = false;

                if (headerValue == "close")
                    bShouldClose = true;
            }

            if (headerName == "content-length")
                iDataSize = beast::lexicalCastThrow <int> (headerValue);

            if (headerName == "authorization")
                sAuthorization = headerValue;
        }

        return haREAD_LINE;
    }

    assert (false);
    return haERROR;
}

} // ripple
