#include <test/jtx/envconfig.h>

#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/unit_test/suite.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/optional/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace xrpl::test {

/*
 * This test replicates the "stream truncated" error that occurs when:
 * 1. A peer starts sending a large message (e.g., quantum signature validation)
 * 2. The TCP connection is abruptly closed mid-transmission
 * 3. The receiving peer's onReadMessage gets an incomplete message
 */

class stream_truncated_test : public beast::unit_test::Suite
{
private:
    using IoContextType = boost::asio::io_context;
    using StrandType = boost::asio::strand<IoContextType::executor_type>;
    using TimerType = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using AcceptorType = boost::asio::ip::tcp::acceptor;
    using SocketType = boost::asio::ip::tcp::socket;
    using StreamType = boost::asio::ssl::stream<SocketType&>;
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;

    IoContextType ioContext_;
    boost::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::thread thread_;
    std::shared_ptr<boost::asio::ssl::context> context_;

    // Track what happens when stream truncated occurs
    std::atomic<bool> streamTruncatedDetected_{false};
    std::atomic<bool> connectionClosedCleanly_{false};
    std::string lastErrorMessage_;
    std::mutex errorMutex_;

    //--------------------------------------------------------------------------
    // Server that sends partial message then abruptly closes
    //--------------------------------------------------------------------------

    class Server
    {
    private:
        stream_truncated_test& test_;
        AcceptorType acceptor_;
        SocketType socket_;
        StreamType stream_;
        StrandType strand_;
        TimerType timer_;
        std::vector<uint8_t> largeMessage_;

    public:
        explicit Server(stream_truncated_test& test)
            : test_(test)
            , acceptor_(
                  test_.ioContext_,
                  EndpointType(boost::asio::ip::make_address(getEnvLocalhostAddr()), 0))
            , socket_(test_.ioContext_)
            , stream_(socket_, *test_.context_)
            , strand_(boost::asio::make_strand(test_.ioContext_))
            , timer_(test_.ioContext_)
        {
            acceptor_.listen();
        }

        [[nodiscard]] EndpointType
        endpoint() const
        {
            return acceptor_.local_endpoint();
        }

        void
        run()
        {
            // Create a large message buffer simulating a quantum signature
            // validation message (roughly 50KB)
            largeMessage_.resize(50000);
            for (size_t i = 0; i < largeMessage_.size(); ++i)
                largeMessage_[i] = static_cast<uint8_t>(i % 256);

            acceptor_.async_accept(socket_, bind_executor(strand_, [this](ErrorCode ec) {
                                       if (ec)
                                       {
                                           test_.log << "[server] accept failed: " << ec.message()
                                                     << std::endl;
                                           return;
                                       }
                                       onAccept();
                                   }));
        }

        void
        onAccept()
        {
            test_.log << "[server] Connection accepted" << std::endl;

            timer_.expires_after(std::chrono::seconds(3));
            timer_.async_wait(bind_executor(strand_, [this](ErrorCode ec) {
                if (!ec)
                {
                    test_.log << "[server] Timeout" << std::endl;
                    socket_.close();
                }
            }));

            stream_.async_handshake(
                StreamType::server, bind_executor(strand_, [this](ErrorCode ec) {
                    if (ec)
                    {
                        test_.log << "[server] handshake failed: " << ec.message() << std::endl;
                        return;
                    }
                    onHandshake();
                }));
        }

        void
        onHandshake()
        {
            test_.log << "[server] Handshake completed, sending partial large "
                         "message..."
                      << std::endl;

            // Send only the first 30KB of the message, then abruptly close
            // This simulates a truncated stream scenario
            auto const partialBuffer = boost::asio::buffer(largeMessage_.data(), 30000);

            boost::asio::async_write(
                stream_,
                partialBuffer,
                bind_executor(strand_, [this](ErrorCode ec, std::size_t bytesWritten) {
                    if (ec)
                    {
                        test_.log << "[server] write failed: " << ec.message() << std::endl;
                        return;
                    }

                    test_.log << "[server] Wrote " << bytesWritten
                              << " bytes, now ABRUPTLY CLOSING (no shutdown)" << std::endl;

                    // Abruptly close the socket WITHOUT async_shutdown
                    // This triggers "stream truncated" on the client side
                    ErrorCode closeEc;
                    socket_.close(closeEc);
                    timer_.cancel();

                    test_.log << "[server] Socket forcibly closed" << std::endl;
                }));
        }

        void
        close()
        {
            ErrorCode ec;
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
        SocketType socket_;
        StreamType stream_;
        StrandType strand_;
        TimerType timer_;
        std::vector<uint8_t> readBuffer_;

    public:
        Client(stream_truncated_test& test, EndpointType const& ep)
            : test_(test)
            , socket_(test_.ioContext_)
            , stream_(socket_, *test_.context_)
            , strand_(boost::asio::make_strand(test_.ioContext_))
            , timer_(test_.ioContext_)
        {
            timer_.expires_after(std::chrono::seconds(5));
            timer_.async_wait(bind_executor(strand_, [this](ErrorCode ec) {
                if (!ec)
                {
                    test_.log << "[client] Timeout waiting for data" << std::endl;
                    socket_.close();
                }
            }));

            socket_.async_connect(ep, bind_executor(strand_, [this](ErrorCode ec) {
                                      if (ec)
                                      {
                                          test_.log << "[client] connect failed: " << ec.message()
                                                    << std::endl;
                                          return;
                                      }
                                      onConnect();
                                  }));
        }

        void
        onConnect()
        {
            test_.log << "[client] Connected, starting handshake" << std::endl;

            stream_.async_handshake(
                StreamType::client, bind_executor(strand_, [this](ErrorCode ec) {
                    if (ec)
                    {
                        test_.log << "[client] handshake failed: " << ec.message() << std::endl;
                        return;
                    }
                    onHandshake();
                }));
        }

        void
        onHandshake()
        {
            test_.log << "[client] Handshake completed, reading data..." << std::endl;

            // Start reading - this should eventually get "stream truncated"
            readSome();
        }

        void
        readSome()
        {
            readBuffer_.resize(8192);
            stream_.async_read_some(
                boost::asio::buffer(readBuffer_),
                bind_executor(strand_, [this](ErrorCode ec, std::size_t bytesTransferred) {
                    onRead(ec, bytesTransferred);
                }));
        }

        void
        onRead(ErrorCode ec, std::size_t bytesTransferred)
        {
            if (ec)
            {
                // This is where we expect to see "stream truncated"
                std::string const errorMsg = ec.message();
                test_.log << "[client] READ ERROR: " << errorMsg
                          << " (category: " << ec.category().name() << ")" << std::endl;

                {
                    std::scoped_lock const lock(test_.errorMutex_);
                    test_.lastErrorMessage_ = errorMsg;
                }

                // Check if this is the stream_truncated error
                if (errorMsg.contains("truncat") || ec == boost::asio::ssl::error::stream_truncated)
                {
                    test_.streamTruncatedDetected_ = true;
                    test_.log << "[client] *** STREAM TRUNCATED ERROR DETECTED ***" << std::endl;
                }
                else if (ec == boost::asio::error::eof)
                {
                    test_.log << "[client] Got EOF (clean close)" << std::endl;
                }

                socket_.close();
                timer_.cancel();
                return;
            }

            test_.log << "[client] Read " << bytesTransferred << " bytes" << std::endl;

            // Try to read more
            readSome();
        }

        void
        close()
        {
            ErrorCode ec;
            socket_.close(ec);
            timer_.cancel();
        }
    };

public:
    stream_truncated_test()
        : work_(ioContext_.get_executor())
        , thread_(std::thread([this] {
            beast::setCurrentThreadName("io_context");
            this->ioContext_.run();
        }))
        , context_(makeSslContext(""))
    {
    }

    ~stream_truncated_test() override
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
        log << "Stream truncated detected: " << (streamTruncatedDetected_ ? "YES" : "NO")
            << std::endl;
        log << "Last error message: " << lastErrorMessage_ << std::endl;

        // The test passes if we successfully triggered and detected
        // the stream truncated error
        BEAST_EXPECT(streamTruncatedDetected_);
    }

    void
    run() override
    {
        testStreamTruncated();
    }
};

BEAST_DEFINE_TESTSUITE(stream_truncated, overlay, xrpl);

}  // namespace xrpl::test
