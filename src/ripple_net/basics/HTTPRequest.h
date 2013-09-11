//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_BASICS_HTTPREQUEST_H_INCLUDED
#define RIPPLE_NET_BASICS_HTTPREQUEST_H_INCLUDED

/** An HTTP request we are handling from a client.
*/
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

#endif
