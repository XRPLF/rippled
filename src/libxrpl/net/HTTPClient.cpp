#include <xrpl/basics/Log.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/net/AutoSocket.h>
#include <xrpl/net/HTTPClient.h>
#include <xrpl/net/HTTPClientSSLContext.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/resolver_query_base.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/regex.hpp>

#include <optional>

namespace ripple {

static std::optional<HTTPClientSSLContext> httpClientSSLContext;

void
HTTPClient::initializeSSLContext(
    std::string const& sslVerifyDir,
    std::string const& sslVerifyFile,
    bool sslVerify,
    beast::Journal j)
{
    httpClientSSLContext.emplace(sslVerifyDir, sslVerifyFile, sslVerify, j);
}

//------------------------------------------------------------------------------
//
// Fetch a web page via http or https.
//
//------------------------------------------------------------------------------

class HTTPClientImp : public std::enable_shared_from_this<HTTPClientImp>,
                      public HTTPClient
{
public:
    HTTPClientImp(
        boost::asio::io_context& io_context,
        unsigned short const port,
        std::size_t maxResponseSize,
        beast::Journal& j)
        : mSocket(io_context, httpClientSSLContext->context())
        , mResolver(io_context)
        , mHeader(maxClientHeaderBytes)
        , mPort(port)
        , maxResponseSize_(maxResponseSize)
        , mDeadline(io_context)
        , j_(j)
    {
    }

    //--------------------------------------------------------------------------

    void
    makeGet(
        std::string const& strPath,
        boost::asio::streambuf& sb,
        std::string const& strHost)
    {
        std::ostream osRequest(&sb);

        osRequest << "GET " << strPath
                  << " HTTP/1.0\r\n"
                     "Host: "
                  << strHost
                  << "\r\n"
                     "Accept: */*\r\n"  // YYY Do we need this line?
                     "Connection: close\r\n\r\n";
    }

    //--------------------------------------------------------------------------

    void
    request(
        bool bSSL,
        std::deque<std::string> deqSites,
        std::function<
            void(boost::asio::streambuf& sb, std::string const& strHost)> build,
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete)
    {
        mSSL = bSSL;
        mDeqSites = deqSites;
        mBuild = build;
        mComplete = complete;
        mTimeout = timeout;

        httpsNext();
    }

    //--------------------------------------------------------------------------

    void
    get(bool bSSL,
        std::deque<std::string> deqSites,
        std::string const& strPath,
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete)
    {
        mComplete = complete;
        mTimeout = timeout;

        request(
            bSSL,
            deqSites,
            std::bind(
                &HTTPClientImp::makeGet,
                shared_from_this(),
                strPath,
                std::placeholders::_1,
                std::placeholders::_2),
            timeout,
            complete);
    }

    //--------------------------------------------------------------------------

    void
    httpsNext()
    {
        JLOG(j_.trace()) << "Fetch: " << mDeqSites[0];

        auto query = std::make_shared<Query>(
            mDeqSites[0],
            std::to_string(mPort),
            boost::asio::ip::resolver_query_base::numeric_service);
        mQuery = query;

        try
        {
            mDeadline.expires_after(mTimeout);
        }
        catch (boost::system::system_error const& e)
        {
            mShutdown = e.code();

            JLOG(j_.trace()) << "expires_after: " << mShutdown.message();
            mDeadline.async_wait(std::bind(
                &HTTPClientImp::handleDeadline,
                shared_from_this(),
                std::placeholders::_1));
        }

        if (!mShutdown)
        {
            JLOG(j_.trace()) << "Resolving: " << mDeqSites[0];

            mResolver.async_resolve(
                mQuery->host,
                mQuery->port,
                mQuery->flags,
                std::bind(
                    &HTTPClientImp::handleResolve,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }

        if (mShutdown)
            invokeComplete(mShutdown);
    }

    void
    handleDeadline(boost::system::error_code const& ecResult)
    {
        if (ecResult == boost::asio::error::operation_aborted)
        {
            // Timer canceled because deadline no longer needed.
            JLOG(j_.trace()) << "Deadline cancelled.";

            // Aborter is done.
        }
        else if (ecResult)
        {
            JLOG(j_.trace()) << "Deadline error: " << mDeqSites[0] << ": "
                             << ecResult.message();

            // Can't do anything sound.
            abort();
        }
        else
        {
            JLOG(j_.trace()) << "Deadline arrived.";

            // Mark us as shutting down.
            // XXX Use our own error code.
            mShutdown = boost::system::error_code{
                boost::system::errc::bad_address,
                boost::system::system_category()};

            // Cancel any resolving.
            mResolver.cancel();

            // Stop the transaction.
            mSocket.async_shutdown(std::bind(
                &HTTPClientImp::handleShutdown,
                shared_from_this(),
                std::placeholders::_1));
        }
    }

    void
    handleShutdown(boost::system::error_code const& ecResult)
    {
        if (ecResult)
        {
            JLOG(j_.trace()) << "Shutdown error: " << mDeqSites[0] << ": "
                             << ecResult.message();
        }
    }

    void
    handleResolve(
        boost::system::error_code const& ecResult,
        boost::asio::ip::tcp::resolver::results_type result)
    {
        if (!mShutdown)
        {
            mShutdown = ecResult ? ecResult
                                 : httpClientSSLContext->preConnectVerify(
                                       mSocket.SSLSocket(), mDeqSites[0]);
        }

        if (mShutdown)
        {
            JLOG(j_.trace()) << "Resolve error: " << mDeqSites[0] << ": "
                             << mShutdown.message();

            invokeComplete(mShutdown);
        }
        else
        {
            JLOG(j_.trace()) << "Resolve complete.";

            boost::asio::async_connect(
                mSocket.lowest_layer(),
                result,
                std::bind(
                    &HTTPClientImp::handleConnect,
                    shared_from_this(),
                    std::placeholders::_1));
        }
    }

    void
    handleConnect(boost::system::error_code const& ecResult)
    {
        if (!mShutdown)
            mShutdown = ecResult;

        if (mShutdown)
        {
            JLOG(j_.trace()) << "Connect error: " << mShutdown.message();
        }

        if (!mShutdown)
        {
            JLOG(j_.trace()) << "Connected.";

            mShutdown = httpClientSSLContext->postConnectVerify(
                mSocket.SSLSocket(), mDeqSites[0]);

            if (mShutdown)
            {
                JLOG(j_.trace()) << "postConnectVerify: " << mDeqSites[0]
                                 << ": " << mShutdown.message();
            }
        }

        if (mShutdown)
        {
            invokeComplete(mShutdown);
        }
        else if (mSSL)
        {
            mSocket.async_handshake(
                AutoSocket::ssl_socket::client,
                std::bind(
                    &HTTPClientImp::handleRequest,
                    shared_from_this(),
                    std::placeholders::_1));
        }
        else
        {
            handleRequest(ecResult);
        }
    }

    void
    handleRequest(boost::system::error_code const& ecResult)
    {
        if (!mShutdown)
            mShutdown = ecResult;

        if (mShutdown)
        {
            JLOG(j_.trace()) << "Handshake error:" << mShutdown.message();

            invokeComplete(mShutdown);
        }
        else
        {
            JLOG(j_.trace()) << "Session started.";

            mBuild(mRequest, mDeqSites[0]);

            mSocket.async_write(
                mRequest,
                std::bind(
                    &HTTPClientImp::handleWrite,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleWrite(
        boost::system::error_code const& ecResult,
        std::size_t bytes_transferred)
    {
        if (!mShutdown)
            mShutdown = ecResult;

        if (mShutdown)
        {
            JLOG(j_.trace()) << "Write error: " << mShutdown.message();

            invokeComplete(mShutdown);
        }
        else
        {
            JLOG(j_.trace()) << "Wrote.";

            mSocket.async_read_until(
                mHeader,
                "\r\n\r\n",
                std::bind(
                    &HTTPClientImp::handleHeader,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleHeader(
        boost::system::error_code const& ecResult,
        std::size_t bytes_transferred)
    {
        std::string strHeader{
            {std::istreambuf_iterator<char>(&mHeader)},
            std::istreambuf_iterator<char>()};
        JLOG(j_.trace()) << "Header: \"" << strHeader << "\"";

        static boost::regex reStatus{
            "\\`HTTP/1\\S+ (\\d{3}) .*\\'"};  // HTTP/1.1 200 OK
        static boost::regex reSize{
            "\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'", boost::regex::icase};
        static boost::regex reBody{"\\`.*\\r\\n\\r\\n(.*)\\'"};

        boost::smatch smMatch;
        // Match status code.
        if (!boost::regex_match(strHeader, smMatch, reStatus))
        {
            // XXX Use our own error code.
            JLOG(j_.trace()) << "No status code";
            invokeComplete(boost::system::error_code{
                boost::system::errc::bad_address,
                boost::system::system_category()});
            return;
        }

        mStatus = beast::lexicalCastThrow<int>(std::string(smMatch[1]));

        if (boost::regex_match(strHeader, smMatch, reBody))  // we got some body
            mBody = smMatch[1];

        std::size_t const responseSize = [&] {
            if (boost::regex_match(strHeader, smMatch, reSize))
                return beast::lexicalCast<std::size_t>(
                    std::string(smMatch[1]), maxResponseSize_);
            return maxResponseSize_;
        }();

        if (responseSize > maxResponseSize_)
        {
            JLOG(j_.trace()) << "Response field too large";
            invokeComplete(boost::system::error_code{
                boost::system::errc::value_too_large,
                boost::system::system_category()});
            return;
        }

        if (responseSize == 0)
        {
            // no body wanted or available
            invokeComplete(ecResult, mStatus);
        }
        else if (mBody.size() >= responseSize)
        {
            // we got the whole thing
            invokeComplete(ecResult, mStatus, mBody);
        }
        else
        {
            mSocket.async_read(
                mResponse.prepare(responseSize - mBody.size()),
                boost::asio::transfer_all(),
                std::bind(
                    &HTTPClientImp::handleData,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleData(
        boost::system::error_code const& ecResult,
        std::size_t bytes_transferred)
    {
        if (!mShutdown)
            mShutdown = ecResult;

        if (mShutdown && mShutdown != boost::asio::error::eof)
        {
            JLOG(j_.trace()) << "Read error: " << mShutdown.message();

            invokeComplete(mShutdown);
        }
        else
        {
            if (mShutdown)
            {
                JLOG(j_.trace()) << "Complete.";
            }
            else
            {
                mResponse.commit(bytes_transferred);
                std::string strBody{
                    {std::istreambuf_iterator<char>(&mResponse)},
                    std::istreambuf_iterator<char>()};
                invokeComplete(ecResult, mStatus, mBody + strBody);
            }
        }
    }

    // Call cancel the deadline timer and invoke the completion routine.
    void
    invokeComplete(
        boost::system::error_code const& ecResult,
        int iStatus = 0,
        std::string const& strData = "")
    {
        boost::system::error_code ecCancel;
        try
        {
            mDeadline.cancel();
        }
        catch (boost::system::system_error const& e)
        {
            JLOG(j_.trace())
                << "invokeComplete: Deadline cancel error: " << e.what();
            ecCancel = e.code();
        }

        JLOG(j_.debug()) << "invokeComplete: Deadline popping: "
                         << mDeqSites.size();

        if (!mDeqSites.empty())
        {
            mDeqSites.pop_front();
        }

        bool bAgain = true;

        if (mDeqSites.empty() || !ecResult)
        {
            // ecResult: !0 = had an error, last entry
            //    iStatus: result, if no error
            //  strData: data, if no error
            bAgain = mComplete &&
                mComplete(ecResult ? ecResult : ecCancel, iStatus, strData);
        }

        if (!mDeqSites.empty() && bAgain)
        {
            httpsNext();
        }
    }

private:
    using pointer = std::shared_ptr<HTTPClient>;

    bool mSSL;
    AutoSocket mSocket;
    boost::asio::ip::tcp::resolver mResolver;

    struct Query
    {
        std::string host;
        std::string port;
        boost::asio::ip::resolver_query_base::flags flags;
    };
    std::shared_ptr<Query> mQuery;

    boost::asio::streambuf mRequest;
    boost::asio::streambuf mHeader;
    boost::asio::streambuf mResponse;
    std::string mBody;
    unsigned short const mPort;
    std::size_t const maxResponseSize_;
    int mStatus;
    std::function<void(boost::asio::streambuf& sb, std::string const& strHost)>
        mBuild;
    std::function<bool(
        boost::system::error_code const& ecResult,
        int iStatus,
        std::string const& strData)>
        mComplete;

    boost::asio::basic_waitable_timer<std::chrono::steady_clock> mDeadline;

    // If not success, we are shutting down.
    boost::system::error_code mShutdown;

    std::deque<std::string> mDeqSites;
    std::chrono::seconds mTimeout;
    beast::Journal j_;
};

//------------------------------------------------------------------------------

void
HTTPClient::get(
    bool bSSL,
    boost::asio::io_context& io_context,
    std::deque<std::string> deqSites,
    unsigned short const port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool(
        boost::system::error_code const& ecResult,
        int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    auto client =
        std::make_shared<HTTPClientImp>(io_context, port, responseMax, j);
    client->get(bSSL, deqSites, strPath, timeout, complete);
}

void
HTTPClient::get(
    bool bSSL,
    boost::asio::io_context& io_context,
    std::string strSite,
    unsigned short const port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool(
        boost::system::error_code const& ecResult,
        int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    std::deque<std::string> deqSites(1, strSite);

    auto client =
        std::make_shared<HTTPClientImp>(io_context, port, responseMax, j);
    client->get(bSSL, deqSites, strPath, timeout, complete);
}

void
HTTPClient::request(
    bool bSSL,
    boost::asio::io_context& io_context,
    std::string strSite,
    unsigned short const port,
    std::function<void(boost::asio::streambuf& sb, std::string const& strHost)>
        setRequest,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<bool(
        boost::system::error_code const& ecResult,
        int iStatus,
        std::string const& strData)> complete,
    beast::Journal& j)
{
    std::deque<std::string> deqSites(1, strSite);

    auto client =
        std::make_shared<HTTPClientImp>(io_context, port, responseMax, j);
    client->request(bSSL, deqSites, setRequest, timeout, complete);
}

}  // namespace ripple
