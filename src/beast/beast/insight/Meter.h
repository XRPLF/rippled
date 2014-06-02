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

#ifndef BEAST_INSIGHT_METER_H_INCLUDED
#define BEAST_INSIGHT_METER_H_INCLUDED

#include <memory>

#include <beast/insight/Base.h>
#include <beast/insight/MeterImpl.h>

namespace beast {
namespace insight {

/** A metric for measuring an integral value.
    
    A meter may be thought of as an increment-only counter.

    This is a lightweight reference wrapper which is cheap to copy and assign.
    When the last reference goes away, the metric is no longer collected.
*/
class Meter : public Base
{
public:
    typedef MeterImpl::value_type value_type;

    /** Create a null metric.
        A null metric reports no information.
    */
    Meter ()
        { }

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Meter (std::shared_ptr <MeterImpl> const& impl)
        : m_impl (impl)
        { }

    /** Increment the meter. */
    /** @{ */
    void increment (value_type amount) const
    {
        if (m_impl)
            m_impl->increment (amount);
    }

    Meter const& operator+= (value_type amount) const
    {
        increment (amount);
        return *this;
    }

    Meter const& operator++ () const
    {
        increment (1);
        return *this;
    }

    Meter const& operator++ (int) const
    {
        increment (1);
        return *this;
    }
    /** @} */

    std::shared_ptr <MeterImpl> const& impl () const
    {
        return m_impl;
    }

private:
    std::shared_ptr <MeterImpl> m_impl;
};

}
}

#endif
