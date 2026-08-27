#pragma once

#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>
#include <xrpl/beast/insight/Unit.h>

#include <memory>
#include <string>

namespace beast::insight {

/**
 * Interface for a manager that allows collection of metrics.
 *
 * To export metrics from a class, pass and save a shared_ptr to this
 * interface in the class constructor. Create the metric objects
 * as desired (counters, events, gauges, meters, and an optional hook)
 * using the interface.
 *
 * @see Counter, Event, Gauge, Hook, Meter
 * @see NullCollector, StatsDCollector
 */
class Collector
{
public:
    using ptr = std::shared_ptr<Collector>;

    virtual ~Collector() = 0;

    /**
     * Called once the services that hook handlers read are constructed.
     *
     * Implementations that poll their producers must not do so before this:
     * hook handlers read live application state. Default is a no-op, for
     * collectors that only push.
     */
    virtual void
    onCollectionReady()
    {
    }

    /**
     * Called before those services are shut down.
     *
     * Polling must have stopped by the time this returns. Paired with
     * onCollectionReady().
     */
    virtual void
    onCollectionStopping()
    {
    }

    /**
     * Create a hook.
     *
     * A hook is called at each collection interval, on an implementation
     * defined thread. This is a convenience facility for gathering metrics
     * in the polling style. The typical usage is to update all the metrics
     * of interest in the handler.
     *
     * Handler will be called with this signature:
     *     void handler (void)
     *
     * @see Hook
     */
    /** @{ */
    template <class Handler>
    Hook
    makeHook(Handler handler)
    {
        return makeHook(HookImpl::HandlerType(handler));
    }

    virtual Hook
    makeHook(HookImpl::HandlerType const& handler) = 0;
    /** @} */

    /**
     * Create a counter with the specified name.
     * @see Counter
     */
    /** @{ */
    virtual Counter
    makeCounter(std::string const& name) = 0;

    Counter
    makeCounter(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return makeCounter(name);
        return makeCounter(prefix + "." + name);
    }
    /** @} */

    /**
     * Create an event with the specified name.
     * @see Event
     */
    /** @{ */
    virtual Event
    makeEvent(std::string const& name) = 0;

    /**
     * Create an event whose samples measure `unit` rather than milliseconds.
     *
     * The default delegates to the millisecond overload, so a collector that
     * cannot act on a unit keeps working unchanged -- the StatsD collector
     * relies on this. Collectors that map a unit onto an export format, such
     * as the OTel collector, override it.
     *
     * @param name Metric name, already prefixed if it came through a Group.
     * @param unit What the samples measure.
     */
    virtual Event
    makeEvent(std::string const& name, Unit unit)
    {
        (void)unit;
        return makeEvent(name);
    }

    Event
    makeEvent(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return makeEvent(name);
        return makeEvent(prefix + "." + name);
    }

    Event
    makeEvent(std::string const& prefix, std::string const& name, Unit unit)
    {
        if (prefix.empty())
            return makeEvent(name, unit);
        return makeEvent(prefix + "." + name, unit);
    }
    /** @} */

    /**
     * Create a gauge with the specified name.
     * @see Gauge
     */
    /** @{ */
    virtual Gauge
    makeGauge(std::string const& name) = 0;

    Gauge
    makeGauge(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return makeGauge(name);
        return makeGauge(prefix + "." + name);
    }
    /** @} */

    /**
     * Create a meter with the specified name.
     * @see Meter
     */
    /** @{ */
    virtual Meter
    makeMeter(std::string const& name) = 0;

    Meter
    makeMeter(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return makeMeter(name);
        return makeMeter(prefix + "." + name);
    }
    /** @} */
};

}  // namespace beast::insight
