#pragma once

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/server/WSSession.h>
#include <xrpl/server/detail/BasePeer.h>
#include <xrpl/server/detail/LowestLayer.h>

#include <boost/asio/error.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/logic/tribool.hpp>

#include <algorithm>
#include <functional>
#include <list>

namespace xrpl {

/** Represents an active WebSocket connection. */
template <class Handler, class Impl>
class BaseWSPeer : public BasePeer<Handler, Impl>, public WSSession
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer<clock_type>;
    using BasePeer<Handler, Impl>::strand_;

private:
    friend class BasePeer<Handler, Impl>;

    http_request_type request_;
    boost::beast::multi_buffer rb_;
    boost::beast::multi_buffer wb_;
    std::list<std::shared_ptr<WSMsg>> wq_;
    /// The socket has been closed, or will close after the next write
    /// finishes. Do not do any more writes, and don't try to close
    /// again.
    bool doClose_ = false;
    boost::beast::websocket::close_reason cr_;
    waitable_timer timer_;
    bool closeOnTimer_ = false;
    bool pingActive_ = false;
    boost::beast::websocket::ping_data payload_;
    error_code ec_;
    std::function<void(boost::beast::websocket::frame_type, boost::beast::string_view)>
        controlCallback_;

public:
    template <class Body, class Headers>
    BaseWSPeer(
        Port const& port,
        Handler& handler,
        boost::asio::executor const& executor,
        waitable_timer timer,
        endpoint_type remoteAddress,
        boost::beast::http::request<Body, Headers>&& request,
        beast::Journal journal);

    void
    run() override;

    //
    // WSSession
    //

    [[nodiscard]] Port const&
    port() const override
    {
        return this->port_;
    }

    [[nodiscard]] http_request_type const&
    request() const override
    {
        return this->request_;
    }

    [[nodiscard]] boost::asio::ip::tcp::endpoint const&
    remoteEndpoint() const override
    {
        return this->remoteAddress_;
    }

    void
    send(std::shared_ptr<WSMsg> w) override;

    void
    close() override;

    void
    close(boost::beast::websocket::close_reason const& reason) override;

    void
    complete() override;

protected:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    onWsHandshake(error_code const& ec);

    void
    doWrite();

    void
    onWrite(error_code const& ec);

    void
    onWriteFin(error_code const& ec);

    void
    doRead();

    void
    onRead(error_code const& ec);

    void
    onClose(error_code const& ec);

    void
    startTimer();

    void
    cancelTimer();

    void
    onPing(error_code const& ec);

    void
    onPingPong(boost::beast::websocket::frame_type kind, boost::beast::string_view payload);

    void
    onTimer(error_code ec);

    template <class String>
    void
    fail(error_code ec, String const& what);
};

//------------------------------------------------------------------------------

template <class Handler, class Impl>
template <class Body, class Headers>
BaseWSPeer<Handler, Impl>::BaseWSPeer(
    Port const& port,
    Handler& handler,
    boost::asio::executor const& executor,
    waitable_timer timer,
    endpoint_type remoteAddress,
    boost::beast::http::request<Body, Headers>&& request,
    beast::Journal journal)
    : BasePeer<Handler, Impl>(port, handler, executor, remoteAddress, journal)
    , request_(std::move(request))
    , timer_(std::move(timer))
    , payload_("12345678")  // ensures size is 8 bytes
{
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::run()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BaseWSPeer::run, impl().shared_from_this()));
    impl().ws_.set_option(port().pmdOptions);
    // Must manage the control callback memory outside of the `control_callback`
    // function
    controlCallback_ =
        std::bind(&BaseWSPeer::onPingPong, this, std::placeholders::_1, std::placeholders::_2);
    impl().ws_.control_callback(controlCallback_);
    startTimer();
    closeOnTimer_ = true;
    impl().ws_.set_option(boost::beast::websocket::stream_base::decorator([](auto& res) {
        res.set(boost::beast::http::field::server, BuildInfo::getFullVersionString());
    }));
    impl().ws_.async_accept(
        request_,
        bind_executor(
            strand_,
            std::bind(
                &BaseWSPeer::onWsHandshake, impl().shared_from_this(), std::placeholders::_1)));
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::send(std::shared_ptr<WSMsg> w)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BaseWSPeer::send, impl().shared_from_this(), std::move(w)));
    if (doClose_)
        return;
    if (wq_.size() > port().wsQueueLimit)
    {
        cr_.code = safeCast<decltype(cr_.code)>(boost::beast::websocket::close_code::policy_error);
        cr_.reason = "Policy error: client is too slow.";
        JLOG(this->j_.info()) << cr_.reason;
        wq_.erase(std::next(wq_.begin()), wq_.end());
        close(cr_);
        return;
    }
    wq_.emplace_back(std::move(w));
    if (wq_.size() == 1)
        onWrite({});
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::close()
{
    close(boost::beast::websocket::close_reason{});
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::close(boost::beast::websocket::close_reason const& reason)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, [self = impl().shared_from_this(), reason] { self->close(reason); });
    if (doClose_)
        return;
    doClose_ = true;
    if (wq_.empty())
    {
        impl().ws_.async_close(
            reason,
            bind_executor(
                strand_, [self = impl().shared_from_this()](boost::beast::error_code const& ec) {
                    self->onClose(ec);
                }));
    }
    else
    {
        cr_ = reason;
    }
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::complete()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BaseWSPeer::complete, impl().shared_from_this()));
    doRead();
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onWsHandshake(error_code const& ec)
{
    if (ec)
        return fail(ec, "on_ws_handshake");
    closeOnTimer_ = false;
    doRead();
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::doWrite()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BaseWSPeer::doWrite, impl().shared_from_this()));
    onWrite({});
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onWrite(error_code const& ec)
{
    if (ec)
        return fail(ec, "write");
    auto& w = *wq_.front();
    auto const result =
        w.prepare(65536, std::bind(&BaseWSPeer::doWrite, impl().shared_from_this()));
    if (boost::indeterminate(result.first))
        return;
    startTimer();
    if (!result.first)
    {
        impl().ws_.async_write_some(
            static_cast<bool>(result.first),
            result.second,
            bind_executor(
                strand_,
                std::bind(&BaseWSPeer::onWrite, impl().shared_from_this(), std::placeholders::_1)));
    }
    else
    {
        impl().ws_.async_write_some(
            static_cast<bool>(result.first),
            result.second,
            bind_executor(
                strand_,
                std::bind(
                    &BaseWSPeer::onWriteFin, impl().shared_from_this(), std::placeholders::_1)));
    }
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onWriteFin(error_code const& ec)
{
    if (ec)
        return fail(ec, "write_fin");
    wq_.pop_front();
    if (doClose_)
    {
        impl().ws_.async_close(
            cr_,
            bind_executor(
                strand_,
                std::bind(&BaseWSPeer::onClose, impl().shared_from_this(), std::placeholders::_1)));
    }
    else if (!wq_.empty())
    {
        onWrite({});
    }
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::doRead()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BaseWSPeer::doRead, impl().shared_from_this()));
    impl().ws_.async_read(
        rb_,
        bind_executor(
            strand_,
            std::bind(&BaseWSPeer::onRead, impl().shared_from_this(), std::placeholders::_1)));
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onRead(error_code const& ec)
{
    if (ec == boost::beast::websocket::error::closed)
        return onClose({});
    if (ec)
        return fail(ec, "read");
    auto const& data = rb_.data();
    std::vector<boost::asio::const_buffer> b;
    b.reserve(std::distance(data.begin(), data.end()));
    std::ranges::copy(data, std::back_inserter(b));
    this->handler_.onWSMessage(impl().shared_from_this(), b);
    rb_.consume(rb_.size());
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onClose(error_code const& ec)
{
    cancelTimer();
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::startTimer()
{
    // Max seconds without completing a message
    static constexpr std::chrono::seconds kTimeout{30};
    static constexpr std::chrono::seconds kTimeoutLocal{3};

    try
    {
        timer_.expires_after(remoteEndpoint().address().is_loopback() ? kTimeoutLocal : kTimeout);
    }
    catch (boost::system::system_error const& e)
    {
        return fail(e.code(), "start_timer");
    }

    timer_.async_wait(bind_executor(
        strand_,
        std::bind(
            &BaseWSPeer<Handler, Impl>::onTimer,
            impl().shared_from_this(),
            std::placeholders::_1)));
}

// Convenience for discarding the error code
template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::cancelTimer()
{
    try
    {
        timer_.cancel();
    }
    catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
    {
        // ignored
    }
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onPing(error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    pingActive_ = false;
    if (!ec)
        return;
    fail(ec, "on_ping");
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onPingPong(
    boost::beast::websocket::frame_type kind,
    boost::beast::string_view payload)
{
    if (kind == boost::beast::websocket::frame_type::pong)
    {
        boost::beast::string_view const p(payload_.begin());
        if (payload == p)
        {
            closeOnTimer_ = false;
            JLOG(this->j_.trace()) << "got matching pong";
        }
        else
        {
            JLOG(this->j_.trace()) << "got pong";
        }
    }
}

template <class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::onTimer(error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (!ec)
    {
        if (!closeOnTimer_ || !pingActive_)
        {
            startTimer();
            closeOnTimer_ = true;
            pingActive_ = true;
            // cryptographic is probably overkill..
            beast::rngfill(payload_.begin(), payload_.size(), cryptoPrng());
            impl().ws_.async_ping(
                payload_,
                bind_executor(
                    strand_,
                    std::bind(
                        &BaseWSPeer::onPing, impl().shared_from_this(), std::placeholders::_1)));
            JLOG(this->j_.trace()) << "sent ping";
            return;
        }
        ec = boost::system::errc::make_error_code(boost::system::errc::timed_out);
    }
    fail(ec, "timer");
}

template <class Handler, class Impl>
template <class String>
void
BaseWSPeer<Handler, Impl>::fail(error_code ec, String const& what)
{
    XRPL_ASSERT(strand_.running_in_this_thread(), "xrpl::BaseWSPeer::fail : strand in this thread");

    cancelTimer();
    if (!ec_ && ec != boost::asio::error::operation_aborted)
    {
        ec_ = ec;
        JLOG(this->j_.trace()) << what << ": " << ec.message();
        xrpl::getLowestLayer(impl().ws_).socket().close(ec);
    }
}

}  // namespace xrpl
