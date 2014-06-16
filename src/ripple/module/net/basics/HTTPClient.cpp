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

#include <boost/regex.hpp>
#include <beast/asio/placeholders.h>
    
namespace ripple {

//
// Fetch a web page via http or https.
//

SETUP_LOG (HTTPClient)

//------------------------------------------------------------------------------

class HTTPClientSSLContext
{
public:
    HTTPClientSSLContext ()
        : m_context (boost::asio::ssl::context::sslv23)
    {
        boost::system::error_code ec;

        if (getConfig().SSL_VERIFY_FILE.empty ())
        {
            m_context.set_default_verify_paths (ec);

            if (ec && getConfig().SSL_VERIFY_DIR.empty ())
                throw std::runtime_error (boost::str (
                    boost::format ("Failed to set_default_verify_paths: %s") % ec.message ()));
        }
        else
        {
            m_context.load_verify_file (getConfig().SSL_VERIFY_FILE);
        }

        if (! getConfig().SSL_VERIFY_DIR.empty ())
        {
            m_context.add_verify_path (getConfig().SSL_VERIFY_DIR, ec);

            if (ec)
                throw std::runtime_error (boost::str (
                    boost::format ("Failed to add verify path: %s") % ec.message ()));
        }
    }

    boost::asio::ssl::context& context()
    {
        return m_context;
    }

private:
    boost::asio::ssl::context m_context;
};

//------------------------------------------------------------------------------

// VFALCO NOTE I moved the SSL_CONTEXT out of the Config and into this
//             singleton to eliminate the asio dependency in the headers.
//
void HTTPClient::initializeSSLContext ()
{
    beast::SharedSingleton <HTTPClientSSLContext>::get();
}

//------------------------------------------------------------------------------

class HTTPClientImp
    : public std::enable_shared_from_this <HTTPClientImp>
    , public HTTPClient
    , beast::LeakChecked <HTTPClientImp>
{
public:
    HTTPClientImp (boost::asio::io_service& io_service,
                              const unsigned short port,
                              std::size_t responseMax)
        : mSocket (io_service, beast::SharedSingleton <HTTPClientSSLContext>::get()->context())
        , mResolver (io_service)
        , mHeader (maxClientHeaderBytes)
        , mPort (port)
        , mResponseMax (responseMax)
        , mDeadline (io_service)
    {
        if (!getConfig ().SSL_VERIFY)
            mSocket.SSLSocket ().set_verify_mode (boost::asio::ssl::verify_none);
    }

    //--------------------------------------------------------------------------

    void makeGet (const std::string& strPath, boost::asio::streambuf& sb,
        const std::string& strHost)
    {
        std::ostream    osRequest (&sb);

        osRequest <<
                  "GET " << strPath << " HTTP/1.0\r\n"
                  "Host: " << strHost << "\r\n"
                  "Accept: */*\r\n"                       // YYY Do we need this line?
                  "Connection: close\r\n\r\n";
    }

    //--------------------------------------------------------------------------

    void request (
        bool bSSL,
        std::deque<std::string> deqSites,
        std::function<void (boost::asio::streambuf& sb, const std::string& strHost)> build,
        boost::posix_time::time_duration timeout,
        std::function<bool (const boost::system::error_code& ecResult,
            int iStatus, const std::string& strData)> complete)
    {
        mSSL        = bSSL;
        mDeqSites   = deqSites;
        mBuild      = build;
        mComplete   = complete;
        mTimeout    = timeout;

        httpsNext ();
    }

    //--------------------------------------------------------------------------

    void get (
        bool bSSL,
        std::deque<std::string> deqSites,
        const std::string& strPath,
        boost::posix_time::time_duration timeout,
        std::function<bool (const boost::system::error_code& ecResult, int iStatus,
            const std::string& strData)> complete)
    {

        mComplete   = complete;
        mTimeout    = timeout;

        request (
            bSSL,
            deqSites,
            std::bind (&HTTPClientImp::makeGet, shared_from_this (), strPath,
                       std::placeholders::_1, std::placeholders::_2),
            timeout,
            complete);
    }

    //--------------------------------------------------------------------------

    void httpsNext ()
    {
        WriteLog (lsTRACE, HTTPClient) << "Fetch: " << mDeqSites[0];

        std::shared_ptr <boost::asio::ip::tcp::resolver::query> query (
            new boost::asio::ip::tcp::resolver::query (
                mDeqSites[0],
                beast::lexicalCast <std::string> (mPort),
                boost::asio::ip::resolver_query_base::numeric_service));
        mQuery  = query;

        mDeadline.expires_from_now (mTimeout, mShutdown);

        WriteLog (lsTRACE, HTTPClient) << "expires_from_now: " << mShutdown.message ();

        if (!mShutdown)
        {
            mDeadline.async_wait (
                std::bind (
                    &HTTPClientImp::handleDeadline,
                    shared_from_this (),
                    beast::asio::placeholders::error));
        }

        if (!mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Resolving: " << mDeqSites[0];

            mResolver.async_resolve (*mQuery,
                                     std::bind (
                                         &HTTPClientImp::handleResolve,
                                         shared_from_this (),
                                         beast::asio::placeholders::error,
                                         beast::asio::placeholders::iterator));
        }

        if (mShutdown)
            invokeComplete (mShutdown);
    }

    void handleDeadline (const boost::system::error_code& ecResult)
    {
        if (ecResult == boost::asio::error::operation_aborted)
        {
            // Timer canceled because deadline no longer needed.
            WriteLog (lsTRACE, HTTPClient) << "Deadline cancelled.";

            // Aborter is done.
        }
        else if (ecResult)
        {
            WriteLog (lsTRACE, HTTPClient) << "Deadline error: " << mDeqSites[0] << ": " << ecResult.message ();

            // Can't do anything sound.
            abort ();
        }
        else
        {
            WriteLog (lsTRACE, HTTPClient) << "Deadline arrived.";

            // Mark us as shutting down.
            // XXX Use our own error code.
            mShutdown   = boost::system::error_code (boost::system::errc::bad_address, boost::system::system_category ());

            // Cancel any resolving.
            mResolver.cancel ();

            // Stop the transaction.
            mSocket.async_shutdown (std::bind (
                                        &HTTPClientImp::handleShutdown,
                                        shared_from_this (),
                                        beast::asio::placeholders::error));

        }
    }

    void handleShutdown (
        const boost::system::error_code& ecResult
    )
    {
        if (ecResult)
        {
            WriteLog (lsTRACE, HTTPClient) << "Shutdown error: " << mDeqSites[0] << ": " << ecResult.message ();
        }
    }

    void handleResolve (
        const boost::system::error_code& ecResult,
        boost::asio::ip::tcp::resolver::iterator itrEndpoint
    )
    {
        if (!mShutdown)
            mShutdown   = ecResult;

        if (mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Resolve error: " << mDeqSites[0] << ": " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            WriteLog (lsTRACE, HTTPClient) << "Resolve complete.";

            boost::asio::async_connect (
                mSocket.lowest_layer (),
                itrEndpoint,
                std::bind (
                    &HTTPClientImp::handleConnect,
                    shared_from_this (),
                    beast::asio::placeholders::error));
        }
    }

    void handleConnect (const boost::system::error_code& ecResult)
    {
        if (!mShutdown)
            mShutdown   = ecResult;

        if (mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Connect error: " << mShutdown.message ();
        }

        if (!mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Connected.";

            if (getConfig ().SSL_VERIFY)
            {
                mShutdown   = mSocket.verify (mDeqSites[0]);

                if (mShutdown)
                {
                    WriteLog (lsTRACE, HTTPClient) << "set_verify_callback: " << mDeqSites[0] << ": " << mShutdown.message ();
                }
            }
        }

        if (mShutdown)
        {
            invokeComplete (mShutdown);
        }
        else if (mSSL)
        {
            mSocket.async_handshake (
                AutoSocket::ssl_socket::client,
                std::bind (
                    &HTTPClientImp::handleRequest,
                    shared_from_this (),
                    beast::asio::placeholders::error));
        }
        else
        {
            handleRequest (ecResult);
        }
    }

    void handleRequest (const boost::system::error_code& ecResult)
    {
        if (!mShutdown)
            mShutdown   = ecResult;

        if (mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Handshake error:" << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            WriteLog (lsTRACE, HTTPClient) << "Session started.";

            mBuild (mRequest, mDeqSites[0]);

            mSocket.async_write (
                mRequest,
                std::bind (&HTTPClientImp::handleWrite,
                             shared_from_this (),
                             beast::asio::placeholders::error,
                             beast::asio::placeholders::bytes_transferred));
        }
    }

    void handleWrite (const boost::system::error_code& ecResult, std::size_t bytes_transferred)
    {
        if (!mShutdown)
            mShutdown   = ecResult;

        if (mShutdown)
        {
            WriteLog (lsTRACE, HTTPClient) << "Write error: " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            WriteLog (lsTRACE, HTTPClient) << "Wrote.";

            mSocket.async_read_until (
                mHeader,
                "\r\n\r\n",
                std::bind (&HTTPClientImp::handleHeader,
                             shared_from_this (),
                             beast::asio::placeholders::error,
                             beast::asio::placeholders::bytes_transferred));
        }
    }

    void handleHeader (const boost::system::error_code& ecResult, std::size_t bytes_transferred)
    {
        std::string     strHeader ((std::istreambuf_iterator<char> (&mHeader)), std::istreambuf_iterator<char> ());
        WriteLog (lsTRACE, HTTPClient) << "Header: \"" << strHeader << "\"";

        static boost::regex reStatus ("\\`HTTP/1\\S+ (\\d{3}) .*\\'");          // HTTP/1.1 200 OK
        static boost::regex reSize ("\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'");
        static boost::regex reBody ("\\`.*\\r\\n\\r\\n(.*)\\'");

        boost::smatch   smMatch;

        bool    bMatch  = boost::regex_match (strHeader, smMatch, reStatus);    // Match status code.

        if (!bMatch)
        {
            // XXX Use our own error code.
            WriteLog (lsTRACE, HTTPClient) << "No status code";
            invokeComplete (boost::system::error_code (boost::system::errc::bad_address, boost::system::system_category ()));
            return;
        }

        mStatus = beast::lexicalCastThrow <int> (std::string (smMatch[1]));

        if (boost::regex_match (strHeader, smMatch, reBody)) // we got some body
            mBody = smMatch[1];

        if (boost::regex_match (strHeader, smMatch, reSize))
        {
            int size = beast::lexicalCastThrow <int> (std::string(smMatch[1]));

            if (size < mResponseMax)
                mResponseMax = size;
        }

        if (mResponseMax == 0)
        {
            // no body wanted or available
            invokeComplete (ecResult, mStatus);
        }
        else if (mBody.size () >= mResponseMax)
        {
            // we got the whole thing
            invokeComplete (ecResult, mStatus, mBody);
        }
        else
        {
            mSocket.async_read (
                mResponse.prepare (mResponseMax - mBody.size ()),
                boost::asio::transfer_all (),
                std::bind (&HTTPClientImp::handleData,
                             shared_from_this (),
                             beast::asio::placeholders::error,
                             beast::asio::placeholders::bytes_transferred));
        }
    }

    void handleData (const boost::system::error_code& ecResult, std::size_t bytes_transferred)
    {
        if (!mShutdown)
            mShutdown   = ecResult;

        if (mShutdown && mShutdown != boost::asio::error::eof)
        {
            WriteLog (lsTRACE, HTTPClient) << "Read error: " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            if (mShutdown)
            {
                WriteLog (lsTRACE, HTTPClient) << "Complete.";
            }
            else
            {
                mResponse.commit (bytes_transferred);
                std::string strBody ((std::istreambuf_iterator<char> (&mResponse)), std::istreambuf_iterator<char> ());
                invokeComplete (ecResult, mStatus, mBody + strBody);
            }
        }
    }

    // Call cancel the deadline timer and invoke the completion routine.
    void invokeComplete (const boost::system::error_code& ecResult, int iStatus = 0, const std::string& strData = "")
    {
        boost::system::error_code ecCancel;

        (void) mDeadline.cancel (ecCancel);

        if (ecCancel)
        {
            WriteLog (lsTRACE, HTTPClient) << "invokeComplete: Deadline cancel error: " << ecCancel.message ();
        }

        WriteLog (lsDEBUG, HTTPClient) << "invokeComplete: Deadline popping: " << mDeqSites.size ();

        if (!mDeqSites.empty ())
        {
            mDeqSites.pop_front ();
        }

        bool    bAgain  = true;

        if (mDeqSites.empty () || !ecResult)
        {
            // ecResult: !0 = had an error, last entry
            //    iStatus: result, if no error
            //  strData: data, if no error
            bAgain  = mComplete && mComplete (ecResult ? ecResult : ecCancel, iStatus, strData);
        }

        if (!mDeqSites.empty () && bAgain)
        {
            httpsNext ();
        }
    }

    static bool onSMSResponse (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)
    {
        WriteLog (lsINFO, HTTPClient) << "SMS: Response:" << iStatus << " :" << strData;

        return true;
    }

private:
    typedef std::shared_ptr<HTTPClient> pointer;

    bool                                                        mSSL;
    AutoSocket                                                  mSocket;
    boost::asio::ip::tcp::resolver                              mResolver;
    std::shared_ptr<boost::asio::ip::tcp::resolver::query>    mQuery;
    boost::asio::streambuf                                      mRequest;
    boost::asio::streambuf                                      mHeader;
    boost::asio::streambuf                                      mResponse;
    std::string                                                 mBody;
    const unsigned short                                        mPort;
    int                                                         mResponseMax;
    int                                                         mStatus;
    std::function<void (boost::asio::streambuf& sb, const std::string& strHost)>         mBuild;
    std::function<bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> mComplete;

    boost::asio::deadline_timer                                 mDeadline;

    // If not success, we are shutting down.
    boost::system::error_code                                   mShutdown;

    std::deque<std::string>                                     mDeqSites;
    boost::posix_time::time_duration                            mTimeout;
};

//------------------------------------------------------------------------------

void HTTPClient::get (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::deque<std::string> deqSites,
    const unsigned short port,
    const std::string& strPath,
    std::size_t responseMax,
    boost::posix_time::time_duration timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        const std::string& strData)> complete)
{
    std::shared_ptr <HTTPClientImp> client (
        new HTTPClientImp (io_service, port, responseMax));

    client->get (bSSL, deqSites, strPath, timeout, complete);
}

void HTTPClient::get (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::string strSite,
    const unsigned short port,
    const std::string& strPath,
    std::size_t responseMax,
    boost::posix_time::time_duration timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        const std::string& strData)> complete)
{
    std::deque<std::string> deqSites (1, strSite);

    std::shared_ptr <HTTPClientImp> client (
        new HTTPClientImp (io_service, port, responseMax));

    client->get (bSSL, deqSites, strPath, timeout, complete);
}

void HTTPClient::request (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::string strSite,
    const unsigned short port,
    std::function<void (boost::asio::streambuf& sb, const std::string& strHost)> setRequest,
    std::size_t responseMax,
    boost::posix_time::time_duration timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        const std::string& strData)> complete)
{
    std::deque<std::string> deqSites (1, strSite);

    std::shared_ptr <HTTPClientImp> client (
        new HTTPClientImp (io_service, port, responseMax));

    client->request (bSSL, deqSites, setRequest, timeout, complete);
}

void HTTPClient::sendSMS (boost::asio::io_service& io_service, const std::string& strText)
{
    std::string strScheme;
    std::string strDomain;
    int         iPort;
    std::string strPath;

    if (getConfig ().SMS_URL == "" || !parseUrl (getConfig ().SMS_URL, strScheme, strDomain, iPort, strPath))
    {
        WriteLog (lsWARNING, HTTPClient) << "SMSRequest: Bad URL:" << getConfig ().SMS_URL;
    }
    else
    {
        bool const bSSL = strScheme == "https";

        std::deque<std::string> deqSites (1, strDomain);
        std::string strURI  =
            boost::str (boost::format ("%s?from=%s&to=%s&api_key=%s&api_secret=%s&text=%s")
                        % (strPath.empty () ? "/" : strPath)
                        % getConfig ().SMS_FROM
                        % getConfig ().SMS_TO
                        % getConfig ().SMS_KEY
                        % getConfig ().SMS_SECRET
                        % urlEncode (strText));

        // WriteLog (lsINFO) << "SMS: Request:" << strURI;
        WriteLog (lsINFO, HTTPClient) << "SMS: Request: '" << strText << "'";

        if (iPort < 0)
            iPort = bSSL ? 443 : 80;

        std::shared_ptr <HTTPClientImp> client (
            new HTTPClientImp (io_service, iPort, maxClientHeaderBytes));

        client->get (bSSL, deqSites, strURI, boost::posix_time::seconds (smsTimeoutSeconds),
                     std::bind (&HTTPClientImp::onSMSResponse,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3));
    }
}

} // ripple
