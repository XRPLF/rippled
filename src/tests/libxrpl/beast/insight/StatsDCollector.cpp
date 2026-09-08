#include <xrpl/beast/insight/StatsDCollector.h>

#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/detail/error_code.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

namespace beast::insight {

/**
 * Reads the datagrams a StatsDCollector sends, over loopback.
 *
 *   StatsDCollector ──UDP──> LoopbackStatsDServer
 *                                   │
 *                                   └── owns ──> boost::asio::io_context
 *
 * Binds an ephemeral port, so a caller must read port() and point the
 * collector at it. The collector flushes on a one-second timer, so receive()
 * takes a timeout rather than blocking forever.
 *
 * @code
 * // Primary use: read the one datagram a metric produces.
 * LoopbackStatsDServer server;
 * auto collector = StatsDCollector::make(
 *     ip::Endpoint::fromString("127.0.0.1:" + std::to_string(server.port())),
 *     "test",
 *     Journal(Journal::getNullSink()));
 * auto const gauge = collector->makeGauge("g");
 * EXPECT_EQ(server.receive(std::chrono::seconds(10)), "test.g:0|g\n");
 *
 * // Edge case: nothing was sent, so the wait runs out and returns empty.
 * EXPECT_EQ(server.receive(std::chrono::seconds(3)), std::string());
 * @endcode
 *
 * @note Not thread-safe, and receive() must not be called concurrently with
 * itself.
 * @note Returns one datagram per call. A test that needs several must call
 * receive() again; there is no accumulation.
 */
class LoopbackStatsDServer
{
public:
    LoopbackStatsDServer()
        : socket_(
              ioContext_,
              boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 0))
    {
    }

    /**
     * The loopback port to point the collector at.
     *
     * @return the ephemeral port this server is bound to.
     */
    [[nodiscard]] unsigned short
    port() const
    {
        return socket_.local_endpoint().port();
    }

    /**
     * Waits for one datagram.
     *
     * @param timeout How long to wait before giving up.
     * @return the datagram's bytes, or an empty string if none arrived in
     * time.
     */
    std::string
    receive(std::chrono::milliseconds timeout)
    {
        std::string received;
        socket_.async_receive(
            boost::asio::buffer(buffer_),
            [&received, this](boost::system::error_code const& ec, std::size_t bytes) {
                if (!ec)
                    received.assign(buffer_.data(), bytes);
            });
        ioContext_.restart();
        ioContext_.run_for(timeout);
        socket_.cancel();
        return received;
    }

private:
    /**
     * Drives the receive. Restarted per receive() call.
     */
    boost::asio::io_context ioContext_;

    /**
     * Bound to 127.0.0.1 on an ephemeral port for the object's lifetime.
     */
    boost::asio::ip::udp::socket socket_;

    /**
     * Landing space for one datagram. Sized well above the collector's
     * 1472-byte packet limit.
     */
    std::array<char, 2048> buffer_{};
};

/**
 * A gauge nobody touches still publishes its zero.
 *
 * A gauge is only marked dirty when its value changes, so a gauge left at zero
 * would otherwise never be sent and would never exist downstream. Absent and
 * zero must not look the same to an operator.
 */
TEST(StatsDCollector, UntouchedGaugePublishesInitialZero)
{
    LoopbackStatsDServer server;
    auto const address = ip::Endpoint::fromString("127.0.0.1:" + std::to_string(server.port()));

    auto collector = StatsDCollector::make(address, "test", Journal(Journal::getNullSink()));
    // Created and then left alone: no set(), no increment().
    auto const gauge = collector->makeGauge("untouched");

    EXPECT_EQ(server.receive(std::chrono::seconds(10)), std::string("test.untouched:0|g\n"));
}

/**
 * A counter nobody increments publishes nothing.
 *
 * This is the other half of the rule above, and it is why the fix is a gauge
 * starting dirty rather than a flush of everything on the first tick. A counter
 * reports events, so an unsent counter and a zero counter mean the same thing.
 */
TEST(StatsDCollector, UntouchedCounterPublishesNothing)
{
    LoopbackStatsDServer server;
    auto const address = ip::Endpoint::fromString("127.0.0.1:" + std::to_string(server.port()));

    auto collector = StatsDCollector::make(address, "test", Journal(Journal::getNullSink()));
    auto const counter = collector->makeCounter("untouched");

    // Three seconds spans several one-second flush ticks.
    EXPECT_EQ(server.receive(std::chrono::seconds(3)), std::string());
}

}  // namespace beast::insight
