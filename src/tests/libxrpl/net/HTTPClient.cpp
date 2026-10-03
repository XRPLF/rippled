#include <xrpl/net/HTTPClient.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>  // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>  // IWYU pragma: keep
#include <boost/beast/core.hpp>       // IWYU pragma: keep
#include <boost/beast/http.hpp>       // IWYU pragma: keep

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <deque>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace xrpl;

namespace {

// Simple HTTP server using Beast for testing
class TestHTTPServer
{
public:
    /**
     * What the server does with a connection once it has read the request:
     * Reply serves the configured status, headers and body; Stall holds the
     * connection open and never replies; CloseAfterRead hangs up without
     * replying.
     */
    enum class Behaviour { Reply, Stall, CloseAfterRead };

private:
    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::endpoint endpoint_;
    bool running_{true};
    bool finished_{false};
    unsigned short port_{0};

    // Custom headers to return
    std::map<std::string, std::string> customHeaders_;
    std::string responseBody_;
    unsigned int statusCode_{200};

    /**
     * How a connection is handled once its request is read. Anything other
     * than Reply leaves the client to finish through its own error handling.
     */
    Behaviour behaviour_{Behaviour::Reply};

    /**
     * The socket of the most recent stalled connection. Holding it open sends
     * no EOF to the client, so the client's read stays pending until the
     * client's own deadline fires and closes the client side.
     */
    std::optional<boost::asio::ip::tcp::socket> stalledSocket_;

    beast::Journal j_;

public:
    TestHTTPServer() : acceptor_(ioc_), j_(TestSink::instance())
    {
        // Bind to a fixed loopback address (rather than 0.0.0.0) so that a
        // sibling loopback address such as 127.0.0.2 has no listener, which
        // the fallback test relies on.
        endpoint_ = {boost::asio::ip::make_address("127.0.0.1"), 0};
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

    [[nodiscard]] unsigned short
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

    /**
     * Choose what happens to each connection after its request is read.
     *
     * @param behaviour Reply to serve the configured response, Stall to hold
     * the connection open with no reply, CloseAfterRead to hang up instead.
     */
    void
    setBehaviour(Behaviour behaviour)
    {
        behaviour_ = behaviour;
    }

    void
    stop()
    {
        running_ = false;
        acceptor_.close();
    }

    [[nodiscard]] bool
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

            if (behaviour_ == Behaviour::Stall)
            {
                // Hold the connection open and never reply. Moving the socket
                // into a member keeps it alive after this coroutine returns,
                // so the client's read stays pending until its own deadline
                // fires. The accept loop continues and stop() can still end it.
                stalledSocket_.emplace(std::move(socket));
                co_return;
            }

            if (behaviour_ == Behaviour::CloseAfterRead)
            {
                // Hang up without replying, so the client's header read ends
                // with EOF and no bytes.
                socket.close();
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
    std::vector<std::string> const headerCases = {
        "Content-Length",  // Standard case
        "content-length",  // Lowercase - this tests the regex icase fix
        "CONTENT-LENGTH",  // Uppercase
        "Content-length",  // Mixed case
        "content-Length"   // Mixed case 2
    };

    for (auto const& headerName : headerCases)
    {
        TestHTTPServer server;
        std::string const testBody = "Hello World!";
        server.setResponseBody(testBody);
        server.setHeader(headerName, std::to_string(testBody.size()));

        bool completed{false};
        int resultStatus{0};
        std::string resultData;
        boost::system::error_code resultError;

        bool const testCompleted =
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
    std::string const testBody = "Test response body";
    server.setResponseBody(testBody);
    server.setHeader("Content-Type", "text/plain");

    bool completed{false};
    int resultStatus{0};
    std::string resultData;
    boost::system::error_code resultError;

    bool const testCompleted =
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

    bool const testCompleted =
        runHTTPTest(server, "/empty", completed, resultStatus, resultData, resultError);

    EXPECT_TRUE(testCompleted);
    EXPECT_FALSE(resultError);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_TRUE(resultData.empty());
}

TEST_F(HTTPClientTest, different_status_codes)
{
    std::vector<unsigned int> const statusCodes = {200, 404, 500};

    for (auto status : statusCodes)
    {
        TestHTTPServer server;
        server.setStatusCode(status);
        server.setResponseBody("Status " + std::to_string(status));

        bool completed{false};
        int resultStatus{0};
        std::string resultData;
        boost::system::error_code resultError;

        bool const testCompleted =
            runHTTPTest(server, "/status", completed, resultStatus, resultData, resultError);

        EXPECT_TRUE(testCompleted);
        EXPECT_FALSE(resultError);
        EXPECT_EQ(resultStatus, static_cast<int>(status));
    }
}

TEST_F(HTTPClientTest, request_times_out_on_stalled_peer)
{
    // A peer that reads the request but never replies must not leave the
    // client waiting: the request deadline fires and completes the handler
    // exactly once with timed_out. This depends on the deadline wait being
    // armed on the normal, non-throwing expires_after path.
    TestHTTPServer server;
    server.setBehaviour(TestHTTPServer::Behaviour::Stall);

    int completions{0};
    int resultStatus{-1};
    boost::system::error_code resultError;

    HTTPClient::get(
        false,  // no SSL
        server.ioc(),
        "127.0.0.1",
        server.port(),
        "/stall",
        1024,  // max response size
        std::chrono::seconds(1),
        [&](boost::system::error_code const& ec, int status, std::string const&) -> bool {
            resultError = ec;
            resultStatus = status;
            ++completions;
            // Close the acceptor so the accept loop ends and run_for drains.
            server.stop();
            return false;  // don't retry
        },
        j_);

    // Bounded wall-clock drive; the 1s deadline must fire well within this.
    server.ioc().run_for(std::chrono::seconds(4));
    // Stop unconditionally so the accept loop ends and the fixture's
    // finished() check holds even when the client never completes; a
    // regression then fails an EXPECT instead of aborting the binary.
    server.stop();
    server.ioc().poll();

    EXPECT_EQ(completions, 1);
    EXPECT_EQ(resultError, boost::asio::error::timed_out);
    EXPECT_EQ(resultStatus, 0);
    EXPECT_TRUE(server.finished());
}

TEST_F(HTTPClientTest, falls_back_to_next_site_after_connect_failure)
{
    // When the first site cannot be reached, the client must fall back to the
    // next site and report that site's result, not the first site's connect
    // error. This depends on shutdown_ being cleared at the start of each
    // attempt in httpsNext().
    TestHTTPServer server;
    std::string const testBody = "fallback body";
    server.setResponseBody(testBody);
    server.setHeader("Content-Length", std::to_string(testBody.size()));

    // First site: a loopback address the server does not listen on. Where the
    // whole 127/8 block is local (Linux) the connect is refused at once; where
    // 127.0.0.2 is not a configured loopback address (macOS) it is unreachable
    // and the 1 s deadline ends the attempt instead. Either way the client
    // must move on to the second site, which is the server.
    std::deque<std::string> const sites{"127.0.0.2", "127.0.0.1"};

    int completions{0};
    int resultStatus{-1};
    std::string resultData;
    boost::system::error_code resultError;

    HTTPClient::get(
        false,  // no SSL
        server.ioc(),
        sites,
        server.port(),
        "/fallback",
        1024,  // max response size
        std::chrono::seconds(1),
        [&](boost::system::error_code const& ec, int status, std::string const& data) -> bool {
            resultError = ec;
            resultStatus = status;
            resultData = data;
            ++completions;
            server.stop();
            return false;  // don't retry
        },
        j_);

    // Bounded wall-clock drive: worst case is one 1 s deadline on the first
    // site followed by the real exchange on the second.
    server.ioc().run_for(std::chrono::seconds(6));
    server.stop();
    server.ioc().poll();

    EXPECT_EQ(completions, 1);
    EXPECT_FALSE(resultError);
    EXPECT_EQ(resultStatus, 200);
    EXPECT_EQ(resultData, testBody);
    EXPECT_TRUE(server.finished());
}

TEST_F(HTTPClientTest, reports_read_error_when_peer_closes_without_reply)
{
    // A peer that reads the request and hangs up must surface the read error
    // itself, not a parse failure of the empty header buffer. This depends on
    // handleHeader() recording its own error code before it looks at the
    // buffer. The client timeout is longer than the drive window so that a
    // timeout cannot stand in for the read error.
    TestHTTPServer server;
    server.setBehaviour(TestHTTPServer::Behaviour::CloseAfterRead);

    int completions{0};
    int resultStatus{-1};
    boost::system::error_code resultError;

    HTTPClient::get(
        false,  // no SSL
        server.ioc(),
        "127.0.0.1",
        server.port(),
        "/hangup",
        1024,  // max response size
        std::chrono::seconds(10),
        [&](boost::system::error_code const& ec, int status, std::string const&) -> bool {
            resultError = ec;
            resultStatus = status;
            ++completions;
            server.stop();
            return false;  // don't retry
        },
        j_);

    server.ioc().run_for(std::chrono::seconds(4));
    server.stop();
    server.ioc().poll();

    EXPECT_EQ(completions, 1);
    EXPECT_EQ(resultError, boost::asio::error::eof);
    EXPECT_EQ(resultStatus, 0);
    EXPECT_TRUE(server.finished());
}
