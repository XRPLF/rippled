#include <xrpl/basics/Log.h>
#include <xrpl/net/HTTPClient.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <map>
#include <thread>

using namespace xrpl;

namespace {

struct logger
{
    std::string name;
    logger const* const parent = nullptr;
    static std::size_t depth;
    logger(std::string n) : name(n)
    {
        std::clog << indent() << name << " begin\n";
        ++depth;
    }

    logger(logger const& p, std::string n) : parent(&p), name(n)
    {
        std::clog << indent() << parent->name << " : " << name << " begin\n";
        ++depth;
    }

    ~logger()
    {
        --depth;
        if (parent)
            std::clog << indent() << parent->name << " : " << name << " end\n";
        else
            std::clog << indent() << name << " end\n";
    }

    std::string
    indent()
    {
        return std::string(depth, ' ');
    }
};
std::size_t logger::depth = 0;

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
    std::map<std::string, std::string> custom_headers_;
    std::string response_body_;
    unsigned int status_code_{200};

public:
    TestHTTPServer() : acceptor_(ioc_), port_(0)
    {
        logger l("TestHTTPServer()");
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
        logger l("~TestHTTPServer()");
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
        custom_headers_[name] = value;
    }

    void
    setResponseBody(std::string const& body)
    {
        response_body_ = body;
    }

    void
    setStatusCode(unsigned int code)
    {
        status_code_ = code;
    }

private:
    void
    stop()
    {
        logger l("TestHTTPServer::stop");
        running_ = false;
        acceptor_.close();
    }

    void
    accept()
    {
        logger l("TestHTTPServer::accept");
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
        logger l("TestHTTPServer::handleConnection");
        try
        {
            std::optional<logger> r(std::in_place, l, "read the http request");
            // Read the HTTP request
            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::string_body> req;
            boost::beast::http::read(socket, buffer, req);

            // Create response
            r.emplace(l, "create response");
            boost::beast::http::response<boost::beast::http::string_body> res;
            res.version(req.version());
            res.result(status_code_);
            res.set(boost::beast::http::field::server, "TestServer");

            // Add custom headers
            r.emplace(l, "add custom headers");
            for (auto const& [name, value] : custom_headers_)
            {
                res.set(name, value);
            }

            // Set body and prepare payload first
            r.emplace(l, "set body and prepare payload");
            res.body() = response_body_;
            res.prepare_payload();

            // Override Content-Length with custom headers after prepare_payload
            // This allows us to test case-insensitive header parsing
            r.emplace(l, "override content-length");
            for (auto const& [name, value] : custom_headers_)
            {
                if (boost::iequals(name, "Content-Length"))
                {
                    res.erase(boost::beast::http::field::content_length);
                    res.set(name, value);
                }
            }

            // Send response
            r.emplace(l, "send response");
            boost::beast::http::write(socket, res);

            // Shutdown socket gracefully
            r.emplace(l, "shutdown socket");
            boost::system::error_code ec;
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        }
        catch (std::exception const&)
        {
            // Connection handling errors are expected
            logger c(l, "catch");
        }

        if (running_)
        {
            logger r(l, "accept");
            accept();
        }
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
    logger l("runHTTPTest");
    // Create a null journal for testing
    beast::Journal j{beast::Journal::getNullSink()};

    std::optional<logger> r(std::in_place, l, "initializeSSLContext");

    // Initialize HTTPClient SSL context
    HTTPClient::initializeSSLContext("", "", false, j);

    r.emplace(l, "HTTPClient::get");
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
        r.emplace(l, "ioc.run_one");
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
    logger l("HTTPClient case insensitive Content-Length");

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
        logger h(l, header_name);
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
    logger l("HTTPClient basic HTTP request");
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
    logger l("HTTPClient empty response");
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
    logger l("HTTPClient different status codes");
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
