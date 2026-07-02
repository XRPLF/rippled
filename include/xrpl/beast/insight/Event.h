#pragma once

#include <xrpl/beast/insight/EventImpl.h>

#include <chrono>
#include <memory>
#include <utility>

namespace beast::insight {

/** A metric for reporting event timing.

    An event is an operation that has an associated millisecond time, or
    other integral value. Because events happen at a specific moment, the
    metric only supports a push-style interface.

    This is a lightweight reference wrapper which is cheap to copy and assign.
    When the last reference goes away, the metric is no longer collected.
*/
class Event final
{
public:
    using value_type = EventImpl::value_type;

    /** Create a null metric.
        A null metric reports no information.
    */
    Event() = default;

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Event(std::shared_ptr<EventImpl> impl) : impl_(std::move(impl))
    {
    }

    /** Push an event notification. */
    template <class Rep, class Period>
    void
    notify(std::chrono::duration<Rep, Period> const& value) const
    {
        using namespace std::chrono;
        if (impl_)
            impl_->notify(ceil<value_type>(value));
    }

    [[nodiscard]] std::shared_ptr<EventImpl> const&
    impl() const
    {
        return impl_;
    }

private:
    std::shared_ptr<EventImpl> impl_;
};

}  // namespace beast::insight
