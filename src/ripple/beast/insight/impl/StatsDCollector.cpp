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

#include <ripple/beast/net/IPAddressConversion.h>
#include <ripple/beast/insight/HookImpl.h>
#include <ripple/beast/insight/CounterImpl.h>
#include <ripple/beast/insight/EventImpl.h>
#include <ripple/beast/insight/GaugeImpl.h>
#include <ripple/beast/insight/MeterImpl.h>
#include <ripple/beast/insight/StatsDCollector.h>
#include <ripple/beast/core/List.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/optional.hpp>
#include <cassert>
#include <climits>
#include <deque>
#include <functional>
#include <mutex>
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

    Journal m_journal;
    IP::Endpoint m_address;
    std::string m_prefix;
    boost::asio::io_service m_io_service;
    boost::optional <boost::asio::io_service::work> m_work;
    boost::asio::io_service::strand m_strand;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> m_timer;
    boost::asio::ip::udp::socket m_socket;
    std::deque <std::string> m_data;
    std::recursive_mutex metricsLock_;
    List <StatsDMetricBase> metrics_;

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
        assert(false);
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
        , m_strand (m_io_service)
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
        std::lock_guard<std::recursive_mutex> _(metricsLock_);
        metrics_.push_back (metric);
    }

    void remove (StatsDMetricBase& metric)
    {
        std::lock_guard<std::recursive_mutex> _(metricsLock_);
        metrics_.erase (metrics_.iterator_to (metric));
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
        m_io_service.dispatch (m_strand.wrap (std::bind (
            &StatsDCollectorImp::do_post_buffer, this,
                std::move (buffer))));
    }

    // The keepAlive parameter makes sure the buffers sent to
    // boost::asio::async_send do not go away until the call is finished
    void on_send (std::shared_ptr<std::deque<std::string>> /*keepAlive*/,
                  boost::system::error_code ec, std::size_t)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            if (auto stream = m_journal.error())
                stream << "async_send failed: " << ec.message();
            return;
        }
    }

    void log (std::vector <boost::asio::const_buffer> const& buffers)
    {
        (void)buffers;
#if BEAST_STATSDCOLLECTOR_TRACING_ENABLED
        for (auto const& buffer : buffers)
        {
            std::string const s (boost::asio::buffer_cast <char const*> (buffer),
                boost::asio::buffer_size (buffer));
            std::cerr << s;
        }
        std::cerr << '\n';
#endif
    }

    // Send what we have
    void send_buffers ()
    {
        if (m_data.empty ())
            return;

        // Break up the array of strings into blocks
        // that each fit into one UDP packet.
        //
        boost::system::error_code ec;
        std::vector <boost::asio::const_buffer> buffers;
        buffers.reserve (m_data.size ());
        std::size_t size (0);

        auto keepAlive =
            std::make_shared<std::deque<std::string>>(std::move (m_data));
        m_data.clear ();

        for (auto const& s : *keepAlive)
        {
            std::size_t const length (s.size ());
            assert (! s.empty ());
            if (! buffers.empty () && (size + length) > max_packet_size)
            {
                log (buffers);
                m_socket.async_send (buffers, std::bind (
                    &StatsDCollectorImp::on_send, this, keepAlive,
                        std::placeholders::_1,
                            std::placeholders::_2));
                buffers.clear ();
                size = 0;
            }

            buffers.emplace_back (&s[0], length);
            size += length;
        }

        if (! buffers.empty ())
        {
            log (buffers);
            m_socket.async_send (buffers, std::bind (
                &StatsDCollectorImp::on_send, this, keepAlive,
                    std::placeholders::_1,
                        std::placeholders::_2));
        }
    }

    void set_timer ()
    {
        using namespace std::chrono_literals;
        m_timer.expires_from_now(1s);
        m_timer.async_wait (std::bind (
            &StatsDCollectorImp::on_timer, this,
                std::placeholders::_1));
    }

    void on_timer (boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            if (auto stream = m_journal.error())
                stream << "on_timer failed: " << ec.message();
            return;
        }

        std::lock_guard<std::recursive_mutex> _(metricsLock_);

        for (auto& m : metrics_)
            m.do_process();

        send_buffers ();

        set_timer ();
    }

    void run ()
    {
        boost::system::error_code ec;

        if (m_socket.connect (to_endpoint (m_address), ec))
        {
            if (auto stream = m_journal.error())
                stream << "Connect failed: " << ec.message();
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
