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

#include "../../beast/modules/beast_asio/async/AsyncObject.h"
#include "../../beast/beast/asio/IPAddressConversion.h"

#include <atomic>
#include <deque>

#include "boost/asio.hpp"

#include "../ResolverAsio.h"

namespace ripple {

class ResolverAsioImpl
    : public ResolverAsio
    , public AsyncObject <ResolverAsioImpl>
{
public:
    typedef std::pair <std::string, std::string> HostAndPort;

    Journal m_journal;

    boost::asio::io_service& m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::asio::ip::tcp::resolver m_resolver;

    WaitableEvent m_stop_complete;
    std::atomic <bool> m_stop_called;
    std::atomic <bool> m_stopped;

    bool m_idle;

    // Represents a unit of work for the resolver to do
    struct Work 
    {
        std::vector <std::string> names;
        HandlerType handler;

        template <class StringSequence>
        Work (StringSequence const& names_, HandlerType const& handler_)
            : handler(handler_)
        {
            names.reserve(names_.size());

            std::reverse_copy (names_.begin(), names_.end(),
                std::back_inserter (names));
        }
    };

    std::deque <Work> m_work;

    ResolverAsioImpl (boost::asio::io_service& io_service,
        Journal journal)
            : m_journal (journal)
            , m_io_service (io_service)
            , m_strand (io_service)
            , m_resolver (io_service)
            , m_stop_complete (true)
            , m_stop_called (false)
            , m_stopped (false)
            , m_idle (true)
    {
        addReference ();
    }
    
    ~ResolverAsioImpl ()
    {
        check_precondition (m_work.empty());
        check_precondition (m_stopped);
    }

    //-------------------------------------------------------------------------
    // AsyncObject
    void asyncHandlersComplete()
    {
        m_stop_complete.signal ();
    }

    //--------------------------------------------------------------------------
    //
    // Resolver
    //
    //--------------------------------------------------------------------------

    void stop_async ()
    {
        if (m_stop_called.exchange (true) == false)
        {
            m_io_service.dispatch ( m_strand.wrap ( boost::bind (
                &ResolverAsioImpl::do_stop, 
                this, CompletionCounter (this))));

            m_journal.debug << "Queued a stop request";
        }
    }

    void stop ()
    {
        stop_async ();
        m_journal.debug << "Waiting to stop";
        m_stop_complete.wait();
        m_journal.debug << "Stopped";
    }

    void resolve (
        std::vector <std::string> const& names,
        HandlerType const& handler)
    {
        check_precondition (m_stop_called.load () == 0);
        check_precondition (!names.empty());

        // TODO NIKB use rvalue references to construct and move
        //           reducing cost.
        m_io_service.dispatch (m_strand.wrap (boost::bind (
            &ResolverAsioImpl::do_resolve, this,
            names, handler, CompletionCounter (this))));
    }

    //-------------------------------------------------------------------------
    // Resolver
    void do_stop (CompletionCounter)
    {
        if (meets_precondition (m_stop_called == true) &&
            meets_precondition (m_stopped == false))
        {
            m_work.clear ();
            m_resolver.cancel ();
            m_stopped.exchange (true);

            m_journal.debug << "Stopped";
            
            removeReference ();
        }
    }

    void do_finish (
        std::string name,
        boost::system::error_code const& ec,
        HandlerType handler,
        boost::asio::ip::tcp::resolver::iterator iter,
        CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        std::vector <IPAddress> addresses;

        // If we get an error message back, we don't return any
        // results that we may have gotten.
        if (ec == 0)
        {
            while (iter != boost::asio::ip::tcp::resolver::iterator())
            {
                addresses.push_back (IPAddressConversion::from_asio (*iter));
                ++iter;
            }
        }

        handler (name, addresses);

        m_io_service.post (m_strand.wrap (boost::bind (
            &ResolverAsioImpl::do_work, this, 
            CompletionCounter (this))));
    }

    HostAndPort parseName(std::string const& str)
    {
        std::string host (str);
        std::string port;

        std::string::size_type sep (host.find(':'));

        if(sep == std::string::npos)
            sep = host.find(' ');

        if(sep != std::string::npos)
        {
            port = host.substr(sep + 1);
            host.erase(sep);
        }

        return std::make_pair(host, port);
    }

    void do_work (CompletionCounter)
    {
        if (m_stop_called.load () == 1)
            return;

        // We don't have any work to do at this time
        if (m_work.empty ())
        {
            m_idle = true;
            m_journal.trace << "Sleeping";
            return;
        }

        std::string const name (m_work.front ().names.back());
        HandlerType handler (m_work.front ().handler);

        m_work.front ().names.pop_back ();

        if (m_work.front ().names.empty ())
            m_work.pop_front();

        HostAndPort const hp (parseName (name));

        if (hp.first.empty ())
        {
            m_journal.error <<
                "Unable to parse '" << name << "'";

            m_io_service.post (m_strand.wrap (boost::bind (
                &ResolverAsioImpl::do_work, this,
                CompletionCounter (this))));

            return;
        }

        boost::asio::ip::tcp::resolver::query query (
            hp.first, hp.second);

        m_resolver.async_resolve (query, boost::bind (
            &ResolverAsioImpl::do_finish, this, name,
            boost::asio::placeholders::error, handler,
            boost::asio::placeholders::iterator,
            CompletionCounter (this)));
    }

    void do_resolve (std::vector <std::string> const& names,
        HandlerType const& handler, CompletionCounter)
    {
        check_precondition (! names.empty());

        if (m_stop_called.load () == 0)
        {
            // TODO NIKB use emplace_back once we move to C++11
            m_work.push_back(Work(names, handler));

            m_journal.debug <<
                "Queued new job with " << names.size() <<
                " tasks. " << m_work.size() << " jobs outstanding.";

            if (m_work.size() == 1)
            {
                check_precondition (m_idle);

                m_journal.trace << "Waking up";
                m_idle = false;

                m_io_service.post (m_strand.wrap (boost::bind (
                    &ResolverAsioImpl::do_work, this,
                    CompletionCounter (this))));
            }
        }
    }
};

//-----------------------------------------------------------------------------

ResolverAsio *ResolverAsio::New (
    boost::asio::io_service& io_service,
    beast::Journal journal)
{
    return new ResolverAsioImpl (io_service, journal);
}

//-----------------------------------------------------------------------------
Resolver::~Resolver ()
{
}

}
