#include <xrpl/basics/Log.h>
#include <xrpl/net/HTTPClient.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <atomic>
#include <map>
#include <memory>
#include <semaphore>
#include <thread>

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
        XRPL_ASSERT(finished(), "xrpl::TestHTTPServer::~TestHTTPServer : accept future ready");
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
    stop()
    {
        running_ = false;
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
                auto socket = co_await acceptor_.async_accept(boost::asio::use_awaitable);

                if (!running_)
                    break;

                // Handle this connection
                co_await handleConnection(std::move(socket));
            }
            catch (std::exception const& e)
            {
                // Accept or handle failed, stop accepting
                JLOG(j_.debug()) << "Error: " << e.what();
                break;
            }
        }

        finished_ = true;
    }

    boost::asio::awaitable<void>
    handleConnection(boost::asio::ip::tcp::socket socket)
    {
        try
        {
            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::string_body> req;

            // Read the HTTP request asynchronously
            co_await boost::beast::http::async_read(
                socket, buffer, req, boost::asio::use_awaitable);

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

            // Send response asynchronously
            co_await boost::beast::http::async_write(socket, res, boost::asio::use_awaitable);

            // Shutdown socket gracefully
            boost::system::error_code shutdownEc;

            // NOLINTNEXTLINE(bugprone-unused-return-value)
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, shutdownEc);
        }
        catch (std::exception const& e)
        {
            // Error reading or writing, just close the connection
            JLOG(j_.debug()) << "Connection error: " << e.what();
        }
    }
};

}  // anonymous namespace

// Test fixture that manages the SSL context lifecycle via RAII.
// SetUp() initializes the context before each test and TearDown()
// cleans it up afterwards, so individual tests don't need to worry
// about resource management.
class HTTPClientTest : public ::testing::Test
{
protected:
    // Shared journal for SSL context initialization and HTTP requests.
    beast::Journal j_{TestSink::instance()};

    // Initialize the global SSL context used by HTTPClient.
    void
    SetUp() override
    {
        HTTPClient::initializeSSLContext(
            "" /* sslVerifyDir*/, "" /*sslVerifyFile */, false /* sslVerify */, j_ /* journal */);
    }

    // Release the global SSL context to prevent memory leaks.
    void
    TearDown() override
    {
        HTTPClient::cleanupSSLContext();
    }

    // Issue an HTTP GET to the given test server and drive the
    // io_context until a response arrives or a timeout is reached.
    // Returns true when the completion handler was invoked.
    bool
    runHTTPTest(
        TestHTTPServer& server,
        std::string const& path,
        bool& completed,
        int& resultStatus,
        std::string& resultData,
        boost::system::error_code& resultError)
    {
        HTTPClient::get(
            false,  // no SSL
            server.ioc(),
            "127.0.0.1",
            server.port(),
            path,
            1024,  // max response size
            std::chrono::seconds(5),
            [&](boost::system::error_code const& ec, int status, std::string const& data) -> bool {
                resultError = ec;
                resultStatus = status;
                resultData = data;
                completed = true;
                return false;  // don't retry
            },
            j_);

        // Run the IO context until completion
        auto start = std::chrono::steady_clock::now();
        while (server.ioc().run_one() != 0)
        {
            if (std::chrono::steady_clock::now() - start >= std::chrono::seconds(10) ||
                server.finished())
            {
                break;
            }

            if (completed)
            {
                server.stop();
            }
        }

        // Drain any remaining handlers to ensure proper cleanup of HTTPClientImp
        server.ioc().poll();

        return completed;
    }
};

TEST_F(HTTPClientTest, case_insensitive_content_length)
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

TEST_F(HTTPClientTest, basic_http_request)
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

TEST_F(HTTPClientTest, empty_response)
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

TEST_F(HTTPClientTest, different_status_codes)
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
