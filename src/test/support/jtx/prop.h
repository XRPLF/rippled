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

#ifndef RIPPLE_TEST_JTX_PROP_H_INCLUDED
#define RIPPLE_TEST_JTX_PROP_H_INCLUDED

#include <test/support/jtx/Env.h>
#include <memory>

namespace ripple {
namespace test {
namespace jtx {

/** Set a property on a JTx. */
template <class Prop>
struct prop
{
    std::unique_ptr<basic_prop> p_;

    template <class... Args>
    prop(Args&&... args)
        : p_(std::make_unique<
            prop_type<Prop>>(
                std::forward <Args> (
                    args)...))
    {
    }

    void
    operator()(Env& env, JTx& jt) const
    {
        jt.set(p_->clone());
    }
};

} // jtx
} // test
} // ripple

#endif
