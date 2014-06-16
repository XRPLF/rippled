//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/intrusive/List.h>
#include <beast/threads/SharedData.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/optional.hpp>
#include <cassert>
#include <climits>
#include <deque>
#include <functional>
#include <set>
#include <sstream>
#include <thread>

#ifndef BEAST_STATSDCOLLECTOR_TRACING_ENABLED
#define BEAST_STATSDCOLLECTOR_TRACING_ENABLED 0
#endif

namespace beast {
namespace insight {

namespace detail {

class StatsDCollectorImp;

//------------------------------------------------------------------------------

class StatsDMetricBase : public List <StatsDMetricBase>::Node
{
public:
    virtual void do_process () = 0;
};

//------------------------------------------------------------------------------

class StatsDHookImpl
    : public HookImpl
    , public StatsDMetricBase
{
public:
    StatsDHookImpl (
        HandlerType const& handler,
            std::shared_ptr <StatsDCollectorImp> const& impl);

    ~StatsDHookImpl ();

    void do_process ();

private:
    StatsDHookImpl& operator= (StatsDHookImpl const&);

    std::shared_ptr <StatsDCollectorImp> m_impl;
    HandlerType m_handler;
};

//------------------------------------------------------------------------------

class StatsDCounterImpl
    : public CounterImpl
    , public StatsDMetricBase
{
public:
    StatsDCounterImpl (std::string const& name,
        std::shared_ptr <StatsDCollectorImp> const& impl);

    ~StatsDCounterImpl ();

    void increment (CounterImpl::value_type amount);

    void flush ();
    void do_increment (CounterImpl::value_type amount);
    void do_process ();

private:
    StatsDCounterImpl& operator= (StatsDCounterImpl const&);

    std::shared_ptr <StatsDCollectorImp> m_impl;
    std::string m_name;
    CounterImpl::value_type m_value;
    bool m_dirty;
};

//------------------------------------------------------------------------------

class StatsDEventImpl
    : public EventImpl
{
public:
    StatsDEventImpl (std::string const& name,
        std::shared_ptr <StatsDCollectorImp> const& impl);

    ~StatsDEventImpl ();

    void notify (EventImpl::value_type const& alue);

    void do_notify (EventImpl::value_type const& value);
    void do_process ();

private:
    StatsDEventImpl& operator= (StatsDEventImpl const&);

    std::shared_ptr <StatsDCollectorImp> m_impl;
    std::string m_name;
};

//------------------------------------------------------------------------------

class StatsDGaugeImpl
    : public GaugeImpl
    , public StatsDMetricBase
{
public:
    StatsDGaugeImpl (std::string const& name,
        std::shared_ptr <StatsDCollectorImp> const& impl);

    ~StatsDGaugeImpl ();

    void set (GaugeImpl::value_type value);
    void increment (GaugeImpl::difference_type amount);

    void flush ();
    void do_set (GaugeImpl::value_type value);
    void do_increment (GaugeImpl::difference_type amount);
    void do_process ();

private:
    StatsDGaugeImpl& operator= (StatsDGaugeImpl const&);

    std::shared_ptr <StatsDCollectorImp> m_impl;
    std::string m_name;
    GaugeImpl::value_type m_last_value;
    GaugeImpl::value_type m_value;
    bool m_dirty;
};

//------------------------------------------------------------------------------

class StatsDMeterImpl
    : public MeterImpl
    , public StatsDMetricBase
{
public:
    explicit StatsDMeterImpl (std::string const& name,
        std::shared_ptr <StatsDCollectorImp> const& impl);

    ~StatsDMeterImpl ();

    void increment (MeterImpl::value_type amount);

    void flush ();
    void do_increment (MeterImpl::value_type amount);
    void do_process ();

private:
    StatsDMeterImpl& operator= (StatsDMeterImpl const&);

    std::shared_ptr <StatsDCollectorImp> m_impl;
    std::string m_name;
    MeterImpl::value_type m_value;
    bool m_dirty;
};

//------------------------------------------------------------------------------

class StatsDCollectorImp
    : public StatsDCollector
    , public std::enable_shared_from_this <StatsDCollectorImp>
{
private:
    enum
    {
        //max_packet_size = 484
        max_packet_size = 1472
    };

    struct StateType
    {
        List <StatsDMetricBase> metrics;
    };

    typedef SharedData <StateType> State;

    Journal m_journal;
    IP::Endpoint m_address;
    std::string m_prefix;
    boost::asio::io_service m_io_service;
    boost::optional <boost::asio::io_service::work> m_work;
    boost::asio::deadline_timer m_timer;
    boost::asio::ip::udp::socket m_socket;
    std::deque <std::string> m_data;
    State m_state;

    // Must come last for order of init
    std::thread m_thread;

    static boost::asio::ip::udp::endpoint to_endpoint (
        IP::Endpoint const &address)
    {
        if (address.is_v4 ())
        {
            return boost::asio::ip::udp::endpoint (
                boost::asio::ip::address_v4 (
                    address.to_v4().value), address.port ());
        }

        // VFALCO TODO IPv6 support
        bassertfalse;
        return boost::asio::ip::udp::endpoint (
            boost::asio::ip::address_v6 (), 0);
    }

public:
    StatsDCollectorImp (
        IP::Endpoint const& address,
        std::string const& prefix,
        Journal journal)
        : m_journal (journal)
        , m_address (address)
        , m_prefix (prefix)
        , m_work (std::ref (m_io_service))
        , m_timer (m_io_service)
        , m_socket (m_io_service)
        , m_thread (&StatsDCollectorImp::run, this)
    {
    }

    ~StatsDCollectorImp ()
    {
        boost::system::error_code ec;
        m_timer.cancel (ec);

        m_work = boost::none;
        m_thread.join ();
    }

    Hook make_hook (HookImpl::HandlerType const& handler)
    {
        return Hook (std::make_shared <detail::StatsDHookImpl> (
            handler, shared_from_this ()));
    }

    Counter make_counter (std::string const& name)
    {
        return Counter (std::make_shared <detail::StatsDCounterImpl> (
            name, shared_from_this ()));
    }

    Event make_event (std::string const& name)
    {
        return Event (std::make_shared <detail::StatsDEventImpl> (
            name, shared_from_this ()));
    }

    Gauge make_gauge (std::string const& name)
    {
        return Gauge (std::make_shared <detail::StatsDGaugeImpl> (
            name, shared_from_this ()));
    }

    Meter make_meter (std::string const& name)
    {
        return Meter (std::make_shared <detail::StatsDMeterImpl> (
            name, shared_from_this ()));
    }

    //--------------------------------------------------------------------------

    void add (StatsDMetricBase& metric)
    {
        State::Access state (m_state);
        state->metrics.push_back (metric);
    }

    void remove (StatsDMetricBase& metric)
    {
        State::Access state (m_state);
        state->metrics.erase (state->metrics.iterator_to (metric));
    }

    //--------------------------------------------------------------------------

    boost::asio::io_service& get_io_service ()
    {
        return m_io_service;
    }

    std::string const& prefix () const
    {
        return m_prefix;
    }

    void do_post_buffer (std::string const& buffer)
    {
        m_data.emplace_back (buffer);
    }

    void post_buffer (std::string&& buffer)
    {
        m_io_service.dispatch (std::bind (
            &StatsDCollectorImp::do_post_buffer, this,
                std::move (buffer)));
    }

    void on_send (boost::system::error_code ec, std::size_t)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.error <<
                "async_send failed: " << ec.message();
            return;
        }
    }

    void log (std::vector <boost::asio::const_buffer> const& buffers)
    {
        (void)buffers;
#if BEAST_STATSDCOLLECTOR_TRACING_ENABLED
        std::stringstream ss;
        for (auto const& buffer : buffers)
        {
            std::string const s (boost::asio::buffer_cast <char const*> (buffer),
                boost::asio::buffer_size (buffer));
            ss << s;
        }
        //m_journal.trace << std::endl << ss.str ();
        Logger::outputDebugString (ss.str ());
#endif
    }

    // Send what we have
    void send_buffers ()
    {
        // Break up the array of strings into blocks
        // that each fit into one UDP packet.
        //
        boost::system::error_code ec;
        std::vector <boost::asio::const_buffer> buffers;
        buffers.reserve (m_data.size ());
        std::size_t size (0);
        for (std::deque <std::string>::const_iterator iter (m_data.begin());
            iter != m_data.end(); ++iter)
        {
            std::string const& buffer (*iter);
            std::size_t const length (buffer.size ());
            assert (! buffer.empty ());
            if (! buffers.empty () && (size + length) > max_packet_size)
            {
#if BEAST_STATSDCOLLECTOR_TRACING_ENABLED
                log (buffers);
#endif
                m_socket.async_send (buffers, std::bind (
                    &StatsDCollectorImp::on_send, this,
                        beast::asio::placeholders::error,
                            beast::asio::placeholders::bytes_transferred));
                buffers.clear ();
                size = 0;
            }
            buffers.emplace_back (&buffer[0], length);
            size += length;
        }
        if (! buffers.empty ())
        {
#if BEAST_STATSDCOLLECTOR_TRACING_ENABLED
            log (buffers);
#endif
            m_socket.async_send (buffers, std::bind (
                &StatsDCollectorImp::on_send, this,
                    beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred));
        }
        m_data.clear ();
    }

    void set_timer ()
    {
        m_timer.expires_from_now (boost::posix_time::seconds (1));
        m_timer.async_wait (std::bind (
            &StatsDCollectorImp::on_timer, this,
                beast::asio::placeholders::error));
    }

    void on_timer (boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_journal.error <<
                "on_timer failed: " << ec.message();
            return;
        }

        State::Access state (m_state);

        for (List <StatsDMetricBase>::iterator iter (state->metrics.begin());
            iter != state->metrics.end(); ++iter)
            iter->do_process();

        send_buffers ();

        set_timer ();
    }

    void run ()
    {
        boost::system::error_code ec;

        if (m_socket.connect (to_endpoint (m_address), ec))
        {
            m_journal.error <<
                "Connect failed: " << ec.message();
            return;
        }

        set_timer ();

        m_io_service.run ();

        m_socket.shutdown (
            boost::asio::ip::udp::socket::shutdown_send, ec);

        m_socket.close ();

        m_io_service.poll ();
    }
};

//------------------------------------------------------------------------------

StatsDHookImpl::StatsDHookImpl (HandlerType const& handler,
    std::shared_ptr <StatsDCollectorImp> const& impl)
    : m_impl (impl)
    , m_handler (handler)
{
    m_impl->add (*this);
}

StatsDHookImpl::~StatsDHookImpl ()
{
    m_impl->remove (*this);
}

void StatsDHookImpl::do_process ()
{
    m_handler ();
}

//------------------------------------------------------------------------------

StatsDCounterImpl::StatsDCounterImpl (std::string const& name,
    std::shared_ptr <StatsDCollectorImp> const& impl)
    : m_impl (impl)
    , m_name (name)
    , m_value (0)
    , m_dirty (false)
{
    m_impl->add (*this);
}

StatsDCounterImpl::~StatsDCounterImpl ()
{
    m_impl->remove (*this);
}

void StatsDCounterImpl::increment (CounterImpl::value_type amount)
{
    m_impl->get_io_service().dispatch (std::bind (
        &StatsDCounterImpl::do_increment,
            std::static_pointer_cast <StatsDCounterImpl> (
                shared_from_this ()), amount));
}

void StatsDCounterImpl::flush ()
{
    if (m_dirty)
    {
        m_dirty = false;
        std::stringstream ss;
        ss <<
            m_impl->prefix() << "." <<
            m_name << ":" <<
            m_value << "|c" <<
            "\n";
        m_value = 0;
        m_impl->post_buffer (ss.str ());
    }
}

void StatsDCounterImpl::do_increment (CounterImpl::value_type amount)
{
    m_value += amount;
    m_dirty = true;
}

void StatsDCounterImpl::do_process ()
{
    flush ();
}

//------------------------------------------------------------------------------

StatsDEventImpl::StatsDEventImpl (std::string const& name,
    std::shared_ptr <StatsDCollectorImp> const& impl)
    : m_impl (impl)
    , m_name (name)
{
}

StatsDEventImpl::~StatsDEventImpl ()
{
}

void StatsDEventImpl::notify (EventImpl::value_type const& value)
{
    m_impl->get_io_service().dispatch (std::bind (
        &StatsDEventImpl::do_notify,
            std::static_pointer_cast <StatsDEventImpl> (
                shared_from_this ()), value));
}

void StatsDEventImpl::do_notify (EventImpl::value_type const& value)
{
    std::stringstream ss;
    ss <<
        m_impl->prefix() << "." <<
        m_name << ":" <<
        value.count() << "|ms" <<
        "\n";
    m_impl->post_buffer (ss.str ());
}

//------------------------------------------------------------------------------

StatsDGaugeImpl::StatsDGaugeImpl (std::string const& name,
    std::shared_ptr <StatsDCollectorImp> const& impl)
    : m_impl (impl)
    , m_name (name)
    , m_last_value (0)
    , m_value (0)
    , m_dirty (false)
{
    m_impl->add (*this);
}

StatsDGaugeImpl::~StatsDGaugeImpl ()
{
    m_impl->remove (*this);
}

void StatsDGaugeImpl::set (GaugeImpl::value_type value)
{
    m_impl->get_io_service().dispatch (std::bind (
        &StatsDGaugeImpl::do_set,
            std::static_pointer_cast <StatsDGaugeImpl> (
                shared_from_this ()), value));
}

void StatsDGaugeImpl::increment (GaugeImpl::difference_type amount)
{
    m_impl->get_io_service().dispatch (std::bind (
        &StatsDGaugeImpl::do_increment,
            std::static_pointer_cast <StatsDGaugeImpl> (
                shared_from_this ()), amount));
}

void StatsDGaugeImpl::flush ()
{
    if (m_dirty)
    {
        m_dirty = false;
        std::stringstream ss;
        ss <<
            m_impl->prefix() << "." <<
            m_name << ":" <<
            m_value << "|c" <<
            "\n";
        m_impl->post_buffer (ss.str ());
    }
}

void StatsDGaugeImpl::do_set (GaugeImpl::value_type value)
{
    m_value = value;
    
    if (m_value != m_last_value)
    {
        m_last_value = m_value;
        m_dirty = true;
    }
}

void StatsDGaugeImpl::do_increment (GaugeImpl::difference_type amount)
{
    GaugeImpl::value_type value (m_value);

    if (amount > 0)
    {
        GaugeImpl::value_type const d (
            static_cast <GaugeImpl::value_type> (amount));
        value +=
            (d >= std::numeric_limits <GaugeImpl::value_type>::max() - m_value)
            ? std::numeric_limits <GaugeImpl::value_type>::max() - m_value
            : d;
    }
    else if (amount < 0)
    {
        GaugeImpl::value_type const d (
            static_cast <GaugeImpl::value_type> (-amount));
        value = (d >= value) ? 0 : value - d; 
    }

    do_set (value);
}

void StatsDGaugeImpl::do_process ()
{
    flush ();
}

//------------------------------------------------------------------------------

StatsDMeterImpl::StatsDMeterImpl (std::string const& name,
    std::shared_ptr <StatsDCollectorImp> const& impl)
    : m_impl (impl)
    , m_name (name)
    , m_value (0)
    , m_dirty (false)
{
    m_impl->add (*this);
}

StatsDMeterImpl::~StatsDMeterImpl ()
{
    m_impl->remove (*this);
}

void StatsDMeterImpl::increment (MeterImpl::value_type amount)
{
    m_impl->get_io_service().dispatch (std::bind (
        &StatsDMeterImpl::do_increment,
            std::static_pointer_cast <StatsDMeterImpl> (
                shared_from_this ()), amount));
}

void StatsDMeterImpl::flush ()
{
    if (m_dirty)
    {
        m_dirty = false;
        std::stringstream ss;
        ss <<
            m_impl->prefix() << "." <<
            m_name << ":" <<
            m_value << "|m" <<
            "\n";
        m_value = 0;
        m_impl->post_buffer (ss.str ());
    }
}

void StatsDMeterImpl::do_increment (MeterImpl::value_type amount)
{
    m_value += amount;
    m_dirty = true;
}

void StatsDMeterImpl::do_process ()
{
    flush ();
}

}

//------------------------------------------------------------------------------

std::shared_ptr <StatsDCollector> StatsDCollector::New (
    IP::Endpoint const& address, std::string const& prefix, Journal journal)
{
    return std::make_shared <detail::StatsDCollectorImp> (
        address, prefix, journal);
}

}
}
