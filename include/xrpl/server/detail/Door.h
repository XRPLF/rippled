#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/server/detail/PlainHTTPPeer.h>
#include <xrpl/server/detail/SSLHTTPPeer.h>
#include <xrpl/server/detail/io_list.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/detect_ssl.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/predef.h>

#if !BOOST_OS_WINDOWS
#include <sys/resource.h>

#include <dirent.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace xrpl {

/** A listening socket. */
template <class Handler>
class Door : public IOList::Work, public std::enable_shared_from_this<Door<Handler>>
{
private:
    using clock_type = std::chrono::steady_clock;
    using timer_type = boost::asio::basic_waitable_timer<clock_type>;
    using error_code = boost::system::error_code;
    using yield_context = boost::asio::yield_context;
    using protocol_type = boost::asio::ip::tcp;
    using acceptor_type = protocol_type::acceptor;
    using endpoint_type = protocol_type::endpoint;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::beast::tcp_stream;

    // Detects SSL on a socket
    class Detector : public IOList::Work, public std::enable_shared_from_this<Detector>
    {
    private:
        Port const& port_;
        Handler& handler_;
        boost::asio::io_context& ioc_;
        stream_type stream_;
        socket_type& socket_;
        endpoint_type remoteAddress_;
        boost::asio::strand<boost::asio::io_context::executor_type> strand_;
        beast::Journal const j_;

    public:
        Detector(
            Port const& port,
            Handler& handler,
            boost::asio::io_context& ioc,
            stream_type&& stream,
            endpoint_type remoteAddress,
            beast::Journal j);
        void
        run();
        void
        close() override;

    private:
        void
        doDetect(yield_context yield);
    };

    beast::Journal const j_;
    Port const& port_;
    Handler& handler_;
    boost::asio::io_context& ioc_;
    acceptor_type acceptor_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    bool ssl_{
        port_.protocol.count("https") > 0 || port_.protocol.count("wss") > 0 ||
        port_.protocol.count("wss2") > 0 || port_.protocol.count("peer") > 0};
    bool plain_{
        port_.protocol.count("http") > 0 || port_.protocol.count("ws") > 0 ||
        (port_.protocol.count("ws2") != 0u)};
    static constexpr std::chrono::milliseconds kInitialAcceptDelay{50};
    static constexpr std::chrono::milliseconds kMaxAcceptDelay{2000};
    std::chrono::milliseconds acceptDelay_{kInitialAcceptDelay};
    boost::asio::steady_timer backoffTimer_;
    static constexpr double kFreeFdThreshold = 0.70;

    struct FDStats
    {
        std::uint64_t used{0};
        std::uint64_t limit{0};
    };

    void
    reOpen();

    std::optional<FDStats>
    queryFdStats() const;

    bool
    shouldThrottleForFds();

public:
    Door(Handler& handler, boost::asio::io_context& ioContext, Port const& port, beast::Journal j);

    // Work-around because we can't call shared_from_this in ctor
    void
    run();

    /** Close the Door listening socket and connections.
        The listening socket is closed, and all open connections
        belonging to the Door are closed.
        Thread Safety:
            May be called concurrently
    */
    void
    close() override;

    [[nodiscard]] endpoint_type
    getEndpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    template <class ConstBufferSequence>
    void
    create(
        bool ssl,
        ConstBufferSequence const& buffers,
        stream_type&& stream,
        endpoint_type remoteAddress);

    void
    doAccept(yield_context yield);
};

template <class Handler>
Door<Handler>::Detector::Detector(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    stream_type&& stream,
    endpoint_type remoteAddress,
    beast::Journal j)
    : port_(port)
    , handler_(handler)
    , ioc_(ioc)
    , stream_(std::move(stream))
    , socket_(stream_.socket())
    , remoteAddress_(std::move(remoteAddress))
    , strand_(boost::asio::make_strand(ioc_))
    , j_(j)
{
}

template <class Handler>
void
Door<Handler>::Detector::run()
{
    util::spawn(
        strand_, std::bind(&Detector::doDetect, this->shared_from_this(), std::placeholders::_1));
}

template <class Handler>
void
Door<Handler>::Detector::close()
{
    stream_.close();
}

template <class Handler>
void
Door<Handler>::Detector::doDetect(boost::asio::yield_context doYield)
{
    boost::beast::multi_buffer buf(16);
    stream_.expires_after(std::chrono::seconds(15));
    boost::system::error_code ec;
    bool const ssl = async_detect_ssl(stream_, buf, doYield[ec]);
    stream_.expires_never();
    if (!ec)
    {
        if (ssl)
        {
            if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                    port_, handler_, ioc_, j_, remoteAddress_, buf.data(), std::move(stream_)))
                sp->run();
            return;
        }
        if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
                port_, handler_, ioc_, j_, remoteAddress_, buf.data(), std::move(stream_)))
            sp->run();
        return;
    }
    if (ec != boost::asio::error::operation_aborted)
    {
        JLOG(j_.trace()) << "Error detecting ssl: " << ec.message() << " from " << remoteAddress_;
    }
}

//------------------------------------------------------------------------------

template <class Handler>
void
Door<Handler>::reOpen()
{
    error_code ec;

    if (acceptor_.is_open())
    {
        acceptor_.close(ec);
        if (ec)
        {
            std::stringstream ss;
            ss << "Can't close acceptor: " << port_.name << ", " << ec.message();
            JLOG(j_.error()) << ss.str();
            Throw<std::runtime_error>(ss.str());
        }
    }

    endpoint_type const localAddress = endpoint_type(port_.ip, port_.port);

    acceptor_.open(localAddress.protocol(), ec);
    if (ec)
    {
        JLOG(j_.error()) << "Open port '" << port_.name << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec)
    {
        JLOG(j_.error()) << "Option for port '" << port_.name << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.bind(localAddress, ec);
    if (ec)
    {
        JLOG(j_.error()) << "Bind port '" << port_.name << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        JLOG(j_.error()) << "Listen on port '" << port_.name << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    JLOG(j_.info()) << "Opened " << port_;
}

template <class Handler>
Door<Handler>::Door(
    Handler& handler,
    boost::asio::io_context& ioContext,
    Port const& port,
    beast::Journal j)
    : j_(j)
    , port_(port)
    , handler_(handler)
    , ioc_(ioContext)
    , acceptor_(ioContext)
    , strand_(boost::asio::make_strand(ioContext))
    , backoffTimer_(ioContext)
{
    reOpen();
}

template <class Handler>
void
Door<Handler>::run()
{
    util::spawn(
        strand_,
        std::bind(&Door<Handler>::doAccept, this->shared_from_this(), std::placeholders::_1));
}

template <class Handler>
void
Door<Handler>::close()
{
    if (!strand_.running_in_this_thread())
    {
        return boost::asio::post(
            strand_, std::bind(&Door<Handler>::close, this->shared_from_this()));
    }
    backoffTimer_.cancel();
    error_code ec;
    acceptor_.close(ec);
}

//------------------------------------------------------------------------------

template <class Handler>
template <class ConstBufferSequence>
void
Door<Handler>::create(
    bool ssl,
    ConstBufferSequence const& buffers,
    stream_type&& stream,
    endpoint_type remoteAddress)
{
    if (ssl)
    {
        if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                port_, handler_, ioc_, j_, remoteAddress, buffers, std::move(stream)))
            sp->run();
        return;
    }
    if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
            port_, handler_, ioc_, j_, remoteAddress, buffers, std::move(stream)))
        sp->run();
}

template <class Handler>
void
Door<Handler>::doAccept(boost::asio::yield_context doYield)
{
    while (acceptor_.is_open())
    {
        if (shouldThrottleForFds())
        {
            backoffTimer_.expires_after(acceptDelay_);
            boost::system::error_code tec;
            backoffTimer_.async_wait(doYield[tec]);
            acceptDelay_ = std::min(acceptDelay_ * 2, kMaxAcceptDelay);
            JLOG(j_.warn()) << "Throttling do_accept for " << acceptDelay_.count() << "ms.";
            continue;
        }

        error_code ec;
        endpoint_type remoteAddress;
        stream_type stream(ioc_);
        socket_type& socket = stream.socket();
        acceptor_.async_accept(socket, remoteAddress, doYield[ec]);
        if (ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                break;

            if (ec == boost::asio::error::no_descriptors ||
                ec == boost::asio::error::no_buffer_space)
            {
                JLOG(j_.warn()) << "accept: Too many open files. Pausing for "
                                << acceptDelay_.count() << "ms.";

                backoffTimer_.expires_after(acceptDelay_);
                boost::system::error_code tec;
                backoffTimer_.async_wait(doYield[tec]);

                acceptDelay_ = std::min(acceptDelay_ * 2, kMaxAcceptDelay);
            }
            else
            {
                JLOG(j_.error()) << "accept error: " << ec.message();
            }
            continue;
        }

        acceptDelay_ = kInitialAcceptDelay;

        if (ssl_ && plain_)
        {
            if (auto sp = ios().template emplace<Detector>(
                    port_, handler_, ioc_, std::move(stream), remoteAddress, j_))
                sp->run();
        }
        else if (ssl_ || plain_)
        {
            create(ssl_, boost::asio::null_buffers{}, std::move(stream), remoteAddress);
        }
    }
}

template <class Handler>
std::optional<typename Door<Handler>::FDStats>
Door<Handler>::queryFdStats() const
{
#if BOOST_OS_WINDOWS
    return std::nullopt;
#else
    FDStats s;
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY)
        return std::nullopt;
    s.limit = static_cast<std::uint64_t>(rl.rlim_cur);
#if BOOST_OS_LINUX
    static constexpr char const* kFdDir = "/proc/self/fd";
#else
    static constexpr char const* kFdDir = "/dev/fd";
#endif
    if (DIR* d = ::opendir(kFdDir))
    {
        std::uint64_t cnt = 0;
        while (::readdir(d) != nullptr)
            ++cnt;
        ::closedir(d);
        // readdir counts '.', '..', and the DIR* itself shows in the list
        s.used = (cnt >= 3) ? (cnt - 3) : 0;
        return s;
    }
    return std::nullopt;
#endif
}

template <class Handler>
bool
Door<Handler>::shouldThrottleForFds()
{
#if BOOST_OS_WINDOWS
    return false;
#else
    auto const stats = queryFdStats();
    if (!stats || stats->limit == 0)
        return false;

    auto const& s = *stats;
    auto const free = (s.limit > s.used) ? (s.limit - s.used) : 0ull;
    double const freeRatio = static_cast<double>(free) / static_cast<double>(s.limit);
    return freeRatio < kFreeFdThreshold;
#endif
}

}  // namespace xrpl
