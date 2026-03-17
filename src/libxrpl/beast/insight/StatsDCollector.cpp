#include <xrpl/beast/core/List.h>
#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/MeterImpl.h>
#include <xrpl/beast/insight/StatsDCollector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/error_code.hpp>

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

namespace beast {
namespace insight {

namespace detail {

class StatsDCollectorImp;

//------------------------------------------------------------------------------

class StatsDMetricBase : public List<StatsDMetricBase>::Node
{
public:
    virtual void
    do_process() = 0;
    virtual ~StatsDMetricBase() = default;
    StatsDMetricBase() = default;
    StatsDMetricBase(StatsDMetricBase const&) = delete;
    StatsDMetricBase&
    operator=(StatsDMetricBase const&) = delete;
};

//------------------------------------------------------------------------------

class StatsDHookImpl : public HookImpl, public StatsDMetricBase
{
public:
    StatsDHookImpl(HandlerType const& handler, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDHookImpl() override;

    void
    do_process() override;

private:
    StatsDHookImpl&
    operator=(StatsDHookImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    HandlerType handler_;
};

//------------------------------------------------------------------------------

class StatsDCounterImpl : public CounterImpl, public StatsDMetricBase
{
public:
    StatsDCounterImpl(std::string const& name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDCounterImpl() override;

    void
    increment(CounterImpl::value_type amount) override;

    void
    flush();
    void
    do_increment(CounterImpl::value_type amount);
    void
    do_process() override;

private:
    StatsDCounterImpl&
    operator=(StatsDCounterImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    CounterImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

class StatsDEventImpl : public EventImpl
{
public:
    StatsDEventImpl(std::string const& name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDEventImpl() = default;

    void
    notify(EventImpl::value_type const& value) override;

    void
    do_notify(EventImpl::value_type const& value);
    void
    do_process();

private:
    StatsDEventImpl&
    operator=(StatsDEventImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
};

//------------------------------------------------------------------------------

class StatsDGaugeImpl : public GaugeImpl, public StatsDMetricBase
{
public:
    StatsDGaugeImpl(std::string const& name, std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDGaugeImpl() override;

    void
    set(GaugeImpl::value_type value) override;
    void
    increment(GaugeImpl::difference_type amount) override;

    void
    flush();
    void
    do_set(GaugeImpl::value_type value);
    void
    do_increment(GaugeImpl::difference_type amount);
    void
    do_process() override;

private:
    StatsDGaugeImpl&
    operator=(StatsDGaugeImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    GaugeImpl::value_type last_value_{0};
    GaugeImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

class StatsDMeterImpl : public MeterImpl, public StatsDMetricBase
{
public:
    explicit StatsDMeterImpl(
        std::string const& name,
        std::shared_ptr<StatsDCollectorImp> const& impl);

    ~StatsDMeterImpl() override;

    void
    increment(MeterImpl::value_type amount) override;

    void
    flush();
    void
    do_increment(MeterImpl::value_type amount);
    void
    do_process() override;

private:
    StatsDMeterImpl&
    operator=(StatsDMeterImpl const&);

    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    MeterImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

class StatsDCollectorImp : public StatsDCollector,
                           public std::enable_shared_from_this<StatsDCollectorImp>
{
private:
    enum {
        // max_packet_size = 484
        max_packet_size = 1472
    };

    Journal journal_;
    IP::Endpoint address_;
    std::string prefix_;
    boost::asio::io_context io_context_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    boost::asio::ip::udp::socket socket_;
    std::deque<std::string> data_;
    std::recursive_mutex metricsLock_;
    List<StatsDMetricBase> metrics_;

    // Must come last for order of init
    std::thread thread_;

    static boost::asio::ip::udp::endpoint
    to_endpoint(IP::Endpoint const& ep)
    {
        return boost::asio::ip::udp::endpoint(ep.address(), ep.port());
    }

public:
    StatsDCollectorImp(IP::Endpoint const& address, std::string const& prefix, Journal journal)
        : journal_(journal)
        , address_(address)
        , prefix_(prefix)
        , work_(boost::asio::make_work_guard(io_context_))
        , strand_(boost::asio::make_strand(io_context_))
        , timer_(io_context_)
        , socket_(io_context_)
        , thread_(&StatsDCollectorImp::run, this)
    {
    }

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

    Hook
    make_hook(HookImpl::HandlerType const& handler) override
    {
        return Hook(std::make_shared<detail::StatsDHookImpl>(handler, shared_from_this()));
    }

    Counter
    make_counter(std::string const& name) override
    {
        return Counter(std::make_shared<detail::StatsDCounterImpl>(name, shared_from_this()));
    }

    Event
    make_event(std::string const& name) override
    {
        return Event(std::make_shared<detail::StatsDEventImpl>(name, shared_from_this()));
    }

    Gauge
    make_gauge(std::string const& name) override
    {
        return Gauge(std::make_shared<detail::StatsDGaugeImpl>(name, shared_from_this()));
    }

    Meter
    make_meter(std::string const& name) override
    {
        return Meter(std::make_shared<detail::StatsDMeterImpl>(name, shared_from_this()));
    }

    //--------------------------------------------------------------------------

    void
    add(StatsDMetricBase& metric)
    {
        std::lock_guard _(metricsLock_);
        metrics_.push_back(metric);
    }

    void
    remove(StatsDMetricBase& metric)
    {
        std::lock_guard _(metricsLock_);
        metrics_.erase(metrics_.iterator_to(metric));
    }

    //--------------------------------------------------------------------------

    boost::asio::io_context&
    get_io_context()
    {
        return io_context_;
    }

    std::string const&
    prefix() const
    {
        return prefix_;
    }

    void
    do_post_buffer(std::string const& buffer)
    {
        data_.emplace_back(buffer);
    }

    void
    post_buffer(std::string&& buffer)
    {
        boost::asio::dispatch(
            io_context_,
            boost::asio::bind_executor(
                strand_, std::bind(&StatsDCollectorImp::do_post_buffer, this, std::move(buffer))));
    }

    // The keepAlive parameter makes sure the buffers sent to
    // boost::asio::async_send do not go away until the call is finished
    void
    on_send(
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

    void
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

    // Send what we have
    void
    send_buffers()
    {
        if (data_.empty())
            return;

        // Break up the array of strings into blocks
        // that each fit into one UDP packet.
        //
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
                "beast::insight::detail::StatsDCollectorImp::send_buffers : "
                "non-empty payload");
            if (!buffers.empty() && (size + length) > max_packet_size)
            {
                log(buffers);
                socket_.async_send(
                    buffers,
                    std::bind(
                        &StatsDCollectorImp::on_send,
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
                    &StatsDCollectorImp::on_send,
                    this,
                    keepAlive,
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    set_timer()
    {
        using namespace std::chrono_literals;
        timer_.expires_after(1s);
        timer_.async_wait(std::bind(&StatsDCollectorImp::on_timer, this, std::placeholders::_1));
    }

    void
    on_timer(boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            if (auto stream = journal_.error())
                stream << "on_timer failed: " << ec.message();
            return;
        }

        std::lock_guard _(metricsLock_);

        for (auto& m : metrics_)
            m.do_process();

        send_buffers();

        set_timer();
    }

    void
    run()
    {
        boost::system::error_code ec;

        if (socket_.connect(to_endpoint(address_), ec))
        {
            if (auto stream = journal_.error())
                stream << "Connect failed: " << ec.message();
            return;
        }

        set_timer();

        io_context_.run();

        // NOLINTNEXTLINE(bugprone-unused-return-value)
        socket_.shutdown(boost::asio::ip::udp::socket::shutdown_send, ec);

        socket_.close();

        io_context_.poll();
    }
};

//------------------------------------------------------------------------------

StatsDHookImpl::StatsDHookImpl(
    HandlerType const& handler,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), handler_(handler)
{
    impl_->add(*this);
}

StatsDHookImpl::~StatsDHookImpl()
{
    impl_->remove(*this);
}

void
StatsDHookImpl::do_process()
{
    handler_();
}

//------------------------------------------------------------------------------

StatsDCounterImpl::StatsDCounterImpl(
    std::string const& name,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(name)
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
        impl_->get_io_context(),
        std::bind(
            &StatsDCounterImpl::do_increment,
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
        impl_->post_buffer(ss.str());
    }
}

void
StatsDCounterImpl::do_increment(CounterImpl::value_type amount)
{
    value_ += amount;
    dirty_ = true;
}

void
StatsDCounterImpl::do_process()
{
    flush();
}

//------------------------------------------------------------------------------

StatsDEventImpl::StatsDEventImpl(
    std::string const& name,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(name)
{
}

void
StatsDEventImpl::notify(EventImpl::value_type const& value)
{
    boost::asio::dispatch(
        impl_->get_io_context(),
        std::bind(
            &StatsDEventImpl::do_notify,
            std::static_pointer_cast<StatsDEventImpl>(shared_from_this()),
            value));
}

void
StatsDEventImpl::do_notify(EventImpl::value_type const& value)
{
    std::stringstream ss;
    ss << impl_->prefix() << "." << name_ << ":" << value.count() << "|ms"
       << "\n";
    impl_->post_buffer(ss.str());
}

//------------------------------------------------------------------------------

StatsDGaugeImpl::StatsDGaugeImpl(
    std::string const& name,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(name)
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
        impl_->get_io_context(),
        std::bind(
            &StatsDGaugeImpl::do_set,
            std::static_pointer_cast<StatsDGaugeImpl>(shared_from_this()),
            value));
}

void
StatsDGaugeImpl::increment(GaugeImpl::difference_type amount)
{
    boost::asio::dispatch(
        impl_->get_io_context(),
        std::bind(
            &StatsDGaugeImpl::do_increment,
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
        impl_->post_buffer(ss.str());
    }
}

void
StatsDGaugeImpl::do_set(GaugeImpl::value_type value)
{
    value_ = value;

    if (value_ != last_value_)
    {
        last_value_ = value_;
        dirty_ = true;
    }
}

void
StatsDGaugeImpl::do_increment(GaugeImpl::difference_type amount)
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

    do_set(value);
}

void
StatsDGaugeImpl::do_process()
{
    flush();
}

//------------------------------------------------------------------------------

StatsDMeterImpl::StatsDMeterImpl(
    std::string const& name,
    std::shared_ptr<StatsDCollectorImp> const& impl)
    : impl_(impl), name_(name)
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
        impl_->get_io_context(),
        std::bind(
            &StatsDMeterImpl::do_increment,
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
        impl_->post_buffer(ss.str());
    }
}

void
StatsDMeterImpl::do_increment(MeterImpl::value_type amount)
{
    value_ += amount;
    dirty_ = true;
}

void
StatsDMeterImpl::do_process()
{
    flush();
}

}  // namespace detail

//------------------------------------------------------------------------------

std::shared_ptr<StatsDCollector>
StatsDCollector::New(IP::Endpoint const& address, std::string const& prefix, Journal journal)
{
    return std::make_shared<detail::StatsDCollectorImp>(address, prefix, journal);
}

}  // namespace insight
}  // namespace beast
