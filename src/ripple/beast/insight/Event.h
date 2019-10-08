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

#ifndef BEAST_INSIGHT_EVENT_H_INCLUDED
#define BEAST_INSIGHT_EVENT_H_INCLUDED

#include <ripple/beast/insight/EventImpl.h>

#include <date/date.h>

#include <chrono>
#include <memory>

namespace beast {
namespace insight {

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
    Event ()
        { }

    /** Create the metric reference the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Event (std::shared_ptr <EventImpl> const& impl)
        : m_impl (impl)
        { }

    /** Push an event notification. */
    template <class Rep, class Period>
    void
    notify (std::chrono::duration <Rep, Period> const& value) const
    {
        using namespace std::chrono;
        if (m_impl)
            m_impl->notify (date::ceil <value_type> (value));
    }

    std::shared_ptr <EventImpl> const& impl () const
    {
        return m_impl;
    }

private:
    std::shared_ptr <EventImpl> m_impl;
};

}
}

#endif
