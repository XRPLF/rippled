#pragma once

#include <xrpl/beast/insight/GaugeImpl.h>

#include <memory>
#include <utility>

namespace beast::insight {

/** A metric for measuring an integral value.

    A gauge is an instantaneous measurement of a value, like the gas gauge
    in a car. The caller directly sets the value, or adjusts it by a
    specified amount. The value is kept in the client rather than the collector.

    This is a lightweight reference wrapper which is cheap to copy and assign.
    When the last reference goes away, the metric is no longer collected.
*/
class Gauge final
{
public:
    using value_type = GaugeImpl::value_type;
    using difference_type = GaugeImpl::difference_type;

    /** Create a null metric.
        A null metric reports no information.
    */
    Gauge() = default;

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Gauge(std::shared_ptr<GaugeImpl> impl) : impl_(std::move(impl))
    {
    }

    /** Set the value on the gauge.
        A Collector implementation should combine multiple calls to value
        changes into a single change if the calls occur within a single
        collection interval.
    */
    /** @{ */
    void
    set(value_type value) const
    {
        if (impl_)
            impl_->set(value);
    }

    Gauge const&
    operator=(value_type value) const
    {
        set(value);
        return *this;
    }
    /** @} */

    /** Adjust the value of the gauge. */
    /** @{ */
    void
    increment(difference_type amount) const
    {
        if (impl_)
            impl_->increment(amount);
    }

    Gauge const&
    operator+=(difference_type amount) const
    {
        increment(amount);
        return *this;
    }

    Gauge const&
    operator-=(difference_type amount) const
    {
        increment(-amount);
        return *this;
    }

    Gauge const&
    operator++() const
    {
        increment(1);
        return *this;
    }

    Gauge const&
    operator++(int) const
    {
        increment(1);
        return *this;
    }

    Gauge const&
    operator--() const
    {
        increment(-1);
        return *this;
    }

    Gauge const&
    operator--(int) const
    {
        increment(-1);
        return *this;
    }
    /** @} */

    [[nodiscard]] std::shared_ptr<GaugeImpl> const&
    impl() const
    {
        return impl_;
    }

private:
    std::shared_ptr<GaugeImpl> impl_;
};

}  // namespace beast::insight
