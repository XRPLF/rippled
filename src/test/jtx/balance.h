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

#ifndef RIPPLE_TEST_JTX_BALANCE_H_INCLUDED
#define RIPPLE_TEST_JTX_BALANCE_H_INCLUDED

#include <test/jtx/Env.h>
#include <test/jtx/tags.h>

namespace ripple {
namespace test {
namespace jtx {

/** A balance matches.

    This allows "none" which means either the account
    doesn't exist (no XRP) or the trust line does not
    exist. If an amount is specified, the SLE must
    exist even if the amount is 0, or else the test
    fails.
*/
class balance
{
private:
    bool none_;
    Account account_;
    STAmount value_;

public:
    balance(Account const& account, none_t)
        : none_(true), account_(account), value_(XRP)
    {
    }

    balance(Account const& account, None const& value)
        : none_(true), account_(account), value_(value.issue)
    {
    }

    balance(Account const& account, STAmount const& value)
        : none_(false), account_(account), value_(value)
    {
    }

    void
    operator()(Env&) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
