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

#ifndef BEAST_INSIGHT_COLLECTOR_H_INCLUDED
#define BEAST_INSIGHT_COLLECTOR_H_INCLUDED

#include <ripple/beast/insight/Counter.h>
#include <ripple/beast/insight/Event.h>
#include <ripple/beast/insight/Gauge.h>
#include <ripple/beast/insight/Hook.h>
#include <ripple/beast/insight/Meter.h>

#include <string>

namespace beast {
namespace insight {

/** Interface for a manager that allows collection of metrics.

    To export metrics from a class, pass and save a shared_ptr to this
    interface in the class constructor. Create the metric objects
    as desired (counters, events, gauges, meters, and an optional hook)
    using the interface.

    @see Counter, Event, Gauge, Hook, Meter
    @see NullCollector, StatsDCollector
*/
class Collector
{
public:
    using ptr = std::shared_ptr<Collector>;

    virtual ~Collector() = 0;

    /** Create a hook.

        A hook is called at each collection interval, on an implementation
        defined thread. This is a convenience facility for gathering metrics
        in the polling style. The typical usage is to update all the metrics
        of interest in the handler.

        Handler will be called with this signature:
            void handler (void)

        @see Hook
    */
    /** @{ */
    template <class Handler>
    Hook
    make_hook(Handler handler)
    {
        return make_hook(HookImpl::HandlerType(handler));
    }

    virtual Hook
    make_hook(HookImpl::HandlerType const& handler) = 0;
    /** @} */

    /** Create a counter with the specified name.
        @see Counter
    */
    /** @{ */
    virtual Counter
    make_counter(std::string const& name) = 0;

    Counter
    make_counter(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return make_counter(name);
        return make_counter(prefix + "." + name);
    }
    /** @} */

    /** Create an event with the specified name.
        @see Event
    */
    /** @{ */
    virtual Event
    make_event(std::string const& name) = 0;

    Event
    make_event(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return make_event(name);
        return make_event(prefix + "." + name);
    }
    /** @} */

    /** Create a gauge with the specified name.
        @see Gauge
    */
    /** @{ */
    virtual Gauge
    make_gauge(std::string const& name) = 0;

    Gauge
    make_gauge(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return make_gauge(name);
        return make_gauge(prefix + "." + name);
    }
    /** @} */

    /** Create a meter with the specified name.
        @see Meter
    */
    /** @{ */
    virtual Meter
    make_meter(std::string const& name) = 0;

    Meter
    make_meter(std::string const& prefix, std::string const& name)
    {
        if (prefix.empty())
            return make_meter(name);
        return make_meter(prefix + "." + name);
    }
    /** @} */
};

}  // namespace insight
}  // namespace beast

#endif
