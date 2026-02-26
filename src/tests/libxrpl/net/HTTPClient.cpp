#include <xrpl/basics/Log.h>
#include <xrpl/net/HTTPClient.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace xrpl;

namespace {

// Simple HTTP server using Beast for testing
class TestHTTPServer
{
private:
    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::endpoint endpoint_;
    bool running_{true};
    bool finished_{false};
    unsigned short port_;

    // Custom headers to return
    std::map<std::string, std::string> customHeaders_;
    std::string responseBody_;
    unsigned int statusCode_{200};
    bool noContentLength_{false};
    bool sendResponse_{true};
    bool closeImmediately_{false};
    std::chrono::milliseconds responseDelay_{0};
    bool splitEofBody_{false};
    std::size_t splitEofBodyAt_{0};
    std::chrono::milliseconds splitEofBodyDelay_{0};
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> heldSockets_;
    std::atomic<int> activeConnections_{0};
    std::atomic<int> totalAccepted_{0};
    mutable std::mutex heldMutex_;

    beast::Journal j_;

public:
    TestHTTPServer() : acceptor_(ioc_), port_(0), j_(TestSink::instance())
    {
        // Bind to any available port
        endpoint_ = {boost::asio::ip::tcp::v4(), 0};
        acceptor_.open(endpoint_.protocol());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint_);
        acceptor_.listen();

        // Get the actual port that was assigned
        port_ = acceptor_.local_endpoint().port();

        // Start the accept coroutine
        boost::asio::co_spawn(ioc_, accept(), boost::asio::detached);
    }

    TestHTTPServer(TestHTTPServer&&) = delete;
    TestHTTPServer&
    operator=(TestHTTPServer&&) = delete;

    ~TestHTTPServer()
    {
        stop();
        while (ioc_.poll_one() > 0)
            ;
    }

    boost::asio::io_context&
    ioc()
    {
        return ioc_;
    }

    unsigned short
    port() const
    {
        return port_;
    }

    void
    setHeader(std::string const& name, std::string const& value)
    {
        customHeaders_[name] = value;
    }

    void
    setResponseBody(std::string const& body)
    {
        responseBody_ = body;
    }

    void
    setStatusCode(unsigned int code)
    {
        statusCode_ = code;
    }

    void
    setNoContentLength(bool noContentLength)
    {
        noContentLength_ = noContentLength;
    }

    void
    setSendResponse(bool sendResponse)
    {
        sendResponse_ = sendResponse;
    }

    void
    setCloseImmediately(bool closeImmediately)
    {
        closeImmediately_ = closeImmediately;
    }

    void
    setResponseDelay(std::chrono::milliseconds delay)
    {
        responseDelay_ = delay;
    }

    void
    setSplitEofBody(std::size_t splitAt, std::chrono::milliseconds delay)
    {
        splitEofBody_ = true;
        splitEofBodyAt_ = splitAt;
        splitEofBodyDelay_ = delay;
    }

    int
    activeConnectionCount() const
    {
        return activeConnections_.load();
    }

    int
    totalAcceptedCount() const
    {
        return totalAccepted_.load();
    }

    void
    stop()
    {
        running_ = false;
        releaseHeld();
        acceptor_.close();
    }

    bool
    finished() const
    {
        return finished_;
    }

private:
    boost::asio::awaitable<void>
    accept()
    {
        while (running_)
        {
            try
            {
                auto socket = std::make_shared<boost::asio::ip::tcp::socket>(
                    co_await acceptor_.async_accept(boost::asio::use_awaitable));

                if (!running_)
                    break;

                ++totalAccepted_;
                ++activeConnections_;
                boost::asio::co_spawn(ioc_, handleConnection(socket), boost::asio::detached);
            }
            catch (std::exception const& e)
            {
                // Accept or handle failed, stop accepting
                if (running_)
                {
                    JLOG(j_.debug()) << "Error: " << e.what();
                }
                break;
            }
        }

        finished_ = true;
    }

    boost::asio::awaitable<void>
    handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
    {
        try
        {
            if (closeImmediately_)
            {
                boost::system::error_code ec;
                socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                socket->close(ec);
                --activeConnections_;
                co_return;
            }

            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::string_body> req;

            // Read the HTTP request asynchronously
            co_await boost::beast::http::async_read(
                *socket, buffer, req, boost::asio::use_awaitable);

            if (!sendResponse_)
            {
                std::lock_guard lg(heldMutex_);
                heldSockets_.push_back(socket);
                co_return;
            }

            if (responseDelay_.count() > 0)
            {
                boost::asio::steady_timer timer(ioc_);
                timer.expires_after(responseDelay_);
                co_await timer.async_wait(boost::asio::use_awaitable);
            }

            if (splitEofBody_)
            {
                auto splitAt = std::min(splitEofBodyAt_, responseBody_.size());
                auto const head = "HTTP/1.0 " + std::to_string(statusCode_) +
                    " OK\r\n"
                    "Server: TestServer\r\n"
                    "\r\n";
                auto const firstPart = head + responseBody_.substr(0, splitAt);
                auto const secondPart = responseBody_.substr(splitAt);

                co_await boost::asio::async_write(
                    *socket, boost::asio::buffer(firstPart), boost::asio::use_awaitable);

                if (splitEofBodyDelay_.count() > 0)
                {
                    boost::asio::steady_timer timer(ioc_);
                    timer.expires_after(splitEofBodyDelay_);
                    co_await timer.async_wait(boost::asio::use_awaitable);
                }

                if (!secondPart.empty())
                {
                    co_await boost::asio::async_write(
                        *socket, boost::asio::buffer(secondPart), boost::asio::use_awaitable);
                }

                boost::system::error_code shutdownEc;
                socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, shutdownEc);
                socket->close(shutdownEc);
                --activeConnections_;
                co_return;
            }

            // Create response
            boost::beast::http::response<boost::beast::http::string_body> res;
            res.version(req.version());
            res.result(statusCode_);
            res.set(boost::beast::http::field::server, "TestServer");

            // Set body and prepare payload first
            res.body() = responseBody_;
            res.prepare_payload();

            // Override Content-Length with custom headers after
            // prepare_payload. This allows us to test case-insensitive
            // header parsing.
            for (auto const& [name, value] : customHeaders_)
            {
                res.set(name, value);
            }

            // Remove Content-Length to test EOF-delimited responses.
            // HTTP/1.0 clients read until connection close.
            if (noContentLength_)
                res.erase(boost::beast::http::field::content_length);

            // Send response asynchronously
            co_await boost::beast::http::async_write(*socket, res, boost::asio::use_awaitable);

            // Shutdown socket gracefully
            boost::system::error_code shutdownEc;
            socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, shutdownEc);
            socket->close(shutdownEc);
        }
        catch (std::exception const& e)
        {
            // Error reading or writing, just close the connection
            JLOG(j_.debug()) << "Connection error: " << e.what();
        }
        --activeConnections_;
    }

    void
    releaseHeld()
    {
        std::lock_guard lg(heldMutex_);
        for (auto& socket : heldSockets_)
        {
            boost::system::error_code ec;
            socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket->close(ec);
            --activeConnections_;
        }
        heldSockets_.clear();
    }
};

// Helper function to run HTTP client test
bool
runHTTPTest(
    TestHTTPServer& server,
    std::string const& path,
    bool& completed,
    int& resultStatus,
    std::string& resultData,
    boost::system::error_code& resultError,
    std::chrono::seconds timeout = std::chrono::seconds{5})
{
    // Create a null journal for testing
    beast::Journal j{TestSink::instance()};

    // Initialize HTTPClient SSL context
    HTTPClient::initializeSSLContext("", "", false, j);

    HTTPClient::get(
        false,  // no SSL
        server.ioc(),
        "127.0.0.1",
        server.port(),
        path,
        1024,  // max response size
        timeout,
        [&](boost::system::error_code const& ec, int status, std::string const& data) -> bool {
            resultError = ec;
            resultStatus = status;
            resultData = data;
            completed = true;
            return false;  // don't retry
        },
        j);

    // Drive the io_context without blocking indefinitely so timeout
    // scenarios cannot wedge the test harness.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!completed && std::chrono::steady_clock::now() < deadline)
    {
        if (server.ioc().poll_one() == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    server.stop();

    while (server.ioc().poll_one() > 0)
        ;

    return completed;
}

}  // anonymous namespace

TEST(HTTPClient, case_insensitive_content_length)
{
    // Test different cases of Content-Length header
    std::vector<std::string> headerCases = {
        "Content-Length",  // Standard case
        "content-length",  // Lowercase - this tests the regex icase fix
        "CONTENT-LENGTH",  // Uppercase
        "Content-length",  // Mixed case
        "content-Length"   // Mixed case 2
    };

    for (auto const& headerName : headerCases)
    {
        TestHTTPServer server;
        std::string testBody = "Hello World!";
        server.setResponseBody(testBody);
        server.setHeader(headerName, std::to_string(testBody.size()));

        bool completed{false};
        int resultStatus{0};
        std::string resultData;
        boost::system::error_code resultError;

        bool testCompleted =
            runHTTPTest(server, "/test", completed, resultStatus, resultData, resultError);

        // Verify results
        EXPECT_TRUE(testCompleted);
        EXPECT_FALSE(resultError);
        EXPECT_EQ(resultStatus, 200);
        EXPECT_EQ(resultData, testBody);
    }
}

TEST(HTTPClient, basic_http_request)
{
    TestHTTPServer server;
    std::string testBody = "Test response body";
    server.setResponseBody(testBody);
    server.setHeader("Content-Type", "text/plain");

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted =
        runHTTPTest(server, "/basic", completed, resultStatus, resultData, resultError);

    EXPECT_TRUE(testCompleted);
    EXPECT_FALSE(resultError);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_EQ(resultData, testBody);
}

TEST(HTTPClient, empty_response)
{
    TestHTTPServer server;
    server.setResponseBody("");  // Empty body
    server.setHeader("Content-Length", "0");

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted =
        runHTTPTest(server, "/empty", completed, resultStatus, resultData, resultError);

    EXPECT_TRUE(testCompleted);
    EXPECT_FALSE(resultError);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_TRUE(resultData.empty());
}

TEST(HTTPClient, different_status_codes)
{
    std::vector<unsigned int> statusCodes = {200, 404, 500};

    for (auto status : statusCodes)
    {
        TestHTTPServer server;
        server.setStatusCode(status);
        server.setResponseBody("Status " + std::to_string(status));

        bool completed{false};
        int resultStatus{0};
        std::string resultData;
        boost::system::error_code resultError;

        bool testCompleted =
            runHTTPTest(server, "/status", completed, resultStatus, resultData, resultError);

        EXPECT_TRUE(testCompleted);
        EXPECT_FALSE(resultError);
        EXPECT_EQ(resultStatus, static_cast<int>(status));
    }
}

TEST(HTTPClient, eof_without_content_length)
{
    TestHTTPServer server;
    std::string testBody = "EOF delimited body";
    server.setResponseBody(testBody);
    server.setNoContentLength(true);

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted =
        runHTTPTest(server, "/eof", completed, resultStatus, resultData, resultError);

    // EOF-delimited responses complete via connection close, which
    // may report an EOF error code.  The important thing is that the
    // callback fires and delivers the data.
    EXPECT_TRUE(testCompleted);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_EQ(resultData, testBody);
}

TEST(HTTPClient, eof_without_content_length_split_body_close)
{
    TestHTTPServer server;
    std::string testBody = "EOF body split across writes";
    server.setResponseBody(testBody);
    server.setSplitEofBody(5, std::chrono::milliseconds(20));

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted =
        runHTTPTest(server, "/eof-split", completed, resultStatus, resultData, resultError);

    EXPECT_TRUE(testCompleted);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_EQ(resultData, testBody);
}

TEST(HTTPClient, connection_refused)
{
    // Bind to get a port, then close so nothing is listening
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acc(
        ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    auto port = acc.local_endpoint().port();
    acc.close();

    beast::Journal j{TestSink::instance()};
    HTTPClient::initializeSSLContext("", "", false, j);

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    HTTPClient::get(
        false,
        ioc,
        "127.0.0.1",
        port,
        "/test",
        1024,
        std::chrono::seconds(5),
        [&](boost::system::error_code const& ec, int status, std::string const& data) -> bool {
            resultError = ec;
            resultStatus = status;
            resultData = data;
            completed = true;
            return false;
        },
        j);

    ioc.run();

    EXPECT_TRUE(completed);
    EXPECT_TRUE(resultError);
}

TEST(HTTPClient, timeout_no_response)
{
    TestHTTPServer server;
    server.setSendResponse(false);

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted = runHTTPTest(
        server,
        "/timeout",
        completed,
        resultStatus,
        resultData,
        resultError,
        std::chrono::seconds(1));

    EXPECT_TRUE(testCompleted);
    EXPECT_TRUE(resultError);
}

TEST(HTTPClient, server_close_before_response)
{
    TestHTTPServer server;
    server.setCloseImmediately(true);

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool testCompleted =
        runHTTPTest(server, "/close", completed, resultStatus, resultData, resultError);

    EXPECT_TRUE(testCompleted);
    EXPECT_TRUE(resultError);
}

TEST(HTTPClient, concurrent_request_cleanup_success)
{
    constexpr int numRequests = 20;
    TestHTTPServer server;
    server.setStatusCode(200);
    server.setResponseBody("{}");
    server.setResponseDelay(std::chrono::milliseconds(5));

    beast::Journal j{TestSink::instance()};
    HTTPClient::initializeSSLContext("", "", false, j);

    std::atomic<int> completed{0};
    for (int i = 0; i < numRequests; ++i)
    {
        HTTPClient::get(
            false,
            server.ioc(),
            "127.0.0.1",
            server.port(),
            "/concurrent-success",
            1024,
            std::chrono::seconds(5),
            [&completed](boost::system::error_code const&, int, std::string const&) -> bool {
                ++completed;
                return false;
            },
            j);
    }

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (completed.load() < numRequests && std::chrono::steady_clock::now() < deadline)
        server.ioc().run_one();

    EXPECT_EQ(completed.load(), numRequests);

    server.stop();
    while (server.ioc().poll_one() > 0)
        ;
    EXPECT_EQ(server.activeConnectionCount(), 0);
}

TEST(HTTPClient, concurrent_request_cleanup_500)
{
    constexpr int numRequests = 20;
    TestHTTPServer server;
    server.setStatusCode(500);
    server.setResponseBody("{}");
    server.setResponseDelay(std::chrono::milliseconds(5));

    beast::Journal j{TestSink::instance()};
    HTTPClient::initializeSSLContext("", "", false, j);

    std::atomic<int> completed{0};
    for (int i = 0; i < numRequests; ++i)
    {
        HTTPClient::get(
            false,
            server.ioc(),
            "127.0.0.1",
            server.port(),
            "/concurrent-500",
            1024,
            std::chrono::seconds(5),
            [&completed](boost::system::error_code const&, int, std::string const&) -> bool {
                ++completed;
                return false;
            },
            j);
    }

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (completed.load() < numRequests && std::chrono::steady_clock::now() < deadline)
        server.ioc().run_one();

    EXPECT_EQ(completed.load(), numRequests);

    server.stop();
    while (server.ioc().poll_one() > 0)
        ;
    EXPECT_EQ(server.activeConnectionCount(), 0);
}

TEST(HTTPClient, persistent_io_context_cleanup)
{
    TestHTTPServer server;
    server.setStatusCode(200);
    server.setResponseBody("{}");

    beast::Journal j{TestSink::instance()};
    HTTPClient::initializeSSLContext("", "", false, j);

    auto& ioc = server.ioc();
    auto work = boost::asio::make_work_guard(ioc);
    std::atomic<bool> completed{false};
    std::thread runner([&ioc] { ioc.run(); });

    HTTPClient::get(
        false,
        ioc,
        "127.0.0.1",
        server.port(),
        "/persistent",
        1024,
        std::chrono::seconds(5),
        [&completed](boost::system::error_code const&, int, std::string const&) -> bool {
            completed = true;
            return false;
        },
        j);

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!completed.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_TRUE(completed.load());

    server.stop();
    work.reset();
    ioc.stop();
    runner.join();
    EXPECT_EQ(server.activeConnectionCount(), 0);
}
