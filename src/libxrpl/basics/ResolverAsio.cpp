//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/basics/Log.h>
#include <xrpl/basics/ResolverAsio.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <boost/asio.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <locale>
#include <memory>
#include <mutex>

namespace ripple {

/** Mix-in to track when all pending I/O is complete.
    Derived classes must be callable with this signature:
        void asyncHandlersComplete()
*/
template <class Derived>
class AsyncObject
{
protected:
    AsyncObject() : m_pending(0)
    {
    }

public:
    ~AsyncObject()
    {
        // Destroying the object with I/O pending? Not a clean exit!
        ASSERT(
            m_pending.load() == 0,
            "ripple::AsyncObject::~AsyncObject : nothing pending");
    }

    /** RAII container that maintains the count of pending I/O.
        Bind this into the argument list of every handler passed
        to an initiating function.
    */
    class CompletionCounter
    {
    public:
        explicit CompletionCounter(Derived* owner) : m_owner(owner)
        {
            ++m_owner->m_pending;
        }

        CompletionCounter(CompletionCounter const& other)
            : m_owner(other.m_owner)
        {
            ++m_owner->m_pending;
        }

        ~CompletionCounter()
        {
            if (--m_owner->m_pending == 0)
                m_owner->asyncHandlersComplete();
        }

        CompletionCounter&
        operator=(CompletionCounter const&) = delete;

    private:
        Derived* m_owner;
    };

    void
    addReference()
    {
        ++m_pending;
    }

    void
    removeReference()
    {
        if (--m_pending == 0)
            (static_cast<Derived*>(this))->asyncHandlersComplete();
    }

private:
    // The number of handlers pending.
    std::atomic<int> m_pending;
};

class ResolverAsioImpl : public ResolverAsio,
                         public AsyncObject<ResolverAsioImpl>
{
public:
    using HostAndPort = std::pair<std::string, std::string>;

    beast::Journal m_journal;

    boost::asio::io_service& m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::asio::ip::tcp::resolver m_resolver;

    std::condition_variable m_cv;
    std::mutex m_mut;
    bool m_asyncHandlersCompleted;

    std::atomic<bool> m_stop_called;
    std::atomic<bool> m_stopped;

    // Represents a unit of work for the resolver to do
    struct Work
    {
        std::vector<std::string> names;
        HandlerType handler;

        template <class StringSequence>
        Work(StringSequence const& names_, HandlerType const& handler_)
            : handler(handler_)
        {
            names.reserve(names_.size());

            std::reverse_copy(
                names_.begin(), names_.end(), std::back_inserter(names));
        }
    };

    std::deque<Work> m_work;

    ResolverAsioImpl(
        boost::asio::io_service& io_service,
        beast::Journal journal)
        : m_journal(journal)
        , m_io_service(io_service)
        , m_strand(io_service)
        , m_resolver(io_service)
        , m_asyncHandlersCompleted(true)
        , m_stop_called(false)
        , m_stopped(true)
    {
    }

    ~ResolverAsioImpl() override
    {
        ASSERT(
            m_work.empty(),
            "ripple::ResolverAsioImpl::~ResolverAsioImpl : no pending work");
        ASSERT(
            m_stopped, "ripple::ResolverAsioImpl::~ResolverAsioImpl : stopped");
    }

    //-------------------------------------------------------------------------
    // AsyncObject
    void
    asyncHandlersComplete()
    {
        std::unique_lock<std::mutex> lk{m_mut};
        m_asyncHandlersCompleted = true;
        m_cv.notify_all();
    }

    //--------------------------------------------------------------------------
    //
    // Resolver
    //
    //--------------------------------------------------------------------------

    void
    start() override
    {
        ASSERT(m_stopped == true, "ripple::ResolverAsioImpl::start : stopped");
        ASSERT(
            m_stop_called == false,
            "ripple::ResolverAsioImpl::start : not stopping");

        if (m_stopped.exchange(false) == true)
        {
            {
                std::lock_guard lk{m_mut};
                m_asyncHandlersCompleted = false;
            }
            addReference();
        }
    }

    void
    stop_async() override
    {
        if (m_stop_called.exchange(true) == false)
        {
            m_io_service.dispatch(m_strand.wrap(std::bind(
                &ResolverAsioImpl::do_stop, this, CompletionCounter(this))));

            JLOG(m_journal.debug()) << "Queued a stop request";
        }
    }

    void
    stop() override
    {
        stop_async();

        JLOG(m_journal.debug()) << "Waiting to stop";
        std::unique_lock<std::mutex> lk{m_mut};
        m_cv.wait(lk, [this] { return m_asyncHandlersCompleted; });
        lk.unlock();
        JLOG(m_journal.debug()) << "Stopped";
    }

    void
    resolve(std::vector<std::string> const& names, HandlerType const& handler)
        override
    {
        ASSERT(
            m_stop_called == false,
            "ripple::ResolverAsioImpl::resolve : not stopping");
        ASSERT(
            !names.empty(),
            "ripple::ResolverAsioImpl::resolve : names non-empty");

        // TODO NIKB use rvalue references to construct and move
        //           reducing cost.
        m_io_service.dispatch(m_strand.wrap(std::bind(
            &ResolverAsioImpl::do_resolve,
            this,
            names,
            handler,
            CompletionCounter(this))));
    }

    //-------------------------------------------------------------------------
    // Resolver
    void
    do_stop(CompletionCounter)
    {
        ASSERT(
            m_stop_called == true,
            "ripple::ResolverAsioImpl::do_stop : stopping");

        if (m_stopped.exchange(true) == false)
        {
            m_work.clear();
            m_resolver.cancel();

            removeReference();
        }
    }

    void
    do_finish(
        std::string name,
        boost::system::error_code const& ec,
        HandlerType handler,
        boost::asio::ip::tcp::resolver::iterator iter,
        CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        std::vector<beast::IP::Endpoint> addresses;

        // If we get an error message back, we don't return any
        // results that we may have gotten.
        if (!ec)
        {
            while (iter != boost::asio::ip::tcp::resolver::iterator())
            {
                addresses.push_back(
                    beast::IPAddressConversion::from_asio(*iter));
                ++iter;
            }
        }

        handler(name, addresses);

        m_io_service.post(m_strand.wrap(std::bind(
            &ResolverAsioImpl::do_work, this, CompletionCounter(this))));
    }

    HostAndPort
    parseName(std::string const& str)
    {
        // first attempt to parse as an endpoint (IP addr + port).
        // If that doesn't succeed, fall back to generic name + port parsing

        if (auto const result = beast::IP::Endpoint::from_string_checked(str))
        {
            return make_pair(
                result->address().to_string(), std::to_string(result->port()));
        }

        // generic name/port parsing, which doesn't work for
        // IPv6 addresses in particular because it considers a colon
        // a port separator

        // Attempt to find the first and last non-whitespace
        auto const find_whitespace = std::bind(
            &std::isspace<std::string::value_type>,
            std::placeholders::_1,
            std::locale());

        auto host_first =
            std::find_if_not(str.begin(), str.end(), find_whitespace);

        auto port_last =
            std::find_if_not(str.rbegin(), str.rend(), find_whitespace).base();

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

        auto host_last =
            std::find_if(host_first, port_last, find_port_separator);

        auto port_first =
            std::find_if_not(host_last, port_last, find_port_separator);

        return make_pair(
            std::string(host_first, host_last),
            std::string(port_first, port_last));
    }

    void
    do_work(CompletionCounter)
    {
        if (m_stop_called == true)
            return;

        // We don't have any work to do at this time
        if (m_work.empty())
            return;

        std::string const name(m_work.front().names.back());
        HandlerType handler(m_work.front().handler);

        m_work.front().names.pop_back();

        if (m_work.front().names.empty())
            m_work.pop_front();

        auto const [host, port] = parseName(name);

        if (host.empty())
        {
            JLOG(m_journal.error()) << "Unable to parse '" << name << "'";

            m_io_service.post(m_strand.wrap(std::bind(
                &ResolverAsioImpl::do_work, this, CompletionCounter(this))));

            return;
        }

        boost::asio::ip::tcp::resolver::query query(host, port);

        m_resolver.async_resolve(
            query,
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
    do_resolve(
        std::vector<std::string> const& names,
        HandlerType const& handler,
        CompletionCounter)
    {
        ASSERT(
            !names.empty(),
            "ripple::ResolverAsioImpl::do_resolve : names non-empty");

        if (m_stop_called == false)
        {
            m_work.emplace_back(names, handler);

            JLOG(m_journal.debug())
                << "Queued new job with " << names.size() << " tasks. "
                << m_work.size() << " jobs outstanding.";

            if (m_work.size() > 0)
            {
                m_io_service.post(m_strand.wrap(std::bind(
                    &ResolverAsioImpl::do_work,
                    this,
                    CompletionCounter(this))));
            }
        }
    }
};

//-----------------------------------------------------------------------------

std::unique_ptr<ResolverAsio>
ResolverAsio::New(boost::asio::io_service& io_service, beast::Journal journal)
{
    return std::make_unique<ResolverAsioImpl>(io_service, journal);
}

//-----------------------------------------------------------------------------
Resolver::~Resolver() = default;
}  // namespace ripple
