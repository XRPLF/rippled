#include <xrpl/net/HTTPClient.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/net/AutoSocket.h>
#include <xrpl/net/HTTPClientSSLContext.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/resolver_query_base.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/detail/system_category.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

namespace xrpl {

static std::optional<HTTPClientSSLContext> gHttpClientSslContext;

void
HTTPClient::initializeSSLContext(
    std::string const& sslVerifyDir,
    std::string const& sslVerifyFile,
    bool sslVerify,
    beast::Journal j)
{
    gHttpClientSslContext.emplace(sslVerifyDir, sslVerifyFile, sslVerify, j);
}

void
HTTPClient::cleanupSSLContext()
{
    gHttpClientSslContext.reset();
}

//------------------------------------------------------------------------------
//
// Fetch a web page via http or https.
//
//------------------------------------------------------------------------------

class HTTPClientImp : public std::enable_shared_from_this<HTTPClientImp>, public HTTPClient
{
public:
    HTTPClientImp(
        boost::asio::io_context& ioContext,
        unsigned short const port,
        std::size_t maxResponseSize,
        beast::Journal& j)
        : socket_(
              ioContext,
              gHttpClientSslContext->context())  // NOLINT(bugprone-unchecked-optional-access)
        , resolver_(ioContext)
        , header_(kMaxClientHeaderBytes)
        , port_(port)
        , maxResponseSize_(maxResponseSize)
        , deadline_(ioContext)
        , j_(j)
    {
    }

    //--------------------------------------------------------------------------

    void
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    makeGet(std::string const& strPath, boost::asio::streambuf& sb, std::string const& strHost)
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
        std::function<void(boost::asio::streambuf& sb, std::string const& strHost)> build,
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete)
    {
        ssl_ = bSSL;
        deqSites_ = deqSites;
        build_ = build;
        complete_ = complete;
        timeout_ = timeout;

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
        complete_ = complete;
        timeout_ = timeout;

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
        JLOG(j_.trace()) << "Fetch: " << deqSites_[0];

        auto query = std::make_shared<Query>(
            deqSites_[0],
            std::to_string(port_),
            boost::asio::ip::resolver_query_base::numeric_service);
        query_ = query;

        try
        {
            deadline_.expires_after(timeout_);
        }
        catch (boost::system::system_error const& e)
        {
            shutdown_ = e.code();

            JLOG(j_.trace()) << "expires_after: " << shutdown_.message();
            deadline_.async_wait(
                std::bind(
                    &HTTPClientImp::handleDeadline, shared_from_this(), std::placeholders::_1));
        }

        if (!shutdown_)
        {
            JLOG(j_.trace()) << "Resolving: " << deqSites_[0];

            resolver_.async_resolve(
                query_->host,
                query_->port,
                query_->flags,
                std::bind(
                    &HTTPClientImp::handleResolve,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }

        if (shutdown_)
            invokeComplete(shutdown_);
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
            JLOG(j_.trace()) << "Deadline error: " << deqSites_[0] << ": " << ecResult.message();

            // Can't do anything sound.
            std::abort();
        }
        else
        {
            JLOG(j_.trace()) << "Deadline arrived.";

            // Mark us as shutting down.
            // XXX Use our own error code.
            shutdown_ = boost::system::error_code{
                boost::system::errc::bad_address, boost::system::system_category()};

            // Cancel any resolving.
            resolver_.cancel();

            // Stop the transaction.
            socket_.asyncShutdown(
                std::bind(
                    &HTTPClientImp::handleShutdown, shared_from_this(), std::placeholders::_1));
        }
    }

    void
    handleShutdown(boost::system::error_code const& ecResult)
    {
        if (ecResult)
        {
            JLOG(j_.trace()) << "Shutdown error: " << deqSites_[0] << ": " << ecResult.message();
        }
    }

    void
    handleResolve(
        boost::system::error_code const& ecResult,
        boost::asio::ip::tcp::resolver::results_type result)
    {
        if (!shutdown_)
        {
            shutdown_ = ecResult
                ? ecResult
                // gHttpClientSslContext always initialized before use
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                : gHttpClientSslContext->preConnectVerify(socket_.sslSocket(), deqSites_[0]);
        }

        if (shutdown_)
        {
            JLOG(j_.trace()) << "Resolve error: " << deqSites_[0] << ": " << shutdown_.message();

            invokeComplete(shutdown_);
        }
        else
        {
            JLOG(j_.trace()) << "Resolve complete.";

            boost::asio::async_connect(
                socket_.lowestLayer(),
                result,
                std::bind(
                    &HTTPClientImp::handleConnect, shared_from_this(), std::placeholders::_1));
        }
    }

    void
    handleConnect(boost::system::error_code const& ecResult)
    {
        if (!shutdown_)
            shutdown_ = ecResult;

        if (shutdown_)
        {
            JLOG(j_.trace()) << "Connect error: " << shutdown_.message();
        }

        if (!shutdown_)
        {
            JLOG(j_.trace()) << "Connected.";

            // gHttpClientSslContext always initialized before use
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            shutdown_ = gHttpClientSslContext->postConnectVerify(socket_.sslSocket(), deqSites_[0]);

            if (shutdown_)
            {
                JLOG(j_.trace()) << "postConnectVerify: " << deqSites_[0] << ": "
                                 << shutdown_.message();
            }
        }

        if (shutdown_)
        {
            invokeComplete(shutdown_);
        }
        else if (ssl_)
        {
            socket_.asyncHandshake(
                AutoSocket::ssl_socket::client,
                std::bind(
                    &HTTPClientImp::handleRequest, shared_from_this(), std::placeholders::_1));
        }
        else
        {
            handleRequest(ecResult);
        }
    }

    void
    handleRequest(boost::system::error_code const& ecResult)
    {
        if (!shutdown_)
            shutdown_ = ecResult;

        if (shutdown_)
        {
            JLOG(j_.trace()) << "Handshake error:" << shutdown_.message();

            invokeComplete(shutdown_);
        }
        else
        {
            JLOG(j_.trace()) << "Session started.";

            build_(request_, deqSites_[0]);

            socket_.asyncWrite(
                request_,
                std::bind(
                    &HTTPClientImp::handleWrite,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleWrite(boost::system::error_code const& ecResult, std::size_t bytesTransferred)
    {
        if (!shutdown_)
            shutdown_ = ecResult;

        if (shutdown_)
        {
            JLOG(j_.trace()) << "Write error: " << shutdown_.message();

            invokeComplete(shutdown_);
        }
        else
        {
            JLOG(j_.trace()) << "Wrote.";

            socket_.asyncReadUntil(
                header_,
                "\r\n\r\n",
                std::bind(
                    &HTTPClientImp::handleHeader,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleHeader(boost::system::error_code const& ecResult, std::size_t bytesTransferred)
    {
        std::string strHeader{
            {std::istreambuf_iterator<char>(&header_)}, std::istreambuf_iterator<char>()};
        JLOG(j_.trace()) << "Header: \"" << strHeader << "\"";

        static boost::regex const kReStatus{"\\`HTTP/1\\S+ (\\d{3}) .*\\'"};  // HTTP/1.1 200 OK
        static boost::regex const kReSize{
            "\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'", boost::regex::icase};
        static boost::regex const kReBody{"\\`.*\\r\\n\\r\\n(.*)\\'"};

        boost::smatch smMatch;
        // Match status code.
        if (!boost::regex_match(strHeader, smMatch, kReStatus))
        {
            // XXX Use our own error code.
            JLOG(j_.trace()) << "No status code";
            invokeComplete(
                boost::system::error_code{
                    boost::system::errc::bad_address, boost::system::system_category()});
            return;
        }

        status_ = beast::lexicalCastThrow<int>(std::string(smMatch[1]));

        if (boost::regex_match(strHeader, smMatch, kReBody))  // we got some body
            body_ = smMatch[1];

        std::size_t const responseSize = [&] {
            if (boost::regex_match(strHeader, smMatch, kReSize))
                return beast::lexicalCast<std::size_t>(std::string(smMatch[1]), maxResponseSize_);
            return maxResponseSize_;
        }();

        if (responseSize > maxResponseSize_)
        {
            JLOG(j_.trace()) << "Response field too large";
            invokeComplete(
                boost::system::error_code{
                    boost::system::errc::value_too_large, boost::system::system_category()});
            return;
        }

        if (responseSize == 0)
        {
            // no body wanted or available
            invokeComplete(ecResult, status_);
        }
        else if (body_.size() >= responseSize)
        {
            // we got the whole thing
            invokeComplete(ecResult, status_, body_);
        }
        else
        {
            socket_.asyncRead(
                response_.prepare(responseSize - body_.size()),
                boost::asio::transfer_all(),
                std::bind(
                    &HTTPClientImp::handleData,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    handleData(boost::system::error_code const& ecResult, std::size_t bytesTransferred)
    {
        if (!shutdown_)
            shutdown_ = ecResult;

        if (shutdown_ && shutdown_ != boost::asio::error::eof)
        {
            JLOG(j_.trace()) << "Read error: " << shutdown_.message();

            invokeComplete(shutdown_);
        }
        else
        {
            if (shutdown_)
            {
                JLOG(j_.trace()) << "Complete.";
            }
            else
            {
                response_.commit(bytesTransferred);
                std::string const strBody{
                    {std::istreambuf_iterator<char>(&response_)}, std::istreambuf_iterator<char>()};
                invokeComplete(ecResult, status_, body_ + strBody);
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
            deadline_.cancel();
        }
        catch (boost::system::system_error const& e)
        {
            JLOG(j_.trace()) << "invokeComplete: Deadline cancel error: " << e.what();
            ecCancel = e.code();
        }

        JLOG(j_.debug()) << "invokeComplete: Deadline popping: " << deqSites_.size();

        if (!deqSites_.empty())
        {
            deqSites_.pop_front();
        }

        bool bAgain = true;

        if (deqSites_.empty() || !ecResult)
        {
            // ecResult: !0 = had an error, last entry
            //    iStatus: result, if no error
            //  strData: data, if no error
            bAgain = complete_ && complete_(ecResult ? ecResult : ecCancel, iStatus, strData);
        }

        if (!deqSites_.empty() && bAgain)
        {
            httpsNext();
        }
    }

private:
    using pointer = std::shared_ptr<HTTPClient>;

    bool ssl_{};
    AutoSocket socket_;
    boost::asio::ip::tcp::resolver resolver_;

    struct Query
    {
        std::string host;
        std::string port;
        boost::asio::ip::resolver_query_base::flags flags;
    };
    std::shared_ptr<Query> query_;

    boost::asio::streambuf request_;
    boost::asio::streambuf header_;
    boost::asio::streambuf response_;
    std::string body_;
    unsigned short const port_;
    std::size_t const maxResponseSize_;
    int status_{};
    std::function<void(boost::asio::streambuf& sb, std::string const& strHost)> build_;
    std::function<
        bool(boost::system::error_code const& ecResult, int iStatus, std::string const& strData)>
        complete_;

    boost::asio::basic_waitable_timer<std::chrono::steady_clock> deadline_;

    // If not success, we are shutting down.
    boost::system::error_code shutdown_;

    std::deque<std::string> deqSites_;
    std::chrono::seconds timeout_{};
    beast::Journal j_;
};

//------------------------------------------------------------------------------

void
HTTPClient::get(
    bool bSSL,
    boost::asio::io_context& ioContext,
    std::deque<std::string> deqSites,
    unsigned short const port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<
        bool(boost::system::error_code const& ecResult, int iStatus, std::string const& strData)>
        complete,
    beast::Journal& j)
{
    auto client = std::make_shared<HTTPClientImp>(ioContext, port, responseMax, j);
    client->get(bSSL, deqSites, strPath, timeout, complete);
}

void
HTTPClient::get(
    bool bSSL,
    boost::asio::io_context& ioContext,
    std::string strSite,
    unsigned short const port,
    std::string const& strPath,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<
        bool(boost::system::error_code const& ecResult, int iStatus, std::string const& strData)>
        complete,
    beast::Journal& j)
{
    std::deque<std::string> const deqSites(1, strSite);

    auto client = std::make_shared<HTTPClientImp>(ioContext, port, responseMax, j);
    client->get(bSSL, deqSites, strPath, timeout, complete);
}

void
HTTPClient::request(
    bool bSSL,
    boost::asio::io_context& ioContext,
    std::string strSite,
    unsigned short const port,
    std::function<void(boost::asio::streambuf& sb, std::string const& strHost)> setRequest,
    std::size_t responseMax,
    std::chrono::seconds timeout,
    std::function<
        bool(boost::system::error_code const& ecResult, int iStatus, std::string const& strData)>
        complete,
    beast::Journal& j)
{
    std::deque<std::string> const deqSites(1, strSite);

    auto client = std::make_shared<HTTPClientImp>(ioContext, port, responseMax, j);
    client->request(bSSL, deqSites, setRequest, timeout, complete);
}

}  // namespace xrpl
