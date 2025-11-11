#ifndef BEAST_INSIGHT_METER_H_INCLUDED
#define BEAST_INSIGHT_METER_H_INCLUDED

#include <xrpl/beast/insight/MeterImpl.h>

#include <memory>

namespace beast {
namespace insight {

/** A metric for measuring an integral value.

    A meter may be thought of as an increment-only counter.

    This is a lightweight reference wrapper which is cheap to copy and assign.
    When the last reference goes away, the metric is no longer collected.
*/
class Meter final
{
public:
    using value_type = MeterImpl::value_type;

    /** Create a null metric.
        A null metric reports no information.
    */
    Meter()
    {
    }

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Meter(std::shared_ptr<MeterImpl> const& impl) : m_impl(impl)
    {
    }

    /** Increment the meter. */
    /** @{ */
    void
    increment(value_type amount) const
    {
        if (m_impl)
            m_impl->increment(amount);
    }

    Meter const&
    operator+=(value_type amount) const
    {
        increment(amount);
        return *this;
    }

    Meter const&
    operator++() const
    {
        increment(1);
        return *this;
    }

    Meter const&
    operator++(int) const
    {
        increment(1);
        return *this;
    }
    /** @} */

    std::shared_ptr<MeterImpl> const&
    impl() const
    {
        return m_impl;
    }

private:
    std::shared_ptr<MeterImpl> m_impl;
};

}  // namespace insight
}  // namespace beast

#endif
