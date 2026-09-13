#include <test/jtx/envconfig.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/unit_test.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <condition_variable>
#include <functional>
#include <thread>
#include <utility>

namespace xrpl {
namespace test {

/*
 * This test replicates the "stream truncated" error that occurs when:
 * 1. A peer starts sending a large message (e.g., quantum signature validation)
 * 2. The TCP connection is abruptly closed mid-transmission
 * 3. The receiving peer's onReadMessage gets an incomplete message
 */

class stream_truncated_test : public beast::unit_test::Suite
{
private:
    using io_context_type = boost::asio::io_context;
    using strand_type = boost::asio::strand<io_context_type::executor_type>;
    using timer_type =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using acceptor_type = boost::asio::ip::tcp::acceptor;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream<socket_type&>;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

    io_context_type io_context_;
    boost::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;
    std::shared_ptr<boost::asio::ssl::context> context_;

    // Track what happens when stream truncated occurs
    std::atomic<bool> stream_truncated_detected_{false};
    std::atomic<bool> connection_closed_cleanly_{false};
    std::string last_error_message_;
    std::mutex error_mutex_;

    //--------------------------------------------------------------------------
    // Server that sends partial message then abruptly closes
    //--------------------------------------------------------------------------

    class Server
    {
    private:
        stream_truncated_test& test_;
        acceptor_type acceptor_;
        socket_type socket_;
        stream_type stream_;
        strand_type strand_;
        timer_type timer_;
        std::vector<uint8_t> large_message_;

    public:
        explicit Server(stream_truncated_test& test)
            : test_(test)
            , acceptor_(
                  test_.io_context_,
                  endpoint_type(
                      boost::asio::ip::make_address(getEnvLocalhostAddr()),
                      0))
            , socket_(test_.io_context_)
            , stream_(socket_, *test_.context_)
            , strand_(boost::asio::make_strand(test_.io_context_))
            , timer_(test_.io_context_)
        {
            acceptor_.listen();
        }

        endpoint_type
        endpoint() const
        {
            return acceptor_.local_endpoint();
        }

        void
        run()
        {
            // Create a large message buffer simulating a quantum signature
            // validation message (roughly 50KB)
            large_message_.resize(50000);
            for (size_t i = 0; i < large_message_.size(); ++i)
                large_message_[i] = static_cast<uint8_t>(i % 256);

            acceptor_.async_accept(
                socket_,
                bind_executor(
                    strand_,
                    [this](error_code ec) {
                        if (ec)
                        {
                            test_.log << "[server] accept failed: "
                                      << ec.message() << std::endl;
                            return;
                        }
                        on_accept();
                    }));
        }

        void
        on_accept()
        {
            test_.log << "[server] Connection accepted" << std::endl;

            timer_.expires_after(std::chrono::seconds(3));
            timer_.async_wait(bind_executor(
                strand_,
                [this](error_code ec) {
                    if (!ec)
                    {
                        test_.log << "[server] Timeout" << std::endl;
                        socket_.close();
                    }
                }));

            stream_.async_handshake(
                stream_type::server,
                bind_executor(
                    strand_,
                    [this](error_code ec) {
                        if (ec)
                        {
                            test_.log << "[server] handshake failed: "
                                      << ec.message() << std::endl;
                            return;
                        }
                        on_handshake();
                    }));
        }

        void
        on_handshake()
        {
            test_.log << "[server] Handshake completed, sending partial large "
                         "message..."
                      << std::endl;

            // Send only the first 30KB of the message, then abruptly close
            // This simulates a truncated stream scenario
            auto partial_buffer =
                boost::asio::buffer(large_message_.data(), 30000);

            boost::asio::async_write(
                stream_,
                partial_buffer,
                bind_executor(
                    strand_,
                    [this](error_code ec, std::size_t bytes_written) {
                        if (ec)
                        {
                            test_.log << "[server] write failed: "
                                      << ec.message() << std::endl;
                            return;
                        }

                        test_.log
                            << "[server] Wrote " << bytes_written
                            << " bytes, now ABRUPTLY CLOSING (no shutdown)"
                            << std::endl;

                        // Abruptly close the socket WITHOUT async_shutdown
                        // This triggers "stream truncated" on the client side
                        error_code close_ec;
                        socket_.close(close_ec);
                        timer_.cancel();

                        test_.log << "[server] Socket forcibly closed"
                                  << std::endl;
                    }));
        }

        void
        close()
        {
            error_code ec;
            socket_.close(ec);
            acceptor_.close(ec);
            timer_.cancel();
        }
    };

    //--------------------------------------------------------------------------
    // Client that tries to read the message
    //--------------------------------------------------------------------------

    class Client
    {
    private:
        stream_truncated_test& test_;
        socket_type socket_;
        stream_type stream_;
        strand_type strand_;
        timer_type timer_;
        std::vector<uint8_t> read_buffer_;

    public:
        Client(stream_truncated_test& test, endpoint_type const& ep)
            : test_(test)
            , socket_(test_.io_context_)
            , stream_(socket_, *test_.context_)
            , strand_(boost::asio::make_strand(test_.io_context_))
            , timer_(test_.io_context_)
        {
            timer_.expires_after(std::chrono::seconds(5));
            timer_.async_wait(bind_executor(
                strand_,
                [this](error_code ec) {
                    if (!ec)
                    {
                        test_.log << "[client] Timeout waiting for data"
                                  << std::endl;
                        socket_.close();
                    }
                }));

            socket_.async_connect(
                ep,
                bind_executor(
                    strand_,
                    [this](error_code ec) {
                        if (ec)
                        {
                            test_.log << "[client] connect failed: "
                                      << ec.message() << std::endl;
                            return;
                        }
                        on_connect();
                    }));
        }

        void
        on_connect()
        {
            test_.log << "[client] Connected, starting handshake" << std::endl;

            stream_.async_handshake(
                stream_type::client,
                bind_executor(
                    strand_,
                    [this](error_code ec) {
                        if (ec)
                        {
                            test_.log << "[client] handshake failed: "
                                      << ec.message() << std::endl;
                            return;
                        }
                        on_handshake();
                    }));
        }

        void
        on_handshake()
        {
            test_.log << "[client] Handshake completed, reading data..."
                      << std::endl;

            // Start reading - this should eventually get "stream truncated"
            read_some();
        }

        void
        read_some()
        {
            read_buffer_.resize(8192);
            stream_.async_read_some(
                boost::asio::buffer(read_buffer_),
                bind_executor(
                    strand_,
                    [this](error_code ec, std::size_t bytes_transferred) {
                        on_read(ec, bytes_transferred);
                    }));
        }

        void
        on_read(error_code ec, std::size_t bytes_transferred)
        {
            if (ec)
            {
                // This is where we expect to see "stream truncated"
                std::string error_msg = ec.message();
                test_.log << "[client] READ ERROR: " << error_msg
                          << " (category: " << ec.category().name() << ")"
                          << std::endl;

                {
                    std::lock_guard<std::mutex> lock(test_.error_mutex_);
                    test_.last_error_message_ = error_msg;
                }

                // Check if this is the stream_truncated error
                if (error_msg.find("truncat") != std::string::npos ||
                    ec == boost::asio::ssl::error::stream_truncated)
                {
                    test_.stream_truncated_detected_ = true;
                    test_.log
                        << "[client] *** STREAM TRUNCATED ERROR DETECTED ***"
                        << std::endl;
                }
                else if (ec == boost::asio::error::eof)
                {
                    test_.log << "[client] Got EOF (clean close)" << std::endl;
                }

                socket_.close();
                timer_.cancel();
                return;
            }

            test_.log << "[client] Read " << bytes_transferred << " bytes"
                      << std::endl;

            // Try to read more
            read_some();
        }

        void
        close()
        {
            error_code ec;
            socket_.close(ec);
            timer_.cancel();
        }
    };

public:
    stream_truncated_test()
        : work_(io_context_.get_executor())
        , thread_(std::thread([this]() {
            beast::setCurrentThreadName("io_context");
            this->io_context_.run();
        }))
        , context_(makeSslContext(""))
    {
    }

    ~stream_truncated_test()
    {
        work_.reset();
        thread_.join();
    }

    void
    testStreamTruncated()
    {
        testcase("Stream Truncated Error Simulation");

        Server server(*this);
        server.run();

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        Client client(*this, server.endpoint());

        // Wait for test to complete
        std::this_thread::sleep_for(std::chrono::seconds(4));

        server.close();
        client.close();

        // Verify we detected the stream truncated error
        log << "\n=== TEST RESULTS ===" << std::endl;
        log << "Stream truncated detected: "
            << (stream_truncated_detected_ ? "YES" : "NO") << std::endl;
        log << "Last error message: " << last_error_message_ << std::endl;

        // The test passes if we successfully triggered and detected
        // the stream truncated error
        BEAST_EXPECT(stream_truncated_detected_);
    }

    void
    run() override
    {
        testStreamTruncated();
    }
};

BEAST_DEFINE_TESTSUITE(stream_truncated, overlay, xrpl);

}  // namespace test
}  // namespace xrpl
