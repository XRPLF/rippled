#ifndef XRPL_SERVER_DOOR_H_INCLUDED
#define XRPL_SERVER_DOOR_H_INCLUDED

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

namespace ripple {

/** A listening socket. */
template <class Handler>
class Door : public io_list::work,
             public std::enable_shared_from_this<Door<Handler>>
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
    class Detector : public io_list::work,
                     public std::enable_shared_from_this<Detector>
    {
    private:
        Port const& port_;
        Handler& handler_;
        boost::asio::io_context& ioc_;
        stream_type stream_;
        socket_type& socket_;
        endpoint_type remote_address_;
        boost::asio::strand<boost::asio::io_context::executor_type> strand_;
        beast::Journal const j_;

    public:
        Detector(
            Port const& port,
            Handler& handler,
            boost::asio::io_context& ioc,
            stream_type&& stream,
            endpoint_type remote_address,
            beast::Journal j);
        void
        run();
        void
        close() override;

    private:
        void
        do_detect(yield_context yield);
    };

    beast::Journal const j_;
    Port const& port_;
    Handler& handler_;
    boost::asio::io_context& ioc_;
    acceptor_type acceptor_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    bool ssl_;
    bool plain_;
    static constexpr std::chrono::milliseconds INITIAL_ACCEPT_DELAY{50};
    static constexpr std::chrono::milliseconds MAX_ACCEPT_DELAY{2000};
    std::chrono::milliseconds accept_delay_{INITIAL_ACCEPT_DELAY};
    boost::asio::steady_timer backoff_timer_;
    static constexpr double FREE_FD_THRESHOLD = 0.70;

    struct FDStats
    {
        std::uint64_t used{0};
        std::uint64_t limit{0};
    };

    void
    reOpen();

    std::optional<FDStats>
    query_fd_stats() const;

    bool
    should_throttle_for_fds();

public:
    Door(
        Handler& handler,
        boost::asio::io_context& io_context,
        Port const& port,
        beast::Journal j);

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

    endpoint_type
    get_endpoint() const
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
        endpoint_type remote_address);

    void
    do_accept(yield_context yield);
};

template <class Handler>
Door<Handler>::Detector::Detector(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    stream_type&& stream,
    endpoint_type remote_address,
    beast::Journal j)
    : port_(port)
    , handler_(handler)
    , ioc_(ioc)
    , stream_(std::move(stream))
    , socket_(stream_.socket())
    , remote_address_(remote_address)
    , strand_(boost::asio::make_strand(ioc_))
    , j_(j)
{
}

template <class Handler>
void
Door<Handler>::Detector::run()
{
    util::spawn(
        strand_,
        std::bind(
            &Detector::do_detect,
            this->shared_from_this(),
            std::placeholders::_1));
}

template <class Handler>
void
Door<Handler>::Detector::close()
{
    stream_.close();
}

template <class Handler>
void
Door<Handler>::Detector::do_detect(boost::asio::yield_context do_yield)
{
    boost::beast::multi_buffer buf(16);
    stream_.expires_after(std::chrono::seconds(15));
    boost::system::error_code ec;
    bool const ssl = async_detect_ssl(stream_, buf, do_yield[ec]);
    stream_.expires_never();
    if (!ec)
    {
        if (ssl)
        {
            if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                    port_,
                    handler_,
                    ioc_,
                    j_,
                    remote_address_,
                    buf.data(),
                    std::move(stream_)))
                sp->run();
            return;
        }
        if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
                port_,
                handler_,
                ioc_,
                j_,
                remote_address_,
                buf.data(),
                std::move(stream_)))
            sp->run();
        return;
    }
    if (ec != boost::asio::error::operation_aborted)
    {
        JLOG(j_.trace()) << "Error detecting ssl: " << ec.message() << " from "
                         << remote_address_;
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
            ss << "Can't close acceptor: " << port_.name << ", "
               << ec.message();
            JLOG(j_.error()) << ss.str();
            Throw<std::runtime_error>(ss.str());
        }
    }

    endpoint_type const local_address = endpoint_type(port_.ip, port_.port);

    acceptor_.open(local_address.protocol(), ec);
    if (ec)
    {
        JLOG(j_.error()) << "Open port '" << port_.name
                         << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.set_option(
        boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec)
    {
        JLOG(j_.error()) << "Option for port '" << port_.name
                         << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.bind(local_address, ec);
    if (ec)
    {
        JLOG(j_.error()) << "Bind port '" << port_.name
                         << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        JLOG(j_.error()) << "Listen on port '" << port_.name
                         << "' failed:" << ec.message();
        Throw<std::exception>();
    }

    JLOG(j_.info()) << "Opened " << port_;
}

template <class Handler>
Door<Handler>::Door(
    Handler& handler,
    boost::asio::io_context& io_context,
    Port const& port,
    beast::Journal j)
    : j_(j)
    , port_(port)
    , handler_(handler)
    , ioc_(io_context)
    , acceptor_(io_context)
    , strand_(boost::asio::make_strand(io_context))
    , ssl_(
          port_.protocol.count("https") > 0 ||
          port_.protocol.count("wss") > 0 || port_.protocol.count("wss2") > 0 ||
          port_.protocol.count("peer") > 0)
    , plain_(
          port_.protocol.count("http") > 0 || port_.protocol.count("ws") > 0 ||
          port_.protocol.count("ws2"))
    , backoff_timer_(io_context)
{
    reOpen();
}

template <class Handler>
void
Door<Handler>::run()
{
    util::spawn(
        strand_,
        std::bind(
            &Door<Handler>::do_accept,
            this->shared_from_this(),
            std::placeholders::_1));
}

template <class Handler>
void
Door<Handler>::close()
{
    if (!strand_.running_in_this_thread())
        return boost::asio::post(
            strand_,
            std::bind(&Door<Handler>::close, this->shared_from_this()));
    backoff_timer_.cancel();
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
    endpoint_type remote_address)
{
    if (ssl)
    {
        if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                port_,
                handler_,
                ioc_,
                j_,
                remote_address,
                buffers,
                std::move(stream)))
            sp->run();
        return;
    }
    if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
            port_,
            handler_,
            ioc_,
            j_,
            remote_address,
            buffers,
            std::move(stream)))
        sp->run();
}

template <class Handler>
void
Door<Handler>::do_accept(boost::asio::yield_context do_yield)
{
    while (acceptor_.is_open())
    {
        if (should_throttle_for_fds())
        {
            backoff_timer_.expires_after(accept_delay_);
            boost::system::error_code tec;
            backoff_timer_.async_wait(do_yield[tec]);
            accept_delay_ = std::min(accept_delay_ * 2, MAX_ACCEPT_DELAY);
            JLOG(j_.warn()) << "Throttling do_accept for "
                            << accept_delay_.count() << "ms.";
            continue;
        }

        error_code ec;
        endpoint_type remote_address;
        stream_type stream(ioc_);
        socket_type& socket = stream.socket();
        acceptor_.async_accept(socket, remote_address, do_yield[ec]);
        if (ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                break;

            if (ec == boost::asio::error::no_descriptors ||
                ec == boost::asio::error::no_buffer_space)
            {
                JLOG(j_.warn()) << "accept: Too many open files. Pausing for "
                                << accept_delay_.count() << "ms.";

                backoff_timer_.expires_after(accept_delay_);
                boost::system::error_code tec;
                backoff_timer_.async_wait(do_yield[tec]);

                accept_delay_ = std::min(accept_delay_ * 2, MAX_ACCEPT_DELAY);
            }
            else
            {
                JLOG(j_.error()) << "accept error: " << ec.message();
            }
            continue;
        }

        accept_delay_ = INITIAL_ACCEPT_DELAY;

        if (ssl_ && plain_)
        {
            if (auto sp = ios().template emplace<Detector>(
                    port_,
                    handler_,
                    ioc_,
                    std::move(stream),
                    remote_address,
                    j_))
                sp->run();
        }
        else if (ssl_ || plain_)
        {
            create(
                ssl_,
                boost::asio::null_buffers{},
                std::move(stream),
                remote_address);
        }
    }
}

template <class Handler>
std::optional<typename Door<Handler>::FDStats>
Door<Handler>::query_fd_stats() const
{
#if BOOST_OS_WINDOWS
    return std::nullopt;
#else
    FDStats s;
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY)
        return std::nullopt;
    s.limit = static_cast<std::uint64_t>(rl.rlim_cur);
#if BOOST_OS_LINUX
    constexpr char const* kFdDir = "/proc/self/fd";
#else
    constexpr char const* kFdDir = "/dev/fd";
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
Door<Handler>::should_throttle_for_fds()
{
#if BOOST_OS_WINDOWS
    return false;
#else
    auto const stats = query_fd_stats();
    if (!stats || stats->limit == 0)
        return false;

    auto const& s = *stats;
    auto const free = (s.limit > s.used) ? (s.limit - s.used) : 0ull;
    double const free_ratio =
        static_cast<double>(free) / static_cast<double>(s.limit);
    if (free_ratio < FREE_FD_THRESHOLD)
    {
        return true;
    }
    return false;
#endif
}

}  // namespace ripple

#endif
