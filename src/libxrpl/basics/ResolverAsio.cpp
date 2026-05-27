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

/** Mix-in to track when all pending I/O is complete.
    Derived classes must be callable with this signature:
        void asyncHandlersComplete()
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
        // Destroying the object with I/O pending? Not a clean exit!
        XRPL_ASSERT(pending_.load() == 0, "xrpl::AsyncObject::~AsyncObject : nothing pending");
    }

    /** RAII container that maintains the count of pending I/O.
        Bind this into the argument list of every handler passed
        to an initiating function.
    */
    class CompletionCounter
    {
    public:
        explicit CompletionCounter(Derived* owner) : owner_(owner)
        {
            ++owner_->pending_;
        }

        CompletionCounter(CompletionCounter const& other) : owner_(other.owner_)
        {
            ++owner_->pending_;
        }

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

    void
    addReference()
    {
        ++pending_;
    }

    void
    removeReference()
    {
        if (--pending_ == 0)
            (static_cast<Derived*>(this))->asyncHandlersComplete();
    }

private:
    // The number of handlers pending.
    std::atomic<int> pending_;

    friend Derived;
};

class ResolverAsioImpl : public ResolverAsio, public AsyncObject<ResolverAsioImpl>
{
public:
    using HostAndPort = std::pair<std::string, std::string>;

    beast::Journal journal;

    boost::asio::io_context& ioContext;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;
    boost::asio::ip::tcp::resolver resolver;

    std::condition_variable cv;
    std::mutex mut;
    bool asyncHandlersCompleted{true};

    std::atomic<bool> stopCalled;
    std::atomic<bool> stopped;

    // Represents a unit of work for the resolver to do
    struct Work
    {
        std::vector<std::string> names;
        HandlerType handler;

        template <class StringSequence>
        Work(StringSequence const& inNames, HandlerType handler) : handler(std::move(handler))
        {
            names.reserve(inNames.size());

            std::reverse_copy(inNames.begin(), inNames.end(), std::back_inserter(names));
        }
    };

    std::deque<Work> work;

    ResolverAsioImpl(boost::asio::io_context& ioContext, beast::Journal journal)
        : journal(journal)
        , ioContext(ioContext)
        , strand(boost::asio::make_strand(ioContext))
        , resolver(ioContext)
        , stopCalled(false)
        , stopped(true)
    {
    }

    ~ResolverAsioImpl() override
    {
        XRPL_ASSERT(work.empty(), "xrpl::ResolverAsioImpl::~ResolverAsioImpl : no pending work");
        XRPL_ASSERT(stopped, "xrpl::ResolverAsioImpl::~ResolverAsioImpl : stopped");
    }

    //-------------------------------------------------------------------------
    // AsyncObject
    void
    asyncHandlersComplete()
    {
        std::unique_lock<std::mutex> const lk{mut};
        asyncHandlersCompleted = true;
        cv.notify_all();
    }

    //--------------------------------------------------------------------------
    //
    // Resolver
    //
    //--------------------------------------------------------------------------

    void
    start() override
    {
        XRPL_ASSERT(stopped == true, "xrpl::ResolverAsioImpl::start : stopped");
        XRPL_ASSERT(stopCalled == false, "xrpl::ResolverAsioImpl::start : not stopping");

        if (stopped.exchange(false))
        {
            {
                std::scoped_lock const lk{mut};
                asyncHandlersCompleted = false;
            }
            addReference();
        }
    }

    void
    stopAsync() override
    {
        if (!stopCalled.exchange(true))
        {
            boost::asio::dispatch(
                ioContext,
                boost::asio::bind_executor(
                    strand, std::bind(&ResolverAsioImpl::doStop, this, CompletionCounter(this))));

            JLOG(journal.debug()) << "Queued a stop request";
        }
    }

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

    void
    resolve(std::vector<std::string> const& names, HandlerType const& handler) override
    {
        XRPL_ASSERT(stopCalled == false, "xrpl::ResolverAsioImpl::resolve : not stopping");
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::resolve : names non-empty");

        // TODO NIKB use rvalue references to construct and move
        //           reducing cost.
        boost::asio::dispatch(
            ioContext,
            boost::asio::bind_executor(
                strand,
                std::bind(
                    &ResolverAsioImpl::doResolve, this, names, handler, CompletionCounter(this))));
    }

    //-------------------------------------------------------------------------
    // Resolver
    void
    doStop(CompletionCounter)
    {
        XRPL_ASSERT(stopCalled == true, "xrpl::ResolverAsioImpl::doStop : stopping");

        if (!stopped.exchange(true))
        {
            work.clear();
            resolver.cancel();

            removeReference();
        }
    }

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

        // If we get an error message back, we don't return any
        // results that we may have gotten.
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
            ioContext,
            boost::asio::bind_executor(
                strand, std::bind(&ResolverAsioImpl::doWork, this, CompletionCounter(this))));
    }

    static HostAndPort
    parseName(std::string const& str)
    {
        // first attempt to parse as an endpoint (IP addr + port).
        // If that doesn't succeed, fall back to generic name + port parsing

        if (auto const result = beast::IP::Endpoint::fromStringChecked(str))
        {
            return make_pair(result->address().to_string(), std::to_string(result->port()));
        }

        // generic name/port parsing, which doesn't work for
        // IPv6 addresses in particular because it considers a colon
        // a port separator

        // Attempt to find the first and last non-whitespace
        auto const findWhitespace =
            std::bind(&std::isspace<std::string::value_type>, std::placeholders::_1, std::locale());

        auto hostFirst = std::ranges::find_if_not(str, findWhitespace);

        auto portLast =
            std::ranges::find_if_not(std::ranges::reverse_view(str), findWhitespace).base();

        // This should only happen for all-whitespace strings
        if (hostFirst >= portLast)
            return std::make_pair(std::string(), std::string());

        // Attempt to find the first and last valid port separators
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

    void
    doWork(CompletionCounter)
    {
        if (stopCalled)
            return;

        // We don't have any work to do at this time
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
                ioContext,
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

    void
    doResolve(std::vector<std::string> const& names, HandlerType const& handler, CompletionCounter)
    {
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::doResolve : names non-empty");

        if (!stopCalled)
        {
            work.emplace_back(names, handler);

            JLOG(journal.debug()) << "Queued new job with " << names.size() << " tasks. "
                                  << work.size() << " jobs outstanding.";

            if (!work.empty())
            {
                boost::asio::post(
                    ioContext,
                    boost::asio::bind_executor(
                        strand,
                        std::bind(&ResolverAsioImpl::doWork, this, CompletionCounter(this))));
            }
        }
    }
};

//-----------------------------------------------------------------------------

std::unique_ptr<ResolverAsio>
ResolverAsio::make(boost::asio::io_context& ioContext, beast::Journal journal)
{
    return std::make_unique<ResolverAsioImpl>(ioContext, journal);
}

//-----------------------------------------------------------------------------
Resolver::~Resolver() = default;
}  // namespace xrpl
