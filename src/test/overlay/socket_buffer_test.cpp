#include <xrpl/beast/unit_test.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>

namespace xrpl {
namespace test {

class socket_buffer_test : public beast::unit_test::Suite
{
public:
    void
    testSocketBufferSize()
    {
        testcase("Socket Buffer Size Configuration");

        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);

        // Open the socket (need to open before setting options)
        boost::system::error_code ec;
        socket.open(boost::asio::ip::tcp::v4(), ec);
        BEAST_EXPECT(!ec);

        if (ec)
        {
            fail("Failed to open socket: " + ec.message());
            return;
        }

        // Test setting 1MB buffer sizes (same as PeerImp)
        constexpr int targetBufferSize = 1024 * 1024;  // 1 MB

        // Set send buffer size
        socket.set_option(
            boost::asio::socket_base::send_buffer_size(targetBufferSize), ec);

        if (ec)
        {
            fail("Failed to set send buffer size: " + ec.message());
        }
        else
        {
            pass();
        }

        // Set receive buffer size
        socket.set_option(
            boost::asio::socket_base::receive_buffer_size(targetBufferSize), ec);

        if (ec)
        {
            fail("Failed to set receive buffer size: " + ec.message());
        }
        else
        {
            pass();
        }

        // Verify send buffer size was actually set
        boost::asio::socket_base::send_buffer_size sendBufSize;
        socket.get_option(sendBufSize, ec);

        if (ec)
        {
            fail("Failed to get send buffer size: " + ec.message());
        }
        else
        {
            log << "Send buffer size set to: " << sendBufSize.value()
                << " bytes (requested: " << targetBufferSize << ")" << std::endl;

            // OS may set it to a different value, but should be >= requested
            // or at least larger than default 128KB
            BEAST_EXPECT(sendBufSize.value() >= 131072);  // At least 128KB
        }

        // Verify receive buffer size was actually set
        boost::asio::socket_base::receive_buffer_size recvBufSize;
        socket.get_option(recvBufSize, ec);

        if (ec)
        {
            fail("Failed to get receive buffer size: " + ec.message());
        }
        else
        {
            log << "Receive buffer size set to: " << recvBufSize.value()
                << " bytes (requested: " << targetBufferSize << ")" << std::endl;

            // OS may set it to a different value, but should be >= requested
            // or at least larger than default 128KB
            BEAST_EXPECT(recvBufSize.value() >= 131072);  // At least 128KB
        }

        socket.close(ec);
    }

    void
    testDefaultBufferSize()
    {
        testcase("Default Socket Buffer Size");

        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);

        boost::system::error_code ec;
        socket.open(boost::asio::ip::tcp::v4(), ec);
        BEAST_EXPECT(!ec);

        if (ec)
        {
            fail("Failed to open socket: " + ec.message());
            return;
        }

        // Check default buffer sizes
        boost::asio::socket_base::send_buffer_size sendBufSize;
        socket.get_option(sendBufSize, ec);

        if (!ec)
        {
            log << "Default send buffer size: " << sendBufSize.value()
                << " bytes" << std::endl;
        }

        boost::asio::socket_base::receive_buffer_size recvBufSize;
        socket.get_option(recvBufSize, ec);

        if (!ec)
        {
            log << "Default receive buffer size: " << recvBufSize.value()
                << " bytes" << std::endl;
        }

        socket.close(ec);
    }

    void
    run() override
    {
        testDefaultBufferSize();
        testSocketBufferSize();
    }
};

BEAST_DEFINE_TESTSUITE(socket_buffer, overlay, xrpl);

}  // namespace test
}  // namespace xrpl
