/** @file
 *  Concrete asynchronous DNS resolver built on Boost.Asio.
 *
 *  Provides `ResolverAsioImpl`, the production implementation of the
 *  `ResolverAsio` / `Resolver` interface used by the peer-discovery and
 *  overlay subsystems to translate string-form peer addresses (e.g.
 *  `r.ripple.com:51235`) into `beast::IP::Endpoint` objects.
 *
 *  Also anchors the vtable for `Resolver` by defining its pure-virtual
 *  destructor in this translation unit.
 */

#include <xrpl/basics/ResolverAsio.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Resolver.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/error_code.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
#include <locale>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** CRTP mix-in that tracks outstanding asynchronous handler completions.
 *
 *  Maintains an atomic counter, `pending_`, that is incremented for each
 *  live async operation and decremented when it finishes.  When the counter
 *  reaches zero the mix-in calls `Derived::asyncHandlersComplete()`, signalling
 *  that the object can safely be torn down.
 *
 *  Two increment/decrement mechanisms are provided:
 *  - `CompletionCounter` — RAII guard; bind one into every Asio handler.
 *  - `addReference` / `removeReference` — manual pair used for the object's
 *    "lifetime reference" held between `start()` and `doStop()`.
 *
 *  @tparam Derived  The owning class, which must expose
 *      `void asyncHandlersComplete()`.
 *
 *  @note The destructor asserts `pending_ == 0`, so destroying the object
 *      with any live handlers is a hard failure.
 */
template <class Derived>
class AsyncObject
{
    AsyncObject() : pending_(0)
    {
    }

public:
    ~AsyncObject()
    {
        XRPL_ASSERT(pending_.load() == 0, "xrpl::AsyncObject::~AsyncObject : nothing pending");
    }

    /** RAII guard that keeps the pending-handler count non-zero for its lifetime.
     *
     *  Increment `pending_` on construction (including copy construction, which
     *  is what Asio does when it copies a bound handler into its internal queue)
     *  and decrement it on destruction, calling `asyncHandlersComplete()` on
     *  the owning `Derived` object when the count reaches zero.
     *
     *  Bind one of these into the argument list of every handler passed to an
     *  Asio initiating function so that the counter accurately reflects the
     *  number of handlers that have not yet returned.
     *
     *  @note Copy-assignment is deleted; a counter should only be moved through
     *      copy-construction by Asio's internal machinery.
     */
    class CompletionCounter
    {
    public:
        /** Construct and increment the owner's pending count.
         *
         *  @param owner  The `Derived` instance whose counter to increment.
         *      Must not be null and must outlive this counter.
         */
        explicit CompletionCounter(Derived* owner) : owner_(owner)
        {
            ++owner_->pending_;
        }

        /** Copy-construct, incrementing the shared pending count.
         *
         *  Asio copies bound handlers when posting them to an executor;
         *  this constructor ensures each copy independently holds a reference.
         */
        CompletionCounter(CompletionCounter const& other) : owner_(other.owner_)
        {
            ++owner_->pending_;
        }

        /** Destroy and decrement the pending count.
         *
         *  Calls `Derived::asyncHandlersComplete()` when the count reaches zero.
         */
        ~CompletionCounter()
        {
            if (--owner_->pending_ == 0)
                owner_->asyncHandlersComplete();
        }

        CompletionCounter&
        operator=(CompletionCounter const&) = delete;

    private:
        Derived* owner_;
    };

    /** Increment the pending count without an RAII guard.
     *
     *  Must be paired with a matching `removeReference()` call.  Used to
     *  hold a "lifetime reference" while the resolver is running but has no
     *  active Asio handlers.
     */
    void
    addReference()
    {
        ++pending_;
    }

    /** Decrement the pending count and fire `asyncHandlersComplete()` if zero.
     *
     *  Pair with `addReference()`.  Dropping the last reference triggers the
     *  same completion callback that `CompletionCounter` uses.
     */
    void
    removeReference()
    {
        if (--pending_ == 0)
            (static_cast<Derived*>(this))->asyncHandlersComplete();
    }

private:
    /** Number of live handlers plus any manually held references. */
    std::atomic<int> pending_;

    friend Derived;
};

/** Concrete implementation of `ResolverAsio` using Boost.Asio's `tcp::resolver`.
 *
 *  Resolves hostname batches serially — one DNS query at a time — to avoid
 *  exhausting file descriptors or hitting resolver rate limits.  All mutable
 *  I/O-path state is accessed exclusively on a `boost::asio::strand`, so no
 *  explicit locking is needed there.  A `std::mutex` / `std::condition_variable`
 *  pair is used only for the synchronous `stop()` call.
 *
 *  Lifecycle: the object starts in a *stopped* state.  Call `start()` before
 *  submitting any resolution requests, and call `stop()` (or `stopAsync()`) to
 *  drain in-flight work before destruction.  The destructor asserts that the
 *  work queue is empty and the stopped flag is set.
 *
 *  @see AsyncObject for the pending-handler reference-counting scheme.
 */
class ResolverAsioImpl : public ResolverAsio, public AsyncObject<ResolverAsioImpl>
{
public:
    /** Parsed result of a `host:port` string. */
    using HostAndPort = std::pair<std::string, std::string>;

    /** Journal used for debug/error logging throughout this instance. */
    beast::Journal journal;

    /** The `io_context` on which all async operations are dispatched. */
    boost::asio::io_context& io_context;

    /** Strand that serialises all access to the work queue and resolver state. */
    boost::asio::strand<boost::asio::io_context::executor_type> strand;

    /** Underlying Asio TCP resolver. */
    boost::asio::ip::tcp::resolver resolver;

    /** Condition variable used by `stop()` to wait for all handlers to finish. */
    std::condition_variable cv;

    /** Mutex that guards `asyncHandlersCompleted`. */
    std::mutex mut;

    /** Set to `true` (under `mut`) when `AsyncObject::pending_` reaches zero. */
    bool asyncHandlersCompleted{true};

    /** Set to `true` atomically by `stopAsync()`; prevents new work from being enqueued. */
    std::atomic<bool> stop_called;

    /** `true` when the resolver is not running (initial state and after `doStop()`). */
    std::atomic<bool> stopped;

    /** A batch of hostnames to resolve, plus the callback to invoke per result.
     *
     *  Names are stored in **reverse** order so that serving the next entry is
     *  a `pop_back()` — O(1) without element shifting.
     */
    struct Work
    {
        /** Hostnames remaining to resolve, stored in reverse submission order. */
        std::vector<std::string> names;

        /** Callback invoked with each resolved hostname and its endpoints. */
        HandlerType handler;

        /** Construct a `Work` item from a sequence of names and a handler.
         *
         *  Names are reverse-copied so that `names.back()` yields the first
         *  submitted name and `pop_back()` serves them in FIFO order.
         *
         *  @param inNames  Input range of hostnames to resolve.
         *  @param handler  Callback to invoke for each resolved name.
         */
        template <class StringSequence>
        Work(StringSequence const& inNames, HandlerType handler) : handler(std::move(handler))
        {
            names.reserve(inNames.size());

            std::reverse_copy(inNames.begin(), inNames.end(), std::back_inserter(names));
        }
    };

    /** Queue of pending resolution batches, drained serially by `doWork()`. */
    std::deque<Work> work;

    /** Construct in stopped state; no resolution work is performed until `start()` is called.
     *
     *  @param ioContext  The `io_context` for all async I/O.
     *  @param journal    Logging sink for debug and error messages.
     */
    ResolverAsioImpl(boost::asio::io_context& ioContext, beast::Journal journal)
        : journal(journal)
        , io_context(ioContext)
        , strand(boost::asio::make_strand(ioContext))
        , resolver(ioContext)
        , stop_called(false)
        , stopped(true)
    {
    }

    /** Destroy the resolver.
     *
     *  Asserts that `stop()` has been called and the work queue is empty;
     *  destroying the object without stopping first is a programming error.
     */
    ~ResolverAsioImpl() override
    {
        XRPL_ASSERT(work.empty(), "xrpl::ResolverAsioImpl::~ResolverAsioImpl : no pending work");
        XRPL_ASSERT(stopped, "xrpl::ResolverAsioImpl::~ResolverAsioImpl : stopped");
    }

    // --- AsyncObject callback ---

    /** Called by `AsyncObject` when the pending-handler count reaches zero.
     *
     *  Sets `asyncHandlersCompleted` under `mut` and notifies `stop()` via
     *  `cv` so it can unblock and return.
     */
    void
    asyncHandlersComplete()
    {
        std::unique_lock<std::mutex> const lk{mut};
        asyncHandlersCompleted = true;
        cv.notify_all();
    }

    // --- Resolver interface ---

    /** Transition from stopped to running and acquire the lifetime reference.
     *
     *  Clears the stopped flag and calls `addReference()` to hold a "lifetime
     *  reference" in `AsyncObject`, preventing `asyncHandlersComplete()` from
     *  firing prematurely while no Asio handlers are queued.  Must be called
     *  before any `resolve()` requests are submitted.
     *
     *  @note Asserts that the resolver is currently stopped and that no stop
     *      has been requested — calling `start()` after `stopAsync()` is a
     *      programming error.
     */
    void
    start() override
    {
        XRPL_ASSERT(stopped == true, "xrpl::ResolverAsioImpl::start : stopped");
        XRPL_ASSERT(stop_called == false, "xrpl::ResolverAsioImpl::start : not stopping");

        if (stopped.exchange(false))
        {
            {
                std::scoped_lock const lk{mut};
                asyncHandlersCompleted = false;
            }
            addReference();
        }
    }

    /** Request an asynchronous stop; idempotent.
     *
     *  Posts `doStop()` to the strand exactly once (subsequent calls are
     *  no-ops).  `doStop()` will clear the work queue, cancel any in-flight
     *  Asio resolution, and drop the lifetime reference.
     */
    void
    stopAsync() override
    {
        if (!stop_called.exchange(true))
        {
            boost::asio::dispatch(
                io_context,
                boost::asio::bind_executor(
                    strand, std::bind(&ResolverAsioImpl::doStop, this, CompletionCounter(this))));

            JLOG(journal.debug()) << "Queued a stop request";
        }
    }

    /** Synchronously stop the resolver and block until all handlers have returned.
     *
     *  Calls `stopAsync()` then waits on `cv` for `asyncHandlersCompleted` to
     *  become `true`.  Returns only after every `CompletionCounter` has been
     *  destroyed, guaranteeing that no handler is still executing.
     */
    void
    stop() override
    {
        stopAsync();

        JLOG(journal.debug()) << "Waiting to stop";
        std::unique_lock<std::mutex> lk{mut};
        cv.wait(lk, [this] { return asyncHandlersCompleted; });
        lk.unlock();
        JLOG(journal.debug()) << "Stopped";
    }

    /** Enqueue a batch of hostnames for asynchronous resolution.
     *
     *  Serialises onto the strand and calls `doResolve()`, which wraps the
     *  batch in a `Work` item and triggers `doWork()` to start draining the
     *  queue one query at a time.
     *
     *  @param names    Non-empty list of hostnames (or `host:port` strings)
     *      to resolve.
     *  @param handler  Callback invoked once per name with its resolved
     *      `beast::IP::Endpoint` list (empty on error).
     *
     *  @note Must not be called after `stopAsync()` or `stop()`.
     */
    void
    resolve(std::vector<std::string> const& names, HandlerType const& handler) override
    {
        XRPL_ASSERT(stop_called == false, "xrpl::ResolverAsioImpl::resolve : not stopping");
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::resolve : names non-empty");

        // TODO NIKB use rvalue references to construct and move
        //           reducing cost.
        boost::asio::dispatch(
            io_context,
            boost::asio::bind_executor(
                strand,
                std::bind(
                    &ResolverAsioImpl::doResolve, this, names, handler, CompletionCounter(this))));
    }

    // --- Strand-serialised implementation helpers ---

    /** Strand handler that performs the actual shutdown sequence.
     *
     *  Clears the work queue, cancels any in-flight Asio resolution (causing
     *  pending handlers to be called back with `operation_aborted`), and drops
     *  the lifetime reference acquired in `start()`.  Idempotent: the
     *  `stopped.exchange(true)` guard ensures the body runs only once even if
     *  `doStop()` is somehow posted twice.
     *
     *  @param  The `CompletionCounter` parameter is intentionally unnamed;
     *      it is passed solely so the pending count stays non-zero until
     *      this handler returns.
     */
    void
    doStop(CompletionCounter)
    {
        XRPL_ASSERT(stop_called == true, "xrpl::ResolverAsioImpl::doStop : stopping");

        if (!stopped.exchange(true))
        {
            work.clear();
            resolver.cancel();

            removeReference();
        }
    }

    /** Strand handler called when a single `async_resolve` completes.
     *
     *  Silently discards `operation_aborted` results (these occur when
     *  `doStop()` cancels the resolver and produce no user-visible callback).
     *  For any other outcome — success or a genuine error — converts the Asio
     *  result set into a `std::vector<beast::IP::Endpoint>` (empty on error)
     *  and invokes `handler`.  After invoking the handler, posts a new
     *  `doWork()` call to the strand to process the next queued name.
     *
     *  @param name     The original name string that was resolved.
     *  @param ec       Error code from `async_resolve`; non-zero means `results`
     *      should be ignored.
     *  @param handler  User-supplied callback to invoke with the resolved
     *      endpoints.
     *  @param results  Asio resolver result set; valid only when `ec` is clear.
     *  @param         Unnamed `CompletionCounter` that keeps the pending count
     *      non-zero for the duration of this handler.
     */
    void
    doFinish(
        std::string name,
        boost::system::error_code const& ec,
        HandlerType handler,
        boost::asio::ip::tcp::resolver::results_type results,
        CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        std::vector<beast::IP::Endpoint> addresses;
        auto iter = results.begin();

        // Any error means we return an empty address list to the handler.
        if (!ec)
        {
            while (iter != results.end())
            {
                addresses.push_back(beast::IPAddressConversion::fromAsio(*iter));
                ++iter;
            }
        }

        handler(name, addresses);

        boost::asio::post(
            io_context,
            boost::asio::bind_executor(
                strand, std::bind(&ResolverAsioImpl::doWork, this, CompletionCounter(this))));
    }

    /** Parse a `host:port` string into its component parts.
     *
     *  Uses a two-tier strategy to handle both IP addresses and hostnames:
     *
     *  1. Attempt `beast::IP::Endpoint::fromStringChecked()`, which correctly
     *     handles IPv6 bracket notation (e.g. `[::1]:6006`) where a raw
     *     colon-split would produce incorrect results.
     *  2. Fall back to a whitespace-trimmed scan that treats both `:` and
     *     whitespace as the host/port separator, so `"r.ripple.com 51235"` and
     *     `"r.ripple.com:51235"` are both accepted.
     *
     *  An all-whitespace input returns a pair of empty strings.
     *
     *  @param str  The raw name string from the work queue.
     *  @return A `{host, port}` pair.  Either component may be empty if the
     *      input is malformed.
     */
    static HostAndPort
    parseName(std::string const& str)
    {
        if (auto const result = beast::IP::Endpoint::fromStringChecked(str))
        {
            return make_pair(result->address().to_string(), std::to_string(result->port()));
        }

        // Generic host/port scan — does not handle IPv6 because colons would
        // be misinterpreted as port separators; the branch above handles that.

        auto const findWhitespace =
            std::bind(&std::isspace<std::string::value_type>, std::placeholders::_1, std::locale());

        auto hostFirst = std::ranges::find_if_not(str, findWhitespace);

        auto portLast =
            std::ranges::find_if_not(std::ranges::reverse_view(str), findWhitespace).base();

        // Only reachable for all-whitespace strings.
        if (hostFirst >= portLast)
            return std::make_pair(std::string(), std::string());

        auto const findPortSeparator = [](char const c) -> bool {
            if (std::isspace(static_cast<unsigned char>(c)))
                return true;

            if (c == ':')
                return true;

            return false;
        };

        auto hostLast = std::find_if(hostFirst, portLast, findPortSeparator);

        auto portFirst = std::find_if_not(hostLast, portLast, findPortSeparator);

        return make_pair(std::string(hostFirst, hostLast), std::string(portFirst, portLast));
    }

    /** Strand handler that dequeues and issues one DNS resolution.
     *
     *  Pulls the next name from the front batch (using `pop_back()` on the
     *  reversed `names` vector), parses it with `parseName()`, and calls
     *  `resolver.async_resolve()`.  If `parseName()` returns an empty host the
     *  name is logged and skipped, and `doWork()` is re-posted immediately so
     *  the remaining queue is not stalled.  If the queue is empty or a stop has
     *  been requested the method returns without posting further work.
     *
     *  @param  Unnamed `CompletionCounter` that keeps the pending count
     *      non-zero for the duration of this handler.
     */
    void
    doWork(CompletionCounter)
    {
        if (stop_called)
            return;

        if (work.empty())
            return;

        std::string const name(work.front().names.back());
        HandlerType const handler(work.front().handler);

        work.front().names.pop_back();

        if (work.front().names.empty())
            work.pop_front();

        auto const [host, port] = parseName(name);

        if (host.empty())
        {
            JLOG(journal.error()) << "Unable to parse '" << name << "'";

            boost::asio::post(
                io_context,
                boost::asio::bind_executor(
                    strand, std::bind(&ResolverAsioImpl::doWork, this, CompletionCounter(this))));

            return;
        }

        resolver.async_resolve(
            host,
            port,
            std::bind(
                &ResolverAsioImpl::doFinish,
                this,
                name,
                std::placeholders::_1,
                handler,
                std::placeholders::_2,
                CompletionCounter(this)));
    }

    /** Strand handler that enqueues a new batch of names and triggers draining.
     *
     *  Wraps `names` and `handler` in a `Work` item (which reverse-copies the
     *  names for O(1) `pop_back()` dispatch) and appends it to `work`.  Then
     *  posts `doWork()` to begin processing.  Does nothing if a stop has been
     *  requested.
     *
     *  @param names    Non-empty list of hostnames to resolve.
     *  @param handler  Callback to invoke per resolved name.
     *  @param         Unnamed `CompletionCounter` holding the pending count.
     */
    void
    doResolve(std::vector<std::string> const& names, HandlerType const& handler, CompletionCounter)
    {
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::doResolve : names non-empty");

        if (!stop_called)
        {
            work.emplace_back(names, handler);

            JLOG(journal.debug()) << "Queued new job with " << names.size() << " tasks. "
                                  << work.size() << " jobs outstanding.";

            if (!work.empty())
            {
                boost::asio::post(
                    io_context,
                    boost::asio::bind_executor(
                        strand,
                        std::bind(&ResolverAsioImpl::doWork, this, CompletionCounter(this))));
            }
        }
    }
};

/** Create a `ResolverAsio` backed by `ResolverAsioImpl`.
 *
 *  This is the sole construction path for the concrete resolver.  Callers
 *  receive a `std::unique_ptr<ResolverAsio>` and never see the implementation
 *  type directly.
 *
 *  @param ioContext  The `io_context` on which async operations will run.
 *  @param journal    Logging sink for debug and error messages.
 *  @return Owning pointer to a newly constructed, stopped resolver.
 */
std::unique_ptr<ResolverAsio>
ResolverAsio::make(boost::asio::io_context& ioContext, beast::Journal journal)
{
    return std::make_unique<ResolverAsioImpl>(ioContext, journal);
}

/** Anchor the `Resolver` vtable in this translation unit.
 *
 *  The pure-virtual destructor is declared `= 0` in the header; defining it
 *  here satisfies the ODR requirement and prevents the vtable from being
 *  emitted in every translation unit that includes `Resolver.h`.
 */
Resolver::~Resolver() = default;
}  // namespace xrpl
