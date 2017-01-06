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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/net/AutoSocket.h>
#include <beast/core/placeholders.hpp>
#include <ripple/beast/core/LexicalCast.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/regex.hpp>
#include <boost/optional.hpp>

namespace ripple {

//
// Fetch a web page via http or https.
//

class HTTPClientSSLContext
{
public:
    explicit
    HTTPClientSSLContext (Config const& config)
        : m_context (boost::asio::ssl::context::sslv23)
        , verify_ (config.SSL_VERIFY)
    {
        boost::system::error_code ec;

        if (config.SSL_VERIFY_FILE.empty ())
        {
            m_context.set_default_verify_paths (ec);

            if (ec && config.SSL_VERIFY_DIR.empty ())
                Throw<std::runtime_error> (
                    boost::str (boost::format (
                        "Failed to set_default_verify_paths: %s") %
                            ec.message ()));
        }
        else
        {
            m_context.load_verify_file (config.SSL_VERIFY_FILE);
        }

        if (! config.SSL_VERIFY_DIR.empty ())
        {
            m_context.add_verify_path (config.SSL_VERIFY_DIR, ec);

            if (ec)
                Throw<std::runtime_error> (
                    boost::str (boost::format (
                        "Failed to add verify path: %s") % ec.message ()));
        }
    }

    boost::asio::ssl::context& context()
    {
        return m_context;
    }

    bool sslVerify() const
    {
        return verify_;
    }

private:
    boost::asio::ssl::context m_context;
    bool verify_;
};

boost::optional<HTTPClientSSLContext> httpClientSSLContext;

void HTTPClient::initializeSSLContext (Config const& config)
{
    httpClientSSLContext.emplace (config);
}

//------------------------------------------------------------------------------

class HTTPClientImp
    : public std::enable_shared_from_this <HTTPClientImp>
    , public HTTPClient
{
public:
    HTTPClientImp (boost::asio::io_service& io_service,
        const unsigned short port,
        std::size_t responseMax,
        beast::Journal& j)
        : mSocket (io_service, httpClientSSLContext->context ())
        , mResolver (io_service)
        , mHeader (maxClientHeaderBytes)
        , mPort (port)
        , mResponseMax (responseMax)
        , mDeadline (io_service)
        , j_ (j)
    {
        if (!httpClientSSLContext->sslVerify())
            mSocket.SSLSocket ().set_verify_mode (boost::asio::ssl::verify_none);
    }

    //--------------------------------------------------------------------------

    void makeGet (std::string const& strPath, boost::asio::streambuf& sb,
        std::string const& strHost)
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
        std::function<void (boost::asio::streambuf& sb, std::string const& strHost)> build,
        std::chrono::seconds timeout,
        std::function<bool (const boost::system::error_code& ecResult,
            int iStatus, std::string const& strData)> complete)
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
        std::string const& strPath,
        std::chrono::seconds timeout,
        std::function<bool (const boost::system::error_code& ecResult, int iStatus,
            std::string const& strData)> complete)
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
        JLOG (j_.trace()) << "Fetch: " << mDeqSites[0];

        auto query = std::make_shared<boost::asio::ip::tcp::resolver::query>(
                mDeqSites[0],
                beast::lexicalCast <std::string> (mPort),
                boost::asio::ip::resolver_query_base::numeric_service);
        mQuery  = query;

        mDeadline.expires_from_now (mTimeout, mShutdown);

        JLOG (j_.trace()) << "expires_from_now: " << mShutdown.message ();

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
            JLOG (j_.trace()) << "Resolving: " << mDeqSites[0];

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
            JLOG (j_.trace()) << "Deadline cancelled.";

            // Aborter is done.
        }
        else if (ecResult)
        {
            JLOG (j_.trace()) << "Deadline error: " << mDeqSites[0] << ": " << ecResult.message ();

            // Can't do anything sound.
            abort ();
        }
        else
        {
            JLOG (j_.trace()) << "Deadline arrived.";

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
            JLOG (j_.trace()) << "Shutdown error: " << mDeqSites[0] << ": " << ecResult.message ();
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
            JLOG (j_.trace()) << "Resolve error: " << mDeqSites[0] << ": " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            JLOG (j_.trace()) << "Resolve complete.";

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
            JLOG (j_.trace()) << "Connect error: " << mShutdown.message ();
        }

        if (!mShutdown)
        {
            JLOG (j_.trace()) << "Connected.";

            if (httpClientSSLContext->sslVerify ())
            {
                mShutdown   = mSocket.verify (mDeqSites[0]);

                if (mShutdown)
                {
                    JLOG (j_.trace()) << "set_verify_callback: " << mDeqSites[0] << ": " << mShutdown.message ();
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
            JLOG (j_.trace()) << "Handshake error:" << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            JLOG (j_.trace()) << "Session started.";

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
            JLOG (j_.trace()) << "Write error: " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            JLOG (j_.trace()) << "Wrote.";

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
        JLOG (j_.trace()) << "Header: \"" << strHeader << "\"";

        static boost::regex reStatus ("\\`HTTP/1\\S+ (\\d{3}) .*\\'");          // HTTP/1.1 200 OK
        static boost::regex reSize ("\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'");
        static boost::regex reBody ("\\`.*\\r\\n\\r\\n(.*)\\'");

        boost::smatch   smMatch;

        bool    bMatch  = boost::regex_match (strHeader, smMatch, reStatus);    // Match status code.

        if (!bMatch)
        {
            // XXX Use our own error code.
            JLOG (j_.trace()) << "No status code";
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
            JLOG (j_.trace()) << "Read error: " << mShutdown.message ();

            invokeComplete (mShutdown);
        }
        else
        {
            if (mShutdown)
            {
                JLOG (j_.trace()) << "Complete.";
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
    void invokeComplete (const boost::system::error_code& ecResult, int iStatus = 0, std::string const& strData = "")
    {
        boost::system::error_code ecCancel;

        (void) mDeadline.cancel (ecCancel);

        if (ecCancel)
        {
            JLOG (j_.trace()) << "invokeComplete: Deadline cancel error: " << ecCancel.message ();
        }

        JLOG (j_.debug()) << "invokeComplete: Deadline popping: " << mDeqSites.size ();

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

private:
    using pointer = std::shared_ptr<HTTPClient>;

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
    std::function<void (boost::asio::streambuf& sb, std::string const& strHost)>         mBuild;
    std::function<bool (const boost::system::error_code& ecResult, int iStatus, std::string const& strData)> mComplete;

    boost::asio::basic_waitable_timer<std::chrono::steady_clock> mDeadline;

    // If not success, we are shutting down.
    boost::system::error_code                                   mShutdown;

    std::deque<std::string>                                     mDeqSites;
    std::chrono::seconds                                        mTimeout;
    beast::Journal                                              j_;
};

//------------------------------------------------------------------------------

void HTTPClient::get (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::deque<std::string> deqSites,
    const unsigned short port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    auto client = std::make_shared<HTTPClientImp> (
        io_service, port, responseMax, j);
    client->get (bSSL, deqSites, strPath, timeout, complete);
}

void HTTPClient::get (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::string strSite,
    const unsigned short port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    std::deque<std::string> deqSites (1, strSite);

    auto client = std::make_shared<HTTPClientImp> (
        io_service, port, responseMax, j);
    client->get (bSSL, deqSites, strPath, timeout, complete);
}

void HTTPClient::request (
    bool bSSL,
    boost::asio::io_service& io_service,
    std::string strSite,
    const unsigned short port,
    std::function<void (boost::asio::streambuf& sb, std::string const& strHost)> setRequest,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool (const boost::system::error_code& ecResult, int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    std::deque<std::string> deqSites (1, strSite);

    auto client = std::make_shared<HTTPClientImp> (
        io_service, port, responseMax, j);
    client->request (bSSL, deqSites, setRequest, timeout, complete);
}

} // ripple
