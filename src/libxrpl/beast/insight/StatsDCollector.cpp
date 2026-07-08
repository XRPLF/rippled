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

class StatsDMetricBase : public List<StatsDMetricBase>::Node
{
public:
    virtual void
    doProcess() = 0;
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
    StatsDHookImpl(HandlerType handler, std::shared_ptr<StatsDCollectorImp> impl);

    ~StatsDHookImpl() override;

    void
    doProcess() override;

    StatsDHookImpl&
    operator=(StatsDHookImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    HandlerType handler_;
};

//------------------------------------------------------------------------------

class StatsDCounterImpl : public CounterImpl, public StatsDMetricBase
{
public:
    StatsDCounterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl);

    ~StatsDCounterImpl() override;

    void
    increment(CounterImpl::value_type amount) override;

    void
    flush();
    void
    doIncrement(CounterImpl::value_type amount);
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

class StatsDEventImpl : public EventImpl
{
public:
    StatsDEventImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl);

    ~StatsDEventImpl() override = default;

    void
    notify(EventImpl::value_type const& value) override;

    void
    doNotify(EventImpl::value_type const& value);
    void
    doProcess();

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
    StatsDGaugeImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl);

    ~StatsDGaugeImpl() override;

    void
    set(GaugeImpl::value_type value) override;
    void
    increment(GaugeImpl::difference_type amount) override;

    void
    flush();
    void
    doSet(GaugeImpl::value_type value);
    void
    doIncrement(GaugeImpl::difference_type amount);
    void
    doProcess() override;

    StatsDGaugeImpl&
    operator=(StatsDGaugeImpl const&) = delete;

private:
    std::shared_ptr<StatsDCollectorImp> impl_;
    std::string name_;
    GaugeImpl::value_type lastValue_{0};
    GaugeImpl::value_type value_{0};
    bool dirty_{false};
};

//------------------------------------------------------------------------------

class StatsDMeterImpl : public MeterImpl, public StatsDMetricBase
{
public:
    explicit StatsDMeterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl);

    ~StatsDMeterImpl() override;

    void
    increment(MeterImpl::value_type amount) override;

    void
    flush();
    void
    doIncrement(MeterImpl::value_type amount);
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

class StatsDCollectorImp : public StatsDCollector,
                           public std::enable_shared_from_this<StatsDCollectorImp>
{
private:
    static constexpr auto kMaxPacketSize = 1472;

    Journal journal_;
    IP::Endpoint address_;
    std::string prefix_;
    boost::asio::io_context ioContext_;
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
    toEndpoint(IP::Endpoint const& ep)
    {
        return boost::asio::ip::udp::endpoint(ep.address(), ep.port());
    }

public:
    StatsDCollectorImp(IP::Endpoint address, std::string prefix, Journal journal)
        : journal_(journal)
        , address_(std::move(address))
        , prefix_(std::move(prefix))
        , work_(boost::asio::make_work_guard(ioContext_))
        , strand_(boost::asio::make_strand(ioContext_))
        , timer_(ioContext_)
        , socket_(ioContext_)
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
    makeHook(HookImpl::HandlerType const& handler) override
    {
        return Hook(std::make_shared<detail::StatsDHookImpl>(handler, shared_from_this()));
    }

    Counter
    makeCounter(std::string const& name) override
    {
        return Counter(std::make_shared<detail::StatsDCounterImpl>(name, shared_from_this()));
    }

    Event
    makeEvent(std::string const& name) override
    {
        return Event(std::make_shared<detail::StatsDEventImpl>(name, shared_from_this()));
    }

    Gauge
    makeGauge(std::string const& name) override
    {
        return Gauge(std::make_shared<detail::StatsDGaugeImpl>(name, shared_from_this()));
    }

    Meter
    makeMeter(std::string const& name) override
    {
        return Meter(std::make_shared<detail::StatsDMeterImpl>(name, shared_from_this()));
    }

    //--------------------------------------------------------------------------

    void
    add(StatsDMetricBase& metric)
    {
        std::scoped_lock const _(metricsLock_);
        metrics_.pushBack(metric);
    }

    void
    remove(StatsDMetricBase& metric)
    {
        std::scoped_lock const _(metricsLock_);
        metrics_.erase(metrics_.iteratorTo(metric));
    }

    //--------------------------------------------------------------------------

    boost::asio::io_context&
    getIoContext()
    {
        return ioContext_;
    }

    std::string const&
    prefix() const
    {
        return prefix_;
    }

    void
    doPostBuffer(std::string const& buffer)
    {
        data_.emplace_back(buffer);
    }

    void
    postBuffer(std::string&& buffer)
    {
        boost::asio::dispatch(
            ioContext_,
            boost::asio::bind_executor(
                strand_, std::bind(&StatsDCollectorImp::doPostBuffer, this, std::move(buffer))));
    }

    // The keepAlive parameter makes sure the buffers sent to
    // boost::asio::async_send do not go away until the call is finished
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

    // Send what we have
    void
    sendBuffers()
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
                "beast::insight::detail::StatsDCollectorImp::sendBuffers : "
                "non-empty payload");
            if (!buffers.empty() && (size + length) > kMaxPacketSize)
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

    void
    setTimer()
    {
        using namespace std::chrono_literals;
        timer_.expires_after(1s);
        timer_.async_wait(std::bind(&StatsDCollectorImp::onTimer, this, std::placeholders::_1));
    }

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

        ioContext_.run();

        // NOLINTNEXTLINE(bugprone-unused-return-value)
        socket_.shutdown(boost::asio::ip::udp::socket::shutdown_send, ec);

        socket_.close();

        ioContext_.poll();
    }
};

//------------------------------------------------------------------------------

StatsDHookImpl::StatsDHookImpl(HandlerType handler, std::shared_ptr<StatsDCollectorImp> impl)
    : impl_(std::move(impl)), handler_(std::move(handler))
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

StatsDCounterImpl::StatsDCounterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl)
    : impl_(std::move(impl)), name_(std::move(name))
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

StatsDEventImpl::StatsDEventImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl)
    : impl_(std::move(impl)), name_(std::move(name))
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

StatsDGaugeImpl::StatsDGaugeImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl)
    : impl_(std::move(impl)), name_(std::move(name))
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

    if (value_ != lastValue_)
    {
        lastValue_ = value_;
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

StatsDMeterImpl::StatsDMeterImpl(std::string name, std::shared_ptr<StatsDCollectorImp> impl)
    : impl_(std::move(impl)), name_(std::move(name))
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
