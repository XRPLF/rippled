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

#ifndef RIPPLE_NET_BASICS_HTTPREQUEST_H_INCLUDED
#define RIPPLE_NET_BASICS_HTTPREQUEST_H_INCLUDED

namespace ripple {

/** An HTTP request we are handling from a client. */
class HTTPRequest
{
public:
    enum Action
    {
        // What the application code needs to do
        haERROR         = 0,
        haREAD_LINE     = 1,
        haREAD_RAW      = 2,
        haDO_REQUEST    = 3,
        haCLOSE_CONN    = 4
    };

    HTTPRequest () : eState (await_request), iDataSize (0), bShouldClose (true)
    {
        ;
    }
    void reset ();

    std::string& peekBody ()
    {
        return sRequestBody;
    }
    std::string getBody ()
    {
        return sRequestBody;
    }
    std::string& peekRequest ()
    {
        return sRequest;
    }
    std::string getRequest ()
    {
        return sRequest;
    }
    std::string& peekAuth ()
    {
        return sAuthorization;
    }
    std::string getAuth ()
    {
        return sAuthorization;
    }

    std::map<std::string, std::string>& peekHeaders ()
    {
        return mHeaders;
    }
    std::string getReplyHeaders (bool forceClose);

    Action consume (boost::asio::streambuf&);
    Action requestDone (bool forceClose); // call after reply is sent

    int getDataSize ()
    {
        return iDataSize;
    }

private:
    enum state
    {
        await_request,  // We are waiting for the request line
        await_header,   // We are waiting for request headers
        getting_body,   // We are waiting for the body
        do_request,     // We are waiting for the request to complete
    };

    state eState;
    std::string sRequest;           // VERB URL PROTO
    std::string sRequestBody;
    std::string sAuthorization;

    std::map<std::string, std::string> mHeaders;

    int iDataSize;
    bool bShouldClose;
};

}

#endif
