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

#include <ripple/common/Resolver.h>
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/threads/WaitableEvent.h>
#include <boost/asio.hpp>
#include <atomic>
#include <cassert>
#include <deque>
#include <locale>

namespace ripple {

class ResolverImpl
    : public Resolver
{
public:
    typedef std::pair <std::string, std::string> HostAndPort;

    beast::Journal journal_;

    boost::asio::io_service& io_service_;
    boost::asio::io_service::strand strand_;
    boost::asio::ip::tcp::resolver resolver_;

    beast::WaitableEvent stop_complete_;
    std::atomic <bool> stop_called_;
    std::atomic <bool> stopped_;

    // Represents a unit of work for the resolver to do
    struct Work
    {
        std::vector <std::string> names;
        HandlerType handler;

        template <class StringSequence>
        Work (StringSequence const& names_, HandlerType const& handler_)
            : handler (handler_)
        {
            names.reserve(names_.size ());

            std::reverse_copy (names_.begin (), names_.end (),
                std::back_inserter (names));
        }
    };

    std::deque <Work> work_;

    ResolverImpl (boost::asio::io_service& io_service,
        beast::Journal journal)
            : journal_ (journal)
            , io_service_ (io_service)
            , strand_ (io_service)
            , resolver_ (io_service)
            , stop_complete_ (true, true)
            , stop_called_ (false)
            , stopped_ (true)
    {

    }

    ~ResolverImpl ()
    {
        assert (work_.empty ());
        assert (stopped_);
    }

    //--------------------------------------------------------------------------
    //
    // Resolver
    //
    //--------------------------------------------------------------------------

    void start ()
    {
        assert (stopped_.load () == true);
        assert (stop_called_.load () == false);

        work_.clear ();

        if (stopped_.exchange (false) == true)
            stop_complete_.reset ();
    }

    void stop_async ()
    {
        if (stop_called_.exchange (true) == false)
        {
            io_service_.dispatch (strand_.wrap (std::bind (
                &ResolverImpl::do_stop, this)));

            journal_.debug << "Queued a stop request";
        }
    }

    void stop ()
    {
        stop_async ();

        journal_.debug << "Waiting to stop";
        stop_complete_.wait();
        journal_.debug << "Stopped";
    }

    void resolve (
        std::vector <std::string> const& names,
        HandlerType const& handler)
    {
        assert (stop_called_ == false);
        assert (stopped_ == true);
        assert (!names.empty());

        io_service_.dispatch (strand_.wrap (std::bind (
            &ResolverImpl::do_resolve, this, names, handler)));
    }

    //-------------------------------------------------------------------------
    // Resolver
    void do_stop ()
    {
        assert (stop_called_ == true);

        if (stopped_.exchange (true) == false)
            resolver_.cancel ();

        // If the work queue is already empty, then we can signal a stop right
        // away, since nothing else is actively running.
        if (work_.empty ())
            stop_complete_.signal ();
    }

    void do_finish (
        std::string name,
        boost::system::error_code const& ec,
        HandlerType handler,
        boost::asio::ip::tcp::resolver::iterator iter)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            io_service_.post (strand_.wrap (std::bind (
                &ResolverImpl::do_work, this)));
            return;
        }

        std::vector <beast::IP::Endpoint> addresses;

        // If we get an error message back, we don't return any
        // results that we may have gotten.
        if (ec == 0)
        {
            while (iter != boost::asio::ip::tcp::resolver::iterator())
            {
                addresses.push_back (beast::IPAddressConversion::from_asio (*iter));
                ++iter;
            }
        }

        handler (name, addresses);

        io_service_.post (strand_.wrap (std::bind (
            &ResolverImpl::do_work, this)));
    }

    HostAndPort parseName(std::string const& str)
    {
        // Attempt to find the first and last non-whitespace
        auto const find_whitespace = [](char const c) -> bool
        {
            if (std::isspace (c))
                return true;
            return false;
        };

        auto host_first = std::find_if_not (
            str.begin (), str.end (), find_whitespace);

        auto port_last = std::find_if_not (
            str.rbegin (), str.rend(), find_whitespace).base();

        // This should only happen for all-whitespace strings
        if (host_first >= port_last)
            return std::make_pair(std::string (), std::string ());

        // Attempt to find the first and last valid port separators
        auto const find_port_separator = [](char const c) -> bool
        {
            if (std::isspace (c))
                return true;
            return (c == ':');
        };

        auto host_last = std::find_if (
            host_first, port_last, find_port_separator);

        auto port_first = std::find_if_not (
            host_last, port_last, find_port_separator);

        return make_pair (
            std::string (host_first, host_last),
            std::string (port_first, port_last));
    }

    void do_work ()
    {
        if (stop_called_ && !work_.empty ())
        {
            journal_.debug << "Trying to work with stop called. " <<
                "Flushing " << work_.size () << " items from work queue.";
            work_.clear ();
        }

        // We don't have any more work to do at this time
        if (work_.empty ())
        {
            if (stop_called_)
                stop_complete_.signal ();

            return;
        }

        std::string const name (work_.front ().names.back());
        HandlerType handler (work_.front ().handler);

        work_.front ().names.pop_back ();

        if (work_.front ().names.empty ())
            work_.pop_front();

        HostAndPort const hp (parseName (name));

        if (hp.first.empty ())
        {
            journal_.error <<
                "Unable to parse '" << name << "'";

            io_service_.post (strand_.wrap (std::bind (
                &ResolverImpl::do_work, this)));

            return;
        }

        boost::asio::ip::tcp::resolver::query query (
            hp.first, hp.second);

        resolver_.async_resolve (query, std::bind (
            &ResolverImpl::do_finish, this, name,
                beast::asio::placeholders::error, handler,
                    beast::asio::placeholders::iterator));
    }

    void do_resolve (
        std::vector <std::string> const& names,
        HandlerType const& handler)
    {
        assert (! names.empty());

        if (stop_called_)
            return;

        work_.emplace_back (names, handler);

        journal_.debug <<
            "Queued new job with " << names.size() <<
            " tasks. " << work_.size() << " jobs outstanding.";

        if (work_.size() == 1)
        {
            io_service_.post (strand_.wrap (std::bind (
                &ResolverImpl::do_work, this)));
        }
    }
};

//-----------------------------------------------------------------------------
std::unique_ptr<Resolver>
make_Resolver (boost::asio::io_service& io_service, beast::Journal journal)
{
    return std::make_unique<ResolverImpl> (io_service, journal);
}

}
