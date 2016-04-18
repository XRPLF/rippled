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

#include <ripple/beast/insight/NullCollector.h>

namespace beast {
namespace insight {

namespace detail {

class NullHookImpl : public HookImpl
{
private:
    NullHookImpl& operator= (NullHookImpl const&);
};

//------------------------------------------------------------------------------

class NullCounterImpl : public CounterImpl
{
public:
    void increment (value_type)
    {
    }

private:
    NullCounterImpl& operator= (NullCounterImpl const&);
};

//------------------------------------------------------------------------------

class NullEventImpl : public EventImpl
{
public:
    void notify (value_type const&)
    {
    }

private:
    NullEventImpl& operator= (NullEventImpl const&);
};

//------------------------------------------------------------------------------

class NullGaugeImpl : public GaugeImpl
{
public:
    void set (value_type)
    {
    }

    void increment (difference_type)
    {
    }

private:
    NullGaugeImpl& operator= (NullGaugeImpl const&);
};

//------------------------------------------------------------------------------

class NullMeterImpl : public MeterImpl
{
public:
    void increment (value_type)
    {
    }

private:
    NullMeterImpl& operator= (NullMeterImpl const&);
};

//------------------------------------------------------------------------------

class NullCollectorImp : public NullCollector
{
private:
public:
    NullCollectorImp ()
    {
    }

    ~NullCollectorImp ()
    {
    }

    Hook make_hook (HookImpl::HandlerType const&)
    {
        return Hook (std::make_shared <detail::NullHookImpl> ());
    }

    Counter make_counter (std::string const&)
    {
        return Counter (std::make_shared <detail::NullCounterImpl> ());
    }

    Event make_event (std::string const&)
    {
        return Event (std::make_shared <detail::NullEventImpl> ());
    }

    Gauge make_gauge (std::string const&)
    {
        return Gauge (std::make_shared <detail::NullGaugeImpl> ());
    }

    Meter make_meter (std::string const&)
    {
        return Meter (std::make_shared <detail::NullMeterImpl> ());
    }
};

}

//------------------------------------------------------------------------------

std::shared_ptr <Collector> NullCollector::New ()
{
    return std::make_shared <detail::NullCollectorImp> ();
}

}
}
