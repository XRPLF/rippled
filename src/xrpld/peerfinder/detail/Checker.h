/** @file
 *  Async TCP reachability prober for PeerFinder.
 *
 *  When an inbound peer reports the address it claims to accept connections on,
 *  `Checker` attempts an outbound TCP connection to verify that address is
 *  actually reachable. The result gates whether the peer's address is admitted
 *  to the live cache and gossiped to the rest of the network.
 */

#pragma once

#include <xrpl/beast/net/IPAddressConversion.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive/list.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>

namespace xrpl::PeerFinder {

/** Performs async TCP reachability probes for PeerFinder.
 *
 *  When `Logic` receives an inbound peer's advertised listening endpoint via
 *  `on_endpoints`, it calls `asyncConnect` to verify the port is actually open.
 *  The completion handler (`Logic::checkComplete`) sets `SlotImp::canAccept`,
 *  which gates admission to the live cache and gossip propagation.
 *
 *  In-flight operations are tracked in an intrusive list. `stop()` cancels all
 *  pending operations (handlers receive `operation_aborted`). `wait()` blocks
 *  until the list drains. The destructor calls only `wait()` — callers must
 *  invoke `stop()` first if cancellation is desired before destruction.
 *
 *  @tparam Protocol  Boost.Asio protocol type, defaulting to
 *      `boost::asio::ip::tcp`. Substitute a mock protocol in unit tests.
 */
template <class Protocol = boost::asio::ip::tcp>
class Checker
{
private:
    using error_code = boost::system::error_code;

    /** Polymorphic base for a single in-flight async connection probe.
     *
     *  Embeds a `boost::intrusive::list_base_hook` directly to avoid the extra
     *  heap allocation that `std::list<std::unique_ptr<...>>` would require.
     *  Concrete instances are `AsyncOp<Handler>`.
     */
    struct BasicAsyncOp : boost::intrusive::list_base_hook<
                              boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        virtual ~BasicAsyncOp() = default;

        /** Cancel the underlying socket operation without waiting for it to complete. */
        virtual void
        stop() = 0;

        /** Dispatch the completion handler with the given error code. */
        virtual void
        operator()(error_code const& ec) = 0;
    };

    /** Concrete async probe that binds a socket and a caller-supplied handler.
     *
     *  Created as a `shared_ptr` in `asyncConnect`; the lambda passed to
     *  `async_connect` captures that same `shared_ptr`, keeping the op alive
     *  for the entire async lifetime. When the lambda is destroyed the
     *  reference count drops to zero and the destructor calls
     *  `checker.remove(*this)`, deregistering the op from the tracked list
     *  and signalling `cond_` if the list is now empty.
     */
    template <class Handler>
    struct AsyncOp : BasicAsyncOp
    {
        using socket_type = typename Protocol::socket;
        using endpoint_type = typename Protocol::endpoint;

        /** Back-reference to the owning `Checker`, used in the destructor. */
        Checker& checker;
        /** Socket used for the outbound connection attempt. */
        socket_type socket;
        /** Caller-supplied completion handler invoked with the probe result. */
        Handler handler;

        AsyncOp(Checker& owner, boost::asio::io_context& ioContext, Handler&& handler);

        /** Remove this op from the owning `Checker`'s tracking list. */
        ~AsyncOp() override
        {
            checker.remove(*this);
        }

        /** Cancel the socket, causing the pending handler to receive `operation_aborted`. */
        void
        stop() override;

        /** Invoke `handler(ec)` with the async_connect result. */
        void
        operator()(error_code const& ec) override;  // NOLINT(readability-identifier-naming)
    };

    //--------------------------------------------------------------------------

    using list_type = typename boost::intrusive::
        make_list<BasicAsyncOp, boost::intrusive::constant_time_size<true>>::type;

    std::mutex mutex_;
    std::condition_variable cond_;
    boost::asio::io_context& ioContext_;
    list_type list_;
    bool stop_ = false;

public:
    /** Construct a `Checker` that schedules probes on the given `io_context`.
     *
     *  @param ioContext  The Boost.Asio `io_context` that drives async I/O.
     */
    explicit Checker(boost::asio::io_context& ioContext);

    /** Drain all pending probes and destroy the checker.
     *
     *  Blocks until every in-flight `async_connect` (whether it completed
     *  normally or with `operation_aborted`) has finished and its handler has
     *  run. Does **not** cancel pending operations — call `stop()` first if
     *  cancellation before destruction is required.
     */
    ~Checker();

    /** Cancel all pending probes and return immediately.
     *
     *  Sets `stop_ = true` under lock, then calls `socket.cancel()` on every
     *  live `AsyncOp`. The cancel is asynchronous: handlers will receive
     *  `boost::asio::error::operation_aborted` rather than a success code.
     *  Handlers that were already queued for delivery before the cancel may
     *  still complete normally.
     *
     *  @note This method is idempotent; calling it more than once is safe.
     */
    void
    stop();

    /** Block until all pending probes complete.
     *
     *  Waits on `cond_` until the intrusive list of in-flight ops is empty.
     *  Each `AsyncOp` destructor signals the condition variable, so this
     *  unblocks as soon as the last handler returns.
     */
    void
    wait();

    /** Probe `endpoint` for TCP reachability and invoke `handler` on completion.
     *
     *  Creates an `AsyncOp<Handler>`, registers it in the tracked list, and
     *  issues `async_connect` on the converted Boost.Asio endpoint. The handler
     *  is invoked with a `boost::system::error_code`: zero on success (port is
     *  reachable), non-zero on failure (including `operation_aborted` if
     *  `stop()` is called before the connect completes).
     *
     *  @param endpoint  The peer-reported listening address to probe. The port
     *      must be non-zero.
     *  @param handler   Completion handler with signature
     *      `void(boost::system::error_code)`. Invoked on whatever thread
     *      services the `io_context` — Asio strand guarantees are NOT enforced.
     *      `Logic::checkComplete` takes `lock_` immediately on entry to guard
     *      shared slot state.
     *
     *  @note The first endpoint a peer advertises is intentionally discarded
     *      while this probe is in flight; the peer will re-advertise shortly.
     */
    template <class Handler>
    void
    asyncConnect(beast::IP::Endpoint const& endpoint, Handler&& handler);

private:
    /** Remove a completed op from the tracking list and signal waiters.
     *
     *  Called from `AsyncOp::~AsyncOp()`. Erases the op from `list_` under
     *  `mutex_` and notifies `cond_` if the list becomes empty so that
     *  `wait()` can unblock.
     *
     *  @param op  The op to remove; must be in `list_`.
     */
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
