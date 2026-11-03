#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/detail/LowestLayer.h>
#include <xrpl/server/detail/io_list.h>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <utility>

namespace xrpl {

// Common part of all peers
template <class Handler, class Impl>
class BasePeer : public IOList::Work
{
protected:
    using ClockType = std::chrono::system_clock;
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;
    using WaitableTimer = boost::asio::basic_waitable_timer<ClockType>;

    Port const& port_;
    Handler& handler_;
    EndpointType remoteAddress_;
    beast::WrappedSink sink_;
    beast::Journal const j_;

    boost::asio::executor_work_guard<boost::asio::executor> work_;
    boost::asio::strand<boost::asio::executor> strand_;

public:
    // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
    BasePeer(
        Port const& port,
        Handler& handler,
        boost::asio::executor const& executor,
        EndpointType remoteAddress,
        beast::Journal journal);

    void
    close() override;

private:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }
};

//------------------------------------------------------------------------------

template <class Handler, class Impl>
BasePeer<Handler, Impl>::BasePeer(
    Port const& port,
    Handler& handler,
    boost::asio::executor const& executor,
    EndpointType remoteAddress,
    beast::Journal journal)
    : port_(port)
    , handler_(handler)
    , remoteAddress_(std::move(remoteAddress))
    , sink_(
          journal.sink(),
          [] {
              static std::atomic<unsigned> kID{0};
              return "##" + std::to_string(++kID) + " ";
          }())
    , j_(sink_)
    , work_(boost::asio::make_work_guard(executor))
    , strand_(boost::asio::make_strand(executor))
{
}

template <class Handler, class Impl>
void
BasePeer<Handler, Impl>::close()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, [self = impl().shared_from_this()] { self->close(); });
    ErrorCode ec;
    xrpl::getLowestLayer(impl().ws_).socket().close(ec);
}

}  // namespace xrpl
