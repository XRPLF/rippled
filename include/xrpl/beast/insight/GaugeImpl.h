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

#ifndef BEAST_INSIGHT_GAUGEIMPL_H_INCLUDED
#define BEAST_INSIGHT_GAUGEIMPL_H_INCLUDED

#include <cstdint>
#include <memory>

namespace beast {
namespace insight {

class Gauge;

class GaugeImpl : public std::enable_shared_from_this<GaugeImpl>
{
public:
    using value_type = std::uint64_t;
    using difference_type = std::int64_t;

    virtual ~GaugeImpl() = 0;
    virtual void
    set(value_type value) = 0;
    virtual void
    increment(difference_type amount) = 0;
};

}  // namespace insight
}  // namespace beast

#endif
