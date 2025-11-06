#ifndef XRPL_SERVER_BASEPEER_H_INCLUDED
#define XRPL_SERVER_BASEPEER_H_INCLUDED

#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/detail/LowestLayer.h>
#include <xrpl/server/detail/io_list.h>

#include <boost/asio.hpp>

#include <atomic>
#include <functional>
#include <string>

namespace ripple {

// Common part of all peers
template <class Handler, class Impl>
class BasePeer : public io_list::work
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer<clock_type>;

    Port const& port_;
    Handler& handler_;
    endpoint_type remote_address_;
    beast::WrappedSink sink_;
    beast::Journal const j_;

    boost::asio::executor_work_guard<boost::asio::executor> work_;
    boost::asio::strand<boost::asio::executor> strand_;

public:
    BasePeer(
        Port const& port,
        Handler& handler,
        boost::asio::executor const& executor,
        endpoint_type remote_address,
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
    endpoint_type remote_address,
    beast::Journal journal)
    : port_(port)
    , handler_(handler)
    , remote_address_(remote_address)
    , sink_(
          journal.sink(),
          [] {
              static std::atomic<unsigned> id{0};
              return "##" + std::to_string(++id) + " ";
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
        return post(
            strand_, std::bind(&BasePeer::close, impl().shared_from_this()));
    error_code ec;
    ripple::get_lowest_layer(impl().ws_).socket().close(ec);
}

}  // namespace ripple

#endif
