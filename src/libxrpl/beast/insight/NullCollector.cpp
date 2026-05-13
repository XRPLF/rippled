/** @file
 *  No-op implementations of every `beast::insight` metric type.
 *
 *  Defines `NullCollectorImp` (private to `beast::insight::detail`) and
 *  implements `NullCollector::make()`. All metric operations are virtual
 *  dispatches that immediately return without recording anything.
 */

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

/** No-op `HookImpl` that silently discards the polling handler.
 *
 *  The real collector would store the `HandlerType` and invoke it on each
 *  collection interval. This impl simply lets the handler be destroyed at
 *  construction time, producing no callbacks and no overhead.
 *
 *  Copy assignment is deleted to prevent accidental value-copy of a
 *  `shared_from_this` object.
 */
class NullHookImpl : public HookImpl
{
public:
    explicit NullHookImpl() = default;

    NullHookImpl&
    operator=(NullHookImpl const&) = delete;
};

//------------------------------------------------------------------------------

/** No-op `CounterImpl` whose `increment` is a silent discard.
 *
 *  Copy assignment is deleted to prevent accidental value-copy of a
 *  `shared_from_this` object.
 */
class NullCounterImpl : public CounterImpl
{
public:
    explicit NullCounterImpl() = default;

    /** No-op: discards the increment amount without recording it. */
    void
    increment(value_type) override
    {
    }

    NullCounterImpl&
    operator=(NullCounterImpl const&) = delete;
};

//------------------------------------------------------------------------------

/** No-op `EventImpl` whose `notify` is a silent discard.
 *
 *  Copy assignment is deleted to prevent accidental value-copy of a
 *  `shared_from_this` object.
 */
class NullEventImpl : public EventImpl
{
public:
    explicit NullEventImpl() = default;

    /** No-op: discards the event value without recording it. */
    void
    notify(value_type const&) override
    {
    }

    NullEventImpl&
    operator=(NullEventImpl const&) = delete;
};

//------------------------------------------------------------------------------

/** No-op `GaugeImpl` whose absolute `set` and relative `increment` are both
 *  silent discards.
 *
 *  Gauges are the only metric type that supports both absolute assignment
 *  (`set`) and relative adjustment (`increment`), so both overrides are
 *  required even though neither does anything here.
 *
 *  Copy assignment is deleted to prevent accidental value-copy of a
 *  `shared_from_this` object.
 */
class NullGaugeImpl : public GaugeImpl
{
public:
    explicit NullGaugeImpl() = default;

    /** No-op: discards the absolute gauge value without recording it. */
    void
    set(value_type) override
    {
    }

    /** No-op: discards the relative gauge adjustment without recording it. */
    void
    increment(difference_type) override
    {
    }

    NullGaugeImpl&
    operator=(NullGaugeImpl const&) = delete;
};

//------------------------------------------------------------------------------

/** No-op `MeterImpl` whose `increment` is a silent discard.
 *
 *  Copy assignment is deleted to prevent accidental value-copy of a
 *  `shared_from_this` object.
 */
class NullMeterImpl : public MeterImpl
{
public:
    explicit NullMeterImpl() = default;

    /** No-op: discards the meter increment without recording it. */
    void
    increment(value_type) override
    {
    }

    NullMeterImpl&
    operator=(NullMeterImpl const&) = delete;
};

//------------------------------------------------------------------------------

/** Concrete `NullCollector` implementation that allocates a fresh no-op
 *  metric impl for every `make_*` call.
 *
 *  Each returned metric handle holds a `shared_ptr` to its null impl, so
 *  its lifetime is tied to the holder — consistent with how the real
 *  `StatsDCollector` manages its metric objects. The handler passed to
 *  `makeHook` is silently discarded.
 *
 *  This class is private to this translation unit; callers only ever see
 *  the `std::shared_ptr<Collector>` returned by `NullCollector::make()`.
 */
class NullCollectorImp : public NullCollector
{
private:
public:
    NullCollectorImp() = default;

    ~NullCollectorImp() override = default;

    /** Returns a `Hook` backed by a `NullHookImpl`; the handler is discarded. */
    Hook
    makeHook(HookImpl::HandlerType const&) override
    {
        return Hook(std::make_shared<detail::NullHookImpl>());
    }

    /** Returns a `Counter` backed by a `NullCounterImpl`; increments are discarded. */
    Counter
    makeCounter(std::string const&) override
    {
        return Counter(std::make_shared<detail::NullCounterImpl>());
    }

    /** Returns an `Event` backed by a `NullEventImpl`; notifications are discarded. */
    Event
    makeEvent(std::string const&) override
    {
        return Event(std::make_shared<detail::NullEventImpl>());
    }

    /** Returns a `Gauge` backed by a `NullGaugeImpl`; sets and increments are discarded. */
    Gauge
    makeGauge(std::string const&) override
    {
        return Gauge(std::make_shared<detail::NullGaugeImpl>());
    }

    /** Returns a `Meter` backed by a `NullMeterImpl`; increments are discarded. */
    Meter
    makeMeter(std::string const&) override
    {
        return Meter(std::make_shared<detail::NullMeterImpl>());
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

/** Factory that returns a `NullCollector` as a `Collector` pointer.
 *
 *  Callers receive a `std::shared_ptr<Collector>` pointing at a
 *  `NullCollectorImp`, keeping the concrete type invisible at every call
 *  site. Suitable for use wherever metrics reporting is not desired, without
 *  requiring any conditional checks in the consuming code.
 *
 *  @return A shared pointer to a newly constructed `NullCollectorImp`.
 */
std::shared_ptr<Collector>
NullCollector::make()
{
    return std::make_shared<detail::NullCollectorImp>();
}

}  // namespace beast::insight
