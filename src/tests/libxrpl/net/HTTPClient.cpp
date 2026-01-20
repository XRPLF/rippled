#include <xrpl/basics/Log.h>
#include <xrpl/net/HTTPClient.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <gtest/gtest.h>
#include <helpers/DebugSink.h>

#include <atomic>
#include <map>
#include <memory>
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
    std::atomic<bool> running_{true};
    unsigned short port_;

    // Custom headers to return
    std::map<std::string, std::string> customHeaders_;
    std::string responseBody_;
    unsigned int statusCode_{200};

public:
    TestHTTPServer() : acceptor_(ioc_), port_(0)
    {
        // Bind to any available port
        endpoint_ = {boost::asio::ip::tcp::v4(), 0};
        acceptor_.open(endpoint_.protocol());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint_);
        acceptor_.listen();

        // Get the actual port that was assigned
        port_ = acceptor_.local_endpoint().port();

        accept();
    }

    ~TestHTTPServer()
    {
        stop();
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

private:
    void
    stop()
    {
        running_ = false;
        acceptor_.close();
    }

    void
    accept()
    {
        if (!running_)
            return;

        acceptor_.async_accept(
            ioc_,
            endpoint_,
            [&](boost::system::error_code const& error,
                boost::asio::ip::tcp::socket peer) {
                if (!running_)
                    return;

                if (!error)
                {
                    handleConnection(std::move(peer));
                }
            });
    }

    void
    handleConnection(boost::asio::ip::tcp::socket socket)
    {
        // Use async operations to avoid blocking the io_context thread
        // Use shared_ptr to keep objects alive during async operations
        auto socketPtr =
            std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket));
        auto buffer = std::make_shared<boost::beast::flat_buffer>();
        auto req = std::make_shared<
            boost::beast::http::request<boost::beast::http::string_body>>();

        // Read the HTTP request asynchronously
        boost::beast::http::async_read(
            *socketPtr,
            *buffer,
            *req,
            [this, socketPtr, buffer, req](
                boost::beast::error_code ec, std::size_t) {
                if (ec)
                {
                    // Error reading, just close the connection
                    return;
                }

                // Create response
                auto res = std::make_shared<boost::beast::http::response<
                    boost::beast::http::string_body>>();
                res->version(req->version());
                res->result(statusCode_);
                res->set(boost::beast::http::field::server, "TestServer");

                // Add custom headers
                for (auto const& [name, value] : customHeaders_)
                {
                    res->set(name, value);
                }

                // Set body and prepare payload first
                res->body() = responseBody_;
                res->prepare_payload();

                // Override Content-Length with custom headers after
                // prepare_payload This allows us to test case-insensitive
                // header parsing
                for (auto const& [name, value] : customHeaders_)
                {
                    if (boost::iequals(name, "Content-Length"))
                    {
                        res->erase(boost::beast::http::field::content_length);
                        res->set(name, value);
                    }
                }

                // Send response asynchronously
                boost::beast::http::async_write(
                    *socketPtr,
                    *res,
                    [socketPtr, res](boost::beast::error_code ec, std::size_t) {
                        // Shutdown socket gracefully
                        boost::system::error_code shutdownEc;
                        socketPtr->shutdown(
                            boost::asio::ip::tcp::socket::shutdown_send,
                            shutdownEc);
                        // Socket will close when shared_ptr is destroyed
                    });
            });
    }
};

// Helper function to run HTTP client test
bool
runHTTPTest(
    TestHTTPServer& server,
    std::string const& path,
    std::atomic<bool>& completed,
    std::atomic<int>& result_status,
    std::string& result_data,
    boost::system::error_code& result_error)
{
    // Create a null journal for testing
    beast::Journal j{DebugSink::instance()};

    // Initialize HTTPClient SSL context
    HTTPClient::initializeSSLContext("", "", false, j);

    HTTPClient::get(
        false,  // no SSL
        server.ioc(),
        "127.0.0.1",
        server.port(),
        path,
        1024,  // max response size
        std::chrono::seconds(5),
        [&](boost::system::error_code const& ec,
            int status,
            std::string const& data) -> bool {
            result_error = ec;
            result_status = status;
            result_data = data;
            completed = true;
            return false;  // don't retry
        },
        j);

    // Run the IO context until completion
    auto start = std::chrono::steady_clock::now();
    while (!completed &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(10))
    {
        if (server.ioc().run_one() == 0)
        {
            break;
        }
    }

    return completed;
}

}  // anonymous namespace

TEST(HTTPClient, case_insensitive_content_length)
{
    // Test different cases of Content-Length header
    std::vector<std::string> header_cases = {
        "Content-Length",  // Standard case
        "content-length",  // Lowercase - this tests the regex icase fix
        "CONTENT-LENGTH",  // Uppercase
        "Content-length",  // Mixed case
        "content-Length"   // Mixed case 2
    };

    for (auto const& header_name : header_cases)
    {
        TestHTTPServer server;
        std::string test_body = "Hello World!";
        server.setResponseBody(test_body);
        server.setHeader(header_name, std::to_string(test_body.size()));

        std::atomic<bool> completed{false};
        std::atomic<int> result_status{0};
        std::string result_data;
        boost::system::error_code result_error;

        bool test_completed = runHTTPTest(
            server,
            "/test",
            completed,
            result_status,
            result_data,
            result_error);

        // Verify results
        EXPECT_TRUE(test_completed);
        EXPECT_FALSE(result_error);
        EXPECT_EQ(result_status, 200);
        EXPECT_EQ(result_data, test_body);
    }
}

TEST(HTTPClient, basic_http_request)
{
    TestHTTPServer server;
    std::string test_body = "Test response body";
    server.setResponseBody(test_body);
    server.setHeader("Content-Type", "text/plain");

    std::atomic<bool> completed{false};
    std::atomic<int> result_status{0};
    std::string result_data;
    boost::system::error_code result_error;

    bool test_completed = runHTTPTest(
        server, "/basic", completed, result_status, result_data, result_error);

    EXPECT_TRUE(test_completed);
    EXPECT_FALSE(result_error);
    EXPECT_EQ(result_status, 200);
    EXPECT_EQ(result_data, test_body);
}

TEST(HTTPClient, empty_response)
{
    TestHTTPServer server;
    server.setResponseBody("");  // Empty body
    server.setHeader("Content-Length", "0");

    std::atomic<bool> completed{false};
    std::atomic<int> result_status{0};
    std::string result_data;
    boost::system::error_code result_error;

    bool test_completed = runHTTPTest(
        server, "/empty", completed, result_status, result_data, result_error);

    EXPECT_TRUE(test_completed);
    EXPECT_FALSE(result_error);
    EXPECT_EQ(result_status, 200);
    EXPECT_TRUE(result_data.empty());
}

TEST(HTTPClient, different_status_codes)
{
    std::vector<unsigned int> status_codes = {200, 404, 500};

    for (auto status : status_codes)
    {
        TestHTTPServer server;
        server.setStatusCode(status);
        server.setResponseBody("Status " + std::to_string(status));

        std::atomic<bool> completed{false};
        std::atomic<int> result_status{0};
        std::string result_data;
        boost::system::error_code result_error;

        bool test_completed = runHTTPTest(
            server,
            "/status",
            completed,
            result_status,
            result_data,
            result_error);

        EXPECT_TRUE(test_completed);
        EXPECT_FALSE(result_error);
        EXPECT_EQ(result_status, static_cast<int>(status));
    }
}
