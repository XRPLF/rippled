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

#ifndef BEAST_CHRONO_SCOPEDTIMEINTERVAL_H_INCLUDED
#define BEAST_CHRONO_SCOPEDTIMEINTERVAL_H_INCLUDED

#include "../Uncopyable.h"
#include "RelativeTime.h"

namespace beast {

/** Time measurement using scoped RAII container. 
    UnaryFunction will be called with this signature:
        void (RelativeTime const& interval);
*/
template <class UnaryFunction>
class ScopedTimeInterval : public Uncopyable
{
public:
    /** Create the measurement with a default-constructed UnaryFunction. */
    ScopedTimeInterval ()
        : m_start (RelativeTime::fromStartup())
    {
    }

    /** Create the measurement with UnaryFunction constructed from one argument. */
    template <typename Arg>
    explicit ScopedTimeInterval (Arg arg)
        : m_func (arg)
        , m_start (RelativeTime::fromStartup ())
    {
    }

    ~ScopedTimeInterval ()
    {
        m_func (RelativeTime::fromStartup() - m_start);
    }

private:
    UnaryFunction m_func;
    RelativeTime m_start;
};

}

#endif
