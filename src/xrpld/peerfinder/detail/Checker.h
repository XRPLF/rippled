#pragma once

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive/list.hpp>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace xrpl::PeerFinder {

/** Tests remote listening sockets to make sure they are connectable. */
template <class Protocol = boost::asio::ip::tcp>
class Checker
{
private:
    using error_code = boost::system::error_code;

    struct BasicAsyncOp : boost::intrusive::list_base_hook<
                              boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        virtual ~BasicAsyncOp() = default;

        virtual void
        stop() = 0;

        virtual void
        operator()(error_code const& ec) = 0;
    };

    template <class Handler>
    struct AsyncOp : BasicAsyncOp
    {
        using socket_type = Protocol::socket;
        using endpoint_type = Protocol::endpoint;

        Checker& checker;
        socket_type socket;
        Handler handler;

        AsyncOp(Checker& owner, boost::asio::io_context& ioContext, Handler&& handler);

        ~AsyncOp() override
        {
            checker.remove(*this);
        }

        void
        stop() override;

        void
        operator()(error_code const& ec) override;  // NOLINT(readability-identifier-naming)
    };

    //--------------------------------------------------------------------------

    using list_type =
        boost::intrusive::make_list<BasicAsyncOp, boost::intrusive::constant_time_size<true>>::type;

    std::mutex mutex_;
    std::condition_variable cond_;
    boost::asio::io_context& ioContext_;
    list_type list_;
    bool stop_ = false;

public:
    explicit Checker(boost::asio::io_context& ioContext);

    /** Destroy the service.
        Any pending I/O operations will be canceled. This call blocks until
        all pending operations complete (either with success or with
        operation_aborted) and the associated thread and io_context have
        no more work remaining.
    */
    ~Checker();

    /** Stop the service.
        Pending I/O operations will be canceled.
        This issues cancel orders for all pending I/O operations and then
        returns immediately. Handlers will receive operation_aborted errors,
        or if they were already queued they will complete normally.
    */
    void
    stop();

    /** Block until all pending I/O completes. */
    void
    wait();

    /** Performs an async connection test on the specified endpoint.
        The port must be non-zero. Note that the execution guarantees
        offered by asio handlers are NOT enforced.
    */
    template <class Handler>
    void
    asyncConnect(beast::IP::Endpoint const& endpoint, Handler&& handler);

private:
    void
    remove(BasicAsyncOp& op);
};

//------------------------------------------------------------------------------

template <class Protocol>
template <class Handler>
Checker<Protocol>::AsyncOp<Handler>::AsyncOp(
    Checker& owner,
    boost::asio::io_context& ioContext,
    Handler&&
        handler)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved) -- forwarded in init
    : checker(owner), socket(ioContext), handler(std::forward<Handler>(handler))
{
}

template <class Protocol>
template <class Handler>
void
Checker<Protocol>::AsyncOp<Handler>::stop()
{
    error_code ec;
    socket.cancel(ec);
}

template <class Protocol>
template <class Handler>
void
Checker<Protocol>::AsyncOp<Handler>::operator()(error_code const& ec)
{
    handler(ec);
}

//------------------------------------------------------------------------------

template <class Protocol>
Checker<Protocol>::Checker(boost::asio::io_context& ioContext) : ioContext_(ioContext)
{
}

template <class Protocol>
Checker<Protocol>::~Checker()
{
    wait();
}

template <class Protocol>
void
Checker<Protocol>::stop()
{
    std::scoped_lock const lock(mutex_);
    if (!stop_)
    {
        stop_ = true;
        for (auto& c : list_)
            c.stop();
    }
}

template <class Protocol>
void
Checker<Protocol>::wait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!list_.empty())
        cond_.wait(lock);
}

template <class Protocol>
template <class Handler>
void
Checker<Protocol>::asyncConnect(beast::IP::Endpoint const& endpoint, Handler&& handler)
{
    auto const op =
        std::make_shared<AsyncOp<Handler>>(*this, ioContext_, std::forward<Handler>(handler));
    {
        std::scoped_lock const lock(mutex_);
        list_.push_back(*op);
    }
    op->socket.async_connect(
        beast::IPAddressConversion::toAsioEndpoint(endpoint),
        std::bind(&BasicAsyncOp::operator(), op, std::placeholders::_1));
}

template <class Protocol>
void
Checker<Protocol>::remove(BasicAsyncOp& op)
{
    std::scoped_lock const lock(mutex_);
    list_.erase(list_.iterator_to(op));
    if (list_.size() == 0)
        cond_.notify_all();
}

}  // namespace xrpl::PeerFinder
