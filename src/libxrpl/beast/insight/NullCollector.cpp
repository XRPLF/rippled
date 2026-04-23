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
#include <xrpl/beast/insight/NullCollector.h>

#include <memory>
#include <string>

namespace beast {
namespace insight {

namespace detail {

class NullHookImpl : public HookImpl
{
public:
    explicit NullHookImpl() = default;

private:
    NullHookImpl&
    operator=(NullHookImpl const&);
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

private:
    NullCounterImpl&
    operator=(NullCounterImpl const&);
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

private:
    NullEventImpl&
    operator=(NullEventImpl const&);
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

private:
    NullGaugeImpl&
    operator=(NullGaugeImpl const&);
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

private:
    NullMeterImpl&
    operator=(NullMeterImpl const&);
};

//------------------------------------------------------------------------------

class NullCollectorImp : public NullCollector
{
private:
public:
    NullCollectorImp() = default;

    ~NullCollectorImp() = default;

    Hook
    make_hook(HookImpl::HandlerType const&) override
    {
        return Hook(std::make_shared<detail::NullHookImpl>());
    }

    Counter
    make_counter(std::string const&) override
    {
        return Counter(std::make_shared<detail::NullCounterImpl>());
    }

    Event
    make_event(std::string const&) override
    {
        return Event(std::make_shared<detail::NullEventImpl>());
    }

    Gauge
    make_gauge(std::string const&) override
    {
        return Gauge(std::make_shared<detail::NullGaugeImpl>());
    }

    Meter
    make_meter(std::string const&) override
    {
        return Meter(std::make_shared<detail::NullMeterImpl>());
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

std::shared_ptr<Collector>
NullCollector::New()
{
    return std::make_shared<detail::NullCollectorImp>();
}

}  // namespace insight
}  // namespace beast
