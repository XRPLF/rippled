#include <xrpl/beast/insight/NullCollector.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>
#include <xrpl/beast/insight/MeterImpl.h>

#include <memory>
#include <string>

namespace beast::insight {

namespace detail {

class NullHookImpl : public HookImpl
{
public:
    explicit NullHookImpl() = default;

    NullHookImpl&
    operator=(NullHookImpl const&) = delete;
};

//------------------------------------------------------------------------------

class NullCounterImpl : public CounterImpl
{
public:
    explicit NullCounterImpl() = default;

    void
    increment(value_type) override
    {
    }

    NullCounterImpl&
    operator=(NullCounterImpl const&) = delete;
};

//------------------------------------------------------------------------------

class NullEventImpl : public EventImpl
{
public:
    explicit NullEventImpl() = default;

    void
    notify(value_type const&) override
    {
    }

    NullEventImpl&
    operator=(NullEventImpl const&) = delete;
};

//------------------------------------------------------------------------------

class NullGaugeImpl : public GaugeImpl
{
public:
    explicit NullGaugeImpl() = default;

    void
    set(value_type) override
    {
    }

    void
    increment(difference_type) override
    {
    }

    NullGaugeImpl&
    operator=(NullGaugeImpl const&) = delete;
};

//------------------------------------------------------------------------------

class NullMeterImpl : public MeterImpl
{
public:
    explicit NullMeterImpl() = default;

    void
    increment(value_type) override
    {
    }

    NullMeterImpl&
    operator=(NullMeterImpl const&) = delete;
};

//------------------------------------------------------------------------------

class NullCollectorImp : public NullCollector
{
private:
public:
    NullCollectorImp() = default;

    ~NullCollectorImp() override = default;

    Hook
    makeHook(HookImpl::HandlerType const&) override
    {
        return Hook(std::make_shared<detail::NullHookImpl>());
    }

    Counter
    makeCounter(std::string const&) override
    {
        return Counter(std::make_shared<detail::NullCounterImpl>());
    }

    Event
    makeEvent(std::string const&) override
    {
        return Event(std::make_shared<detail::NullEventImpl>());
    }

    Gauge
    makeGauge(std::string const&) override
    {
        return Gauge(std::make_shared<detail::NullGaugeImpl>());
    }

    Meter
    makeMeter(std::string const&) override
    {
        return Meter(std::make_shared<detail::NullMeterImpl>());
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

std::shared_ptr<Collector>
NullCollector::make()
{
    return std::make_shared<detail::NullCollectorImp>();
}

}  // namespace beast::insight
