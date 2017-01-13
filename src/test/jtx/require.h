//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_REQUIRE_H_INCLUDED
#define RIPPLE_TEST_JTX_REQUIRE_H_INCLUDED

#include <test/jtx/requires.h>
#include <functional>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

namespace detail {

inline
void
require_args (requires_t& vec)
{
}

template <class Cond, class... Args>
inline
void
require_args (requires_t& vec,
    Cond const& cond, Args const&... args)
{
    vec.push_back(cond);
    require_args(vec, args...);
}

} // detail

/** Compose many condition functors into one */
template <class...Args>
require_t
required (Args const&... args)
{
    requires_t vec;
    detail::require_args(vec, args...);
    return [vec](Env& env)
    {
        for(auto const& f : vec)
            f(env);
    };
}

/** Check a set of conditions.

    The conditions are checked after a JTx is
    applied, and only if the resulting TER
    matches the expected TER.
*/
class require
{
private:
    require_t cond_;

public:
    template<class... Args>
    require(Args const&... args)
        : cond_(required(args...))
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.requires.emplace_back(cond_);
    }
};

} // jtx
} // test
} // ripple

#endif
