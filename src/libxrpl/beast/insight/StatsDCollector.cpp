/** @file
 *  Concrete StatsD backend for the Beast insight metrics framework.
 *
 *  All metric state and UDP I/O live on a single private background thread,
 *  eliminating per-metric locking.  Metrics post mutations via
 *  `boost::asio::dispatch`; a 1-second timer drains the accumulated values
 *  and ships them to the configured StatsD endpoint.
 *
 *  Enable `BEAST_STATSDCOLLECTOR_TRACING_ENABLED` at compile time to log
 *  raw UDP payloads to `std::cerr` before each send.
 */

#include <xrpl/beast/insight/StatsDCollector.h>

#include <xrpl/beast/core/List.h>
#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/MeterImpl.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef BEAST_STATSDCOLLECTOR_TRACING_ENABLED
#define BEAST_STATSDCOLLECTOR_TRACING_ENABLED 0
#endif

namespace beast::insight {

namespace detail {

class StatsDCollectorImp;

//------------------------------------------------------------------------------

/** Base class for all StatsD metric implementations.
 *
 *  Inherits from `List<StatsDMetricBase>::Node` so each metric object is its
 *  own intrusive-list node, enabling O(1) insertion and removal from the
 *  collector's registry without separate heap allocation.  `doProcess()` is
 *  called once per timer tick (on the I/O thread) to flush pending values.
 */
class StatsDMetricBase : public List<StatsDMetricBase>::Node
{
public:
    /** Serialize and queue any pending metric value for the next UDP send. */
    virtual void
    doProcess() = 0;
    virtual ~StatsDMetricBase() = default;
    StatsDMetricBase() = default;
    StatsDMetricBase(StatsDMetricBase const&) = delete;
    StatsDMetricBase&
    operator=(StatsDMetricBase const&) = delete;
};

//------------------------------------------------------------------------------

/** StatsD implementation of a `Hook` metric.
 *
 *  Registers a user-supplied callback that is invoked once per timer tick on
 *  the I/O thread, allowing callers to push derived metric values (e.g. queue
 *  depths) into the collector at collection time rather than continuously.
 */
class StatsDHookImpl : public HookImpl, public StatsDMetricBase
{
public:
    /** Construct and register the hook with the collector.
     *
     *  @param handler Callback invoked each second on the I/O thread.
     *  @param impl    The owning collector; kept alive via `shared_ptr`.
     */
    StatsDHookImpl(HandlerType handler, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDHookImpl() override;

    /** Invoke the registered handler. */
    void
    doProcess() override;

    StatsDHookImpl&
    operator=(StatsDHookImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    HandlerType handler_;
};

//------------------------------------------------------------------------------

/** StatsD implementation of a `Counter` metric.
 *
 *  Accumulates signed increments between timer ticks; on each tick `flush()`
 *  emits a `|c` StatsD counter line and resets the accumulator to zero.
 *  Increments from any thread are safe: they are marshalled onto the I/O
 *  thread via `boost::asio::dispatch`, so `value_` is never accessed
 *  concurrently.
 */
class StatsDCounterImpl : public CounterImpl, public StatsDMetricBase
{
public:
    /** Construct and register the counter with the collector.
     *
     *  @param name Metric name appended to the collector prefix.
     *  @param impl The owning collector.
     */
    StatsDCounterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDCounterImpl() override;

    /** Schedule an increment on the I/O thread.
     *
     *  Thread-safe; dispatches `doIncrement` asynchronously.
     *  @param amount Signed delta to add to the counter.
     */
    void
    increment(CounterImpl::value_type amount) override;

    /** Serialize and queue the accumulated value if the counter is dirty.
     *
     *  Resets `value_` and `dirty_` after sending.  Must be called on the
     *  I/O thread.
     */
    void
    flush();

    /** Apply the increment and mark the counter dirty.  I/O-thread only. */
    void
    doIncrement(CounterImpl::value_type amount);

    /** Called by the timer tick to flush pending data. */
    void
    doProcess() override;

    StatsDCounterImpl&
    operator=(StatsDCounterImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    CounterImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

/** StatsD implementation of an `Event` metric.
 *
 *  Unlike counters, gauges, and meters, events are not coalesced by the
 *  1-second timer.  Each `notify()` call immediately dispatches a `|ms`
 *  timing line to the I/O thread to preserve per-event latency granularity.
 *  Events therefore do not participate in the `StatsDMetricBase` registry.
 */
class StatsDEventImpl : public EventImpl
{
public:
    /** Construct the event (does NOT register with the collector registry).
     *
     *  @param name Metric name appended to the collector prefix.
     *  @param impl The owning collector.
     */
    StatsDEventImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDEventImpl() override = default;

    /** Schedule immediate serialization of the timing value on the I/O thread.
     *
     *  Thread-safe; dispatches `doNotify` asynchronously.
     *  @param value Duration to report, in milliseconds.
     */
    void
    notify(EventImpl::value_type const& value) override;

    /** Serialize the event as a `|ms` StatsD line and queue it for sending.
     *
     *  Must be called on the I/O thread.
     *  @param value Duration to report, in milliseconds.
     */
    void
    doNotify(EventImpl::value_type const& value);

    /** No-op: events bypass the periodic timer. */
    void
    doProcess();

private:
    StatsDEventImpl&
    operator=(StatsDEventImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
};

//------------------------------------------------------------------------------

/** StatsD implementation of a `Gauge` metric.
 *
 *  Represents an instantaneous level (e.g. queue depth, memory usage).
 *  Two optimizations apply beyond basic dirty-flag flushing:
 *  - `doSet()` suppresses the dirty flag when the incoming value equals the
 *    last-sent value, avoiding redundant sends every second.
 *  - `doIncrement()` applies saturating arithmetic: positive deltas cap at
 *    `uint64_t` max; negative deltas floor at zero, preventing wraparound.
 */
class StatsDGaugeImpl : public GaugeImpl, public StatsDMetricBase
{
public:
    /** Construct and register the gauge with the collector.
     *
     *  @param name Metric name appended to the collector prefix.
     *  @param impl The owning collector.
     */
    StatsDGaugeImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDGaugeImpl() override;

    /** Schedule an absolute value update on the I/O thread.
     *
     *  Thread-safe; dispatches `doSet` asynchronously.
     *  @param value New absolute gauge value.
     */
    void
    set(GaugeImpl::value_type value) override;

    /** Schedule a relative increment on the I/O thread.
     *
     *  Thread-safe; dispatches `doIncrement` asynchronously.
     *  @param amount Signed delta; saturating arithmetic prevents overflow.
     */
    void
    increment(GaugeImpl::difference_type amount) override;

    /** Serialize and queue the current value if the gauge is dirty.
     *
     *  Emits a `|g` StatsD line.  Must be called on the I/O thread.
     */
    void
    flush();

    /** Set the absolute value, marking dirty only when the value changes.
     *
     *  Compares against `last_value_` to suppress unchanged sends.
     *  Must be called on the I/O thread.
     *  @param value New absolute gauge value.
     */
    void
    doSet(GaugeImpl::value_type value);

    /** Apply a saturating delta and delegate to `doSet`.  I/O-thread only.
     *
     *  @param amount Signed delta; capped to avoid unsigned wraparound.
     */
    void
    doIncrement(GaugeImpl::difference_type amount);

    /** Called by the timer tick to flush pending data. */
    void
    doProcess() override;

    StatsDGaugeImpl&
    operator=(StatsDGaugeImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    GaugeImpl::value_type last_value_{0};
    GaugeImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

/** StatsD implementation of a `Meter` metric.
 *
 *  Accumulates unsigned event counts between timer ticks; on each tick
 *  `flush()` emits a `|m` StatsD meter line and resets the accumulator to
 *  zero, mirroring the counter pattern.  Increments are dispatched to the
 *  I/O thread so `value_` is never accessed concurrently.
 */
class StatsDMeterImpl : public MeterImpl, public StatsDMetricBase
{
public:
    /** Construct and register the meter with the collector.
     *
     *  @param name Metric name appended to the collector prefix.
     *  @param impl The owning collector.
     */
    explicit StatsDMeterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDMeterImpl() override;

    /** Schedule an increment on the I/O thread.
     *
     *  Thread-safe; dispatches `doIncrement` asynchronously.
     *  @param amount Unsigned count to add to the meter.
     */
    void
    increment(MeterImpl::value_type amount) override;

    /** Serialize and queue the accumulated count if the meter is dirty.
     *
     *  Emits a `|m` StatsD line and resets `value_`.  Must be called on
     *  the I/O thread.
     */
    void
    flush();

    /** Apply the increment and mark the meter dirty.  I/O-thread only. */
    void
    doIncrement(MeterImpl::value_type amount);

    /** Called by the timer tick to flush pending data. */
    void
    doProcess() override;

    StatsDMeterImpl&
    operator=(StatsDMeterImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    MeterImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

/** Concrete `StatsDCollector` implementation.
 *
 *  Owns an `io_context`, a connected UDP socket, and a background thread that
 *  runs the event loop.  All metric mutations and all network I/O execute
 *  exclusively on that thread; no per-metric mutex is required.
 *
 *  The `metrics_` registry is the only state shared across threads; it is
 *  protected by `metricsLock_` (recursive to allow re-entrant timer
 *  callbacks).  Metrics add and remove themselves in their constructors and
 *  destructors via `add()` / `remove()`.
 *
 *  `std::enable_shared_from_this` lets metric implementations capture a
 *  `shared_ptr` to the collector, ensuring the collector outlives any
 *  outstanding metric object.
 *
 *  @note `thread_` is declared last so it is initialized after all other
 *      members, since `run()` immediately touches the socket and timer.
 */
class StatsDCollectorImp : public StatsDCollector,
                           public std::enable_shared_from_this<StatsDCollectorImp>
{
private:
    /** Maximum UDP payload in bytes; sized for typical Ethernet MTU minus
     *  IP and UDP headers to avoid datagram fragmentation. */
    static constexpr auto kMAX_PACKET_SIZE = 1472;

    Journal journal_;
    IP::Endpoint address_;
    std::string prefix_;
    boost::asio::io_context io_context_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    boost::asio::ip::udp::socket socket_;
    /** Pending serialized metric strings awaiting the next `sendBuffers()`. */
    std::deque<std::string> data_;
    std::recursive_mutex metricsLock_;
    List<StatsDMetricBase> metrics_;

    // Must come last for order of init
    std::thread thread_;

    /** Convert a Beast IP endpoint to a Boost.Asio UDP endpoint. */
    static boost::asio::ip::udp::endpoint
    toEndpoint(IP::Endpoint const& ep)
    {
        return boost::asio::ip::udp::endpoint(ep.address(), ep.port());
    }

public:
    /** Construct the collector and start the background I/O thread.
     *
     *  The work guard is installed before the thread starts so the
     *  `io_context` does not exit immediately.  The background thread runs
     *  `run()`, which connects the UDP socket and starts the periodic timer.
     *
     *  @param address UDP destination (StatsD server host and port).
     *  @param prefix  String prepended to every metric name.
     *  @param journal Logging destination.
     */
    StatsDCollectorImp(IP::Endpoint address, std::string prefix, Journal journal)
        : journal_(journal)
        , address_(std::move(address))
        , prefix_(std::move(prefix))
        , work_(boost::asio::make_work_guard(io_context_))
        , strand_(boost::asio::make_strand(io_context_))
        , timer_(io_context_)
        , socket_(io_context_)
        , thread_(&StatsDCollectorImp::run, this)
    {
    }

    /** Shut down the collector in a safe, ordered sequence.
     *
     *  Cancels the timer, resets the work guard so `io_context::run()` can
     *  drain, then joins the background thread.  The socket is shut down and
     *  closed inside `run()` after `io_context::run()` returns, followed by a
     *  `poll()` to flush any trailing completion handlers.
     */
    ~StatsDCollectorImp() override
    {
        try
        {
            timer_.cancel();
        }
        catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
        {
            // ignored
        }

        work_.reset();
        thread_.join();
    }

    /** Create a Hook that fires a callback once per timer tick.
     *  @param handler Callback invoked on the I/O thread each second.
     *  @return A `Hook` handle owning the implementation.
     */
    Hook
    makeHook(HookImpl::HandlerType const& handler) override
    {
        return Hook(std::make_shared<detail::StatsDHookImpl>(handler, shared_from_this()));
    }

    /** Create a Counter that accumulates and reports signed deltas.
     *  @param name Metric name; prefixed by `prefix_` when serialized.
     *  @return A `Counter` handle owning the implementation.
     */
    Counter
    makeCounter(std::string const& name) override
    {
        return Counter(std::make_shared<detail::StatsDCounterImpl>(name, shared_from_this()));
    }

    /** Create an Event that immediately reports each timing observation.
     *  @param name Metric name; prefixed by `prefix_` when serialized.
     *  @return An `Event` handle owning the implementation.
     */
    Event
    makeEvent(std::string const& name) override
    {
        return Event(std::make_shared<detail::StatsDEventImpl>(name, shared_from_this()));
    }

    /** Create a Gauge that reports an instantaneous unsigned level.
     *  @param name Metric name; prefixed by `prefix_` when serialized.
     *  @return A `Gauge` handle owning the implementation.
     */
    Gauge
    makeGauge(std::string const& name) override
    {
        return Gauge(std::make_shared<detail::StatsDGaugeImpl>(name, shared_from_this()));
    }

    /** Create a Meter that accumulates and reports unsigned event counts.
     *  @param name Metric name; prefixed by `prefix_` when serialized.
     *  @return A `Meter` handle owning the implementation.
     */
    Meter
    makeMeter(std::string const& name) override
    {
        return Meter(std::make_shared<detail::StatsDMeterImpl>(name, shared_from_this()));
    }

    //--------------------------------------------------------------------------

    /** Register a metric in the polling registry.
     *
     *  Called from the metric's constructor; thread-safe via `metricsLock_`.
     *  @param metric The metric to register; must outlive the call to `remove`.
     */
    void
    add(StatsDMetricBase& metric)
    {
        std::scoped_lock const _(metricsLock_);
        metrics_.pushBack(metric);
    }

    /** Deregister a metric from the polling registry.
     *
     *  Called from the metric's destructor; thread-safe via `metricsLock_`.
     *  Uses `iteratorTo` for O(1) removal from the intrusive list.
     *  @param metric The metric to remove; must have been previously `add`ed.
     */
    void
    remove(StatsDMetricBase& metric)
    {
        std::scoped_lock const _(metricsLock_);
        metrics_.erase(metrics_.iteratorTo(metric));
    }

    //--------------------------------------------------------------------------

    /** Return the `io_context` used by this collector.
     *
     *  Metric implementations call this to dispatch their mutations onto the
     *  I/O thread.
     */
    boost::asio::io_context&
    getIoContext()
    {
        return io_context_;
    }

    /** Return the metric-name prefix string. */
    std::string const&
    prefix() const
    {
        return prefix_;
    }

    /** Append a serialized metric string to the pending send queue.
     *
     *  Must be called on the I/O thread (via the strand); called by
     *  `postBuffer`.
     *  @param buffer A fully formatted StatsD line (e.g. `"prefix.name:42|c\n"`).
     */
    void
    doPostBuffer(std::string const& buffer)
    {
        data_.emplace_back(buffer);
    }

    /** Schedule `doPostBuffer` on the I/O thread's strand.
     *
     *  Thread-safe entry point used by metric `flush()` methods.
     *  @param buffer A fully formatted StatsD line to enqueue.
     */
    void
    postBuffer(std::string&& buffer)
    {
        boost::asio::dispatch(
            io_context_,
            boost::asio::bind_executor(
                strand_, std::bind(&StatsDCollectorImp::doPostBuffer, this, std::move(buffer))));
    }

    /** Completion handler for `async_send`.
     *
     *  The `keepAlive` shared pointer extends the lifetime of the string data
     *  backing the scatter-gather buffers until Boost.Asio invokes this
     *  handler — at which point the data is no longer needed and is released.
     *  Aborted operations are silently ignored; other errors are logged.
     *
     *  @param keepAlive Shared ownership of the deque whose string data
     *      backs the buffers passed to `async_send`; released on return.
     *  @param ec        Error code from the completed send.
     */
    void
    onSend(
        std::shared_ptr<std::deque<std::string>> /*keepAlive*/,
        boost::system::error_code ec,
        std::size_t)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            if (auto stream = journal_.error())
                stream << "async_send failed: " << ec.message();
            return;
        }
    }

    /** Write UDP payload contents to `std::cerr` for development tracing.
     *
     *  Compiled out entirely unless `BEAST_STATSDCOLLECTOR_TRACING_ENABLED`
     *  is defined to a non-zero value at build time.
     *  @param buffers Scatter-gather buffer sequence about to be sent.
     */
    static void
    log(std::vector<boost::asio::const_buffer> const& buffers)
    {
        (void)buffers;
#if BEAST_STATSDCOLLECTOR_TRACING_ENABLED
        for (auto const& buffer : buffers)
        {
            std::string const s(buffer.data(), boost::asio::buffer_size(buffer));
            std::cerr << s;
        }
        std::cerr << '\n';
#endif
    }

    /** Pack pending metric strings into UDP datagrams and send them.
     *
     *  Uses a greedy strategy: accumulates formatted metric strings into a
     *  scatter-gather buffer until adding the next string would exceed
     *  `kMAX_PACKET_SIZE` bytes, then fires an `async_send` and starts a
     *  fresh batch.  This maximises throughput while keeping each datagram
     *  below typical Ethernet MTU fragmentation thresholds.
     *
     *  Ownership of the string data is transferred into a `shared_ptr<deque>`
     *  (`keepAlive`) before any `async_send` is issued.  That pointer is
     *  passed as the first argument to `onSend`, so Boost.Asio keeps the
     *  strings alive until the completion handler fires.
     *
     *  Must be called on the I/O thread.
     */
    void
    sendBuffers()
    {
        if (data_.empty())
            return;

        std::vector<boost::asio::const_buffer> buffers;
        buffers.reserve(data_.size());
        std::size_t size(0);

        auto keepAlive = std::make_shared<std::deque<std::string>>(std::move(data_));
        data_.clear();

        for (auto const& s : *keepAlive)
        {
            std::size_t const length(s.size());
            XRPL_ASSERT(
                !s.empty(),
                "beast::insight::detail::StatsDCollectorImp::sendBuffers : "
                "non-empty payload");
            if (!buffers.empty() && (size + length) > kMAX_PACKET_SIZE)
            {
                log(buffers);
                socket_.async_send(
                    buffers,
                    std::bind(
                        &StatsDCollectorImp::onSend,
                        this,
                        keepAlive,
                        std::placeholders::_1,
                        std::placeholders::_2));
                buffers.clear();
                size = 0;
            }

            buffers.emplace_back(&s[0], length);
            size += length;
        }

        if (!buffers.empty())
        {
            log(buffers);
            socket_.async_send(
                buffers,
                std::bind(
                    &StatsDCollectorImp::onSend,
                    this,
                    keepAlive,
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    /** Arm the 1-second repeating timer.  Must be called on the I/O thread. */
    void
    setTimer()
    {
        using namespace std::chrono_literals;
        timer_.expires_after(1s);
        timer_.async_wait(std::bind(&StatsDCollectorImp::onTimer, this, std::placeholders::_1));
    }

    /** Timer callback: poll all registered metrics and ship pending data.
     *
     *  Acquires `metricsLock_`, calls `doProcess()` on every registered
     *  metric (which may call `postBuffer` for dirty metrics), then calls
     *  `sendBuffers()` to dispatch the accumulated UDP writes.  Re-arms the
     *  timer before returning.  Aborted operations (i.e. on shutdown) are
     *  silently ignored.
     *
     *  @param ec Error code; `operation_aborted` signals shutdown.
     */
    void
    onTimer(boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            if (auto stream = journal_.error())
                stream << "onTimer failed: " << ec.message();
            return;
        }

        std::scoped_lock const _(metricsLock_);

        for (auto& m : metrics_)
            m.doProcess();

        sendBuffers();

        setTimer();
    }

    /** Background thread entry point: connect the socket and run the event loop.
     *
     *  Connects the UDP socket to the StatsD endpoint (a connect on a UDP
     *  socket sets the default destination, enabling subsequent `async_send`
     *  calls without specifying the address each time).  Arms the timer and
     *  then blocks in `io_context::run()` until the work guard is released
     *  by the destructor.  After `run()` returns, shuts down and closes the
     *  socket, then calls `poll()` to drain any trailing completion handlers.
     */
    void
    run()
    {
        boost::system::error_code ec;

        if (socket_.connect(toEndpoint(address_), ec))
        {
            if (auto stream = journal_.error())
                stream << "Connect failed: " << ec.message();
            return;
        }

        setTimer();

        io_context_.run();

        // NOLINTNEXTLINE(bugprone-unused-return-value)
        socket_.shutdown(boost::asio::ip::udp::socket::shutdown_send, ec);

        socket_.close();

        io_context_.poll();
    }
};

//------------------------------------------------------------------------------

StatsDHookImpl::StatsDHookImpl(HandlerType handler, std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), handler_(std::move(handler))
{
    impl_->add(*this);
}

StatsDHookImpl::~StatsDHookImpl()
{
    impl_->remove(*this);
}

void
StatsDHookImpl::doProcess()
{
    handler_();
}

//------------------------------------------------------------------------------

StatsDCounterImpl::StatsDCounterImpl(
    std::string name,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(std::move(name))
{
    impl_->add(*this);
}

StatsDCounterImpl::~StatsDCounterImpl()
{
    impl_->remove(*this);
}

void
StatsDCounterImpl::increment(CounterImpl::value_type amount)
{
    boost::asio::dispatch(
        impl_->getIoContext(),
        std::bind(
            &StatsDCounterImpl::doIncrement,
            std::static_pointer_cast<StatsDCounterImpl>(shared_from_this()),
            amount));
}

void
StatsDCounterImpl::flush()
{
    if (dirty_)
    {
        dirty_ = false;
        std::stringstream ss;
        ss << impl_->prefix() << "." << name_ << ":" << value_ << "|c"
           << "\n";
        value_ = 0;
        impl_->postBuffer(ss.str());
    }
}

void
StatsDCounterImpl::doIncrement(CounterImpl::value_type amount)
{
    value_ += amount;
    dirty_ = true;
}

void
StatsDCounterImpl::doProcess()
{
    flush();
}

//------------------------------------------------------------------------------

StatsDEventImpl::StatsDEventImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(std::move(name))
{
}

void
StatsDEventImpl::notify(EventImpl::value_type const& value)
{
    boost::asio::dispatch(
        impl_->getIoContext(),
        std::bind(
            &StatsDEventImpl::doNotify,
            std::static_pointer_cast<StatsDEventImpl>(shared_from_this()),
            value));
}

void
StatsDEventImpl::doNotify(EventImpl::value_type const& value)
{
    std::stringstream ss;
    ss << impl_->prefix() << "." << name_ << ":" << value.count() << "|ms"
       << "\n";
    impl_->postBuffer(ss.str());
}

//------------------------------------------------------------------------------

StatsDGaugeImpl::StatsDGaugeImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(std::move(name))
{
    impl_->add(*this);
}

StatsDGaugeImpl::~StatsDGaugeImpl()
{
    impl_->remove(*this);
}

void
StatsDGaugeImpl::set(GaugeImpl::value_type value)
{
    boost::asio::dispatch(
        impl_->getIoContext(),
        std::bind(
            &StatsDGaugeImpl::doSet,
            std::static_pointer_cast<StatsDGaugeImpl>(shared_from_this()),
            value));
}

void
StatsDGaugeImpl::increment(GaugeImpl::difference_type amount)
{
    boost::asio::dispatch(
        impl_->getIoContext(),
        std::bind(
            &StatsDGaugeImpl::doIncrement,
            std::static_pointer_cast<StatsDGaugeImpl>(shared_from_this()),
            amount));
}

void
StatsDGaugeImpl::flush()
{
    if (dirty_)
    {
        dirty_ = false;
        std::stringstream ss;
        ss << impl_->prefix() << "." << name_ << ":" << value_ << "|g"
           << "\n";
        impl_->postBuffer(ss.str());
    }
}

void
StatsDGaugeImpl::doSet(GaugeImpl::value_type value)
{
    value_ = value;

    if (value_ != last_value_)
    {
        last_value_ = value_;
        dirty_ = true;
    }
}

void
StatsDGaugeImpl::doIncrement(GaugeImpl::difference_type amount)
{
    GaugeImpl::value_type value(value_);

    if (amount > 0)
    {
        GaugeImpl::value_type const d(static_cast<GaugeImpl::value_type>(amount));
        value += (d >= std::numeric_limits<GaugeImpl::value_type>::max() - value_)
            ? std::numeric_limits<GaugeImpl::value_type>::max() - value_
            : d;
    }
    else if (amount < 0)
    {
        GaugeImpl::value_type const d(static_cast<GaugeImpl::value_type>(-amount));
        value = (d >= value) ? 0 : value - d;
    }

    doSet(value);
}

void
StatsDGaugeImpl::doProcess()
{
    flush();
}

//------------------------------------------------------------------------------

StatsDMeterImpl::StatsDMeterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(std::move(name))
{
    impl_->add(*this);
}

StatsDMeterImpl::~StatsDMeterImpl()
{
    impl_->remove(*this);
}

void
StatsDMeterImpl::increment(MeterImpl::value_type amount)
{
    boost::asio::dispatch(
        impl_->getIoContext(),
        std::bind(
            &StatsDMeterImpl::doIncrement,
            std::static_pointer_cast<StatsDMeterImpl>(shared_from_this()),
            amount));
}

void
StatsDMeterImpl::flush()
{
    if (dirty_)
    {
        dirty_ = false;
        std::stringstream ss;
        ss << impl_->prefix() << "." << name_ << ":" << value_ << "|m"
           << "\n";
        value_ = 0;
        impl_->postBuffer(ss.str());
    }
}

void
StatsDMeterImpl::doIncrement(MeterImpl::value_type amount)
{
    value_ += amount;
    dirty_ = true;
}

void
StatsDMeterImpl::doProcess()
{
    flush();
}

}  // namespace detail

//------------------------------------------------------------------------------

std::shared_ptr<StatsDCollector>
StatsDCollector::make(IP::Endpoint const& address, std::string const& prefix, Journal journal)
{
    return std::make_shared<detail::StatsDCollectorImp>(address, prefix, journal);
}

}  // namespace beast::insight
