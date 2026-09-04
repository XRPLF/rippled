#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/resource/Consumer.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace xrpl {

/**
 * Manages an outbound connection attempt.
 */
class ConnectAttempt : public OverlayImpl::Child,
                       public std::enable_shared_from_this<ConnectAttempt>
{
private:
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;
    using RequestType = boost::beast::http::request<boost::beast::http::empty_body>;
    using ResponseType = boost::beast::http::response<boost::beast::http::dynamic_body>;

    using SocketType = boost::asio::ip::tcp::socket;
    using MiddleType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<MiddleType>;
    using SharedContext = std::shared_ptr<boost::asio::ssl::context>;

    Application& app_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    EndpointType remoteEndpoint_;
    resource::Consumer usage_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::unique_ptr<StreamType> streamPtr_;
    SocketType& socket_;
    StreamType& stream_;
    boost::beast::multi_buffer readBuf_;
    ResponseType response_;
    std::shared_ptr<peer_finder::Slot> slot_;
    RequestType req_;

public:
    ConnectAttempt(
        Application& app,
        boost::asio::io_context& ioContext,
        EndpointType remoteEndpoint,
        resource::Consumer usage,
        SharedContext const& context,
        Peer::IdT id,
        std::shared_ptr<peer_finder::Slot> const& slot,
        beast::Journal journal,
        OverlayImpl& overlay);

    ~ConnectAttempt() override;

    void
    stop() override;

    void
    run();

private:
    void
    close();
    void
    fail(std::string const& reason);
    void
    fail(std::string const& name, ErrorCode ec);
    void
    setTimer();
    void
    cancelTimer();
    void
    onTimer(ErrorCode ec);
    void
    onConnect(ErrorCode ec);
    void
    onHandshake(ErrorCode ec);
    void
    onWrite(ErrorCode ec);
    void
    onRead(ErrorCode ec);
    void
    onShutdown(ErrorCode ec);
    void
    processResponse();

    template <class = void>
    static boost::asio::ip::tcp::endpoint
    parseEndpoint(std::string const& s, boost::system::error_code& ec)
    {
        beast::ip::Endpoint bep;
        std::istringstream is(s);
        is >> bep;
        if (is.fail())
        {
            ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
            return boost::asio::ip::tcp::endpoint{};
        }

        return beast::IPAddressConversion::toAsioEndpoint(bep);
    }
};

}  // namespace xrpl
