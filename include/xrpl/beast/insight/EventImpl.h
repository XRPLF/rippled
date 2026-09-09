#pragma once

#include <xrpl/beast/insight/Unit.h>

#include <chrono>
#include <memory>

namespace beast::insight {

class Event;

class EventImpl : public std::enable_shared_from_this<EventImpl>
{
public:
    /**
     * The integral type every sample is stored as.
     *
     * Named for the common case -- durations -- and deliberately left as a
     * duration type. Widening it would change the wire value of every
     * existing StatsD timer, and metrics that need finer resolution than a
     * whole millisecond use the OTel-native microsecond instruments instead.
     * A sample whose unit() is not a duration is carried in the same integral
     * field and interpreted per unit() by the backend.
     */
    using value_type = std::chrono::milliseconds;

    virtual ~EventImpl() = 0;
    virtual void
    notify(value_type const& value) = 0;

    /**
     * @brief What this Event's samples measure. Fixed at construction.
     *
     * The OTel backend reads this to choose the instrument's declared unit
     * and, through that, its bucket ladder. The StatsD backend ignores it.
     */
    [[nodiscard]] Unit
    unit() const noexcept
    {
        return unit_;
    }

protected:
    /**
     * @param unit What the samples measure. Defaults to milliseconds so
     *             existing implementations keep their behaviour unchanged.
     */
    explicit EventImpl(Unit unit = Unit::Millis) : unit_(unit)
    {
    }

private:
    /**
     * What the samples measure; selects the export unit and bucket ladder.
     */
    Unit unit_;
};

}  // namespace beast::insight
