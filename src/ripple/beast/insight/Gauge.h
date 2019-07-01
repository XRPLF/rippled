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

#ifndef BEAST_INSIGHT_GAUGE_H_INCLUDED
#define BEAST_INSIGHT_GAUGE_H_INCLUDED

#include <ripple/beast/insight/GaugeImpl.h>

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
    Gauge ()
    {
    }

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Gauge (std::shared_ptr <GaugeImpl> const& impl)
        : m_impl (impl)
    {
    }

    /** Set the value on the gauge.
        A Collector implementation should combine multiple calls to value
        changes into a single change if the calls occur within a single
        collection interval.
    */
    /** @{ */
    void set (value_type value) const
    {
        if (m_impl)
            m_impl->set (value);
    }

    Gauge const& operator= (value_type value) const
        { set (value); return *this; }
    /** @} */

    /** Adjust the value of the gauge. */
    /** @{ */
    void increment (difference_type amount) const
    {
        if (m_impl)
            m_impl->increment (amount);
    }

    Gauge const&
    operator+= (difference_type amount) const
    {
        increment (amount);
        return *this;
    }

    Gauge const&
    operator-= (difference_type amount) const
    {
        increment (-amount);
        return *this;
    }

    Gauge const&
    operator++ () const
    {
        increment (1);
        return *this;
    }

    Gauge const&
    operator++ (int) const
    {
        increment (1);
        return *this;
    }

    Gauge const&
    operator-- () const
    {
        increment (-1);
        return *this;
    }

    Gauge const&
    operator-- (int) const
    {
        increment (-1);
        return *this;
    }
    /** @} */

    std::shared_ptr <GaugeImpl> const&
    impl () const
    {
        return m_impl;
    }

private:
    std::shared_ptr <GaugeImpl> m_impl;
};

}
}

#endif
