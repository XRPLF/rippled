#ifndef BEAST_INSIGHT_GAUGE_H_INCLUDED
#define BEAST_INSIGHT_GAUGE_H_INCLUDED

#include <xrpl/beast/insight/GaugeImpl.h>

#include <memory>

namespace beast {
namespace insight {

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
    Gauge()
    {
    }

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Gauge(std::shared_ptr<GaugeImpl> const& impl) : m_impl(impl)
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
        if (m_impl)
            m_impl->set(value);
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
        if (m_impl)
            m_impl->increment(amount);
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

    std::shared_ptr<GaugeImpl> const&
    impl() const
    {
        return m_impl;
    }

private:
    std::shared_ptr<GaugeImpl> m_impl;
};

}  // namespace insight
}  // namespace beast

#endif
