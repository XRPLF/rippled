#include <xrpl/basics/Log.h>
#include <xrpl/basics/Resolver.h>
#include <xrpl/basics/ResolverAsio.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
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

    beast::Journal journal_;

    boost::asio::io_context& io_context_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::resolver resolver_;

    std::condition_variable cv_;
    std::mutex mut_;
    bool asyncHandlersCompleted_;

    std::atomic<bool> stop_called_;
    std::atomic<bool> stopped_;

    // Represents a unit of work for the resolver to do
    struct Work
    {
        std::vector<std::string> names;
        HandlerType handler;

        template <class StringSequence>
        Work(StringSequence const& names_, HandlerType const& handler_) : handler(handler_)
        {
            names.reserve(names_.size());

            std::reverse_copy(names_.begin(), names_.end(), std::back_inserter(names));
        }
    };

    std::deque<Work> work_;

    ResolverAsioImpl(boost::asio::io_context& io_context, beast::Journal journal)
        : journal_(journal)
        , io_context_(io_context)
        , strand_(boost::asio::make_strand(io_context))
        , resolver_(io_context)
        , asyncHandlersCompleted_(true)
        , stop_called_(false)
        , stopped_(true)
    {
    }

    ~ResolverAsioImpl() override
    {
        XRPL_ASSERT(work_.empty(), "xrpl::ResolverAsioImpl::~ResolverAsioImpl : no pending work");
        XRPL_ASSERT(stopped_, "xrpl::ResolverAsioImpl::~ResolverAsioImpl : stopped");
    }

    //-------------------------------------------------------------------------
    // AsyncObject
    void
    asyncHandlersComplete()
    {
        std::unique_lock<std::mutex> lk{mut_};
        asyncHandlersCompleted_ = true;
        cv_.notify_all();
    }

    //--------------------------------------------------------------------------
    //
    // Resolver
    //
    //--------------------------------------------------------------------------

    void
    start() override
    {
        XRPL_ASSERT(stopped_ == true, "xrpl::ResolverAsioImpl::start : stopped");
        XRPL_ASSERT(stop_called_ == false, "xrpl::ResolverAsioImpl::start : not stopping");

        if (stopped_.exchange(false) == true)
        {
            {
                std::lock_guard lk{mut_};
                asyncHandlersCompleted_ = false;
            }
            addReference();
        }
    }

    void
    stop_async() override
    {
        if (stop_called_.exchange(true) == false)
        {
            boost::asio::dispatch(
                io_context_,
                boost::asio::bind_executor(
                    strand_, std::bind(&ResolverAsioImpl::do_stop, this, CompletionCounter(this))));

            JLOG(journal_.debug()) << "Queued a stop request";
        }
    }

    void
    stop() override
    {
        stop_async();

        JLOG(journal_.debug()) << "Waiting to stop";
        std::unique_lock<std::mutex> lk{mut_};
        cv_.wait(lk, [this] { return asyncHandlersCompleted_; });
        lk.unlock();
        JLOG(journal_.debug()) << "Stopped";
    }

    void
    resolve(std::vector<std::string> const& names, HandlerType const& handler) override
    {
        XRPL_ASSERT(stop_called_ == false, "xrpl::ResolverAsioImpl::resolve : not stopping");
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::resolve : names non-empty");

        // TODO NIKB use rvalue references to construct and move
        //           reducing cost.
        boost::asio::dispatch(
            io_context_,
            boost::asio::bind_executor(
                strand_,
                std::bind(
                    &ResolverAsioImpl::do_resolve, this, names, handler, CompletionCounter(this))));
    }

    //-------------------------------------------------------------------------
    // Resolver
    void
    do_stop(CompletionCounter)
    {
        XRPL_ASSERT(stop_called_ == true, "xrpl::ResolverAsioImpl::do_stop : stopping");

        if (stopped_.exchange(true) == false)
        {
            work_.clear();
            resolver_.cancel();

            removeReference();
        }
    }

    void
    do_finish(
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
                addresses.push_back(beast::IPAddressConversion::from_asio(*iter));
                ++iter;
            }
        }

        handler(name, addresses);

        boost::asio::post(
            io_context_,
            boost::asio::bind_executor(
                strand_, std::bind(&ResolverAsioImpl::do_work, this, CompletionCounter(this))));
    }

    HostAndPort
    parseName(std::string const& str)
    {
        // first attempt to parse as an endpoint (IP addr + port).
        // If that doesn't succeed, fall back to generic name + port parsing

        if (auto const result = beast::IP::Endpoint::from_string_checked(str))
        {
            return make_pair(result->address().to_string(), std::to_string(result->port()));
        }

        // generic name/port parsing, which doesn't work for
        // IPv6 addresses in particular because it considers a colon
        // a port separator

        // Attempt to find the first and last non-whitespace
        auto const find_whitespace =
            std::bind(&std::isspace<std::string::value_type>, std::placeholders::_1, std::locale());

        auto host_first = std::find_if_not(str.begin(), str.end(), find_whitespace);

        auto port_last = std::find_if_not(str.rbegin(), str.rend(), find_whitespace).base();

        // This should only happen for all-whitespace strings
        if (host_first >= port_last)
            return std::make_pair(std::string(), std::string());

        // Attempt to find the first and last valid port separators
        auto const find_port_separator = [](char const c) -> bool {
            if (std::isspace(static_cast<unsigned char>(c)))
                return true;

            if (c == ':')
                return true;

            return false;
        };

        auto host_last = std::find_if(host_first, port_last, find_port_separator);

        auto port_first = std::find_if_not(host_last, port_last, find_port_separator);

        return make_pair(std::string(host_first, host_last), std::string(port_first, port_last));
    }

    void
    do_work(CompletionCounter)
    {
        if (stop_called_ == true)
            return;

        // We don't have any work to do at this time
        if (work_.empty())
            return;

        std::string const name(work_.front().names.back());
        HandlerType handler(work_.front().handler);

        work_.front().names.pop_back();

        if (work_.front().names.empty())
            work_.pop_front();

        auto const [host, port] = parseName(name);

        if (host.empty())
        {
            JLOG(journal_.error()) << "Unable to parse '" << name << "'";

            boost::asio::post(
                io_context_,
                boost::asio::bind_executor(
                    strand_, std::bind(&ResolverAsioImpl::do_work, this, CompletionCounter(this))));

            return;
        }

        resolver_.async_resolve(
            host,
            port,
            std::bind(
                &ResolverAsioImpl::do_finish,
                this,
                name,
                std::placeholders::_1,
                handler,
                std::placeholders::_2,
                CompletionCounter(this)));
    }

    void
    do_resolve(std::vector<std::string> const& names, HandlerType const& handler, CompletionCounter)
    {
        XRPL_ASSERT(!names.empty(), "xrpl::ResolverAsioImpl::do_resolve : names non-empty");

        if (stop_called_ == false)
        {
            work_.emplace_back(names, handler);

            JLOG(journal_.debug()) << "Queued new job with " << names.size() << " tasks. "
                                   << work_.size() << " jobs outstanding.";

            if (work_.size() > 0)
            {
                boost::asio::post(
                    io_context_,
                    boost::asio::bind_executor(
                        strand_,
                        std::bind(&ResolverAsioImpl::do_work, this, CompletionCounter(this))));
            }
        }
    }
};

//-----------------------------------------------------------------------------

std::unique_ptr<ResolverAsio>
ResolverAsio::New(boost::asio::io_context& io_context, beast::Journal journal)
{
    return std::make_unique<ResolverAsioImpl>(io_context, journal);
}

//-----------------------------------------------------------------------------
Resolver::~Resolver() = default;
}  // namespace xrpl
