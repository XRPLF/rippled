#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/tags.h>

#include <utility>

namespace xrpl::test::jtx {

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
    bool const none_;
    Account const account_;
    STAmount const value_;

public:
    balance(Account account, none_t) : none_(true), account_(std::move(account)), value_(XRP)
    {
    }

    balance(Account account, None const& value)
        : none_(true), account_(std::move(account)), value_(value.asset)
    {
    }

    balance(Account account, STAmount value)
        : none_(false), account_(std::move(account)), value_(std::move(value))
    {
    }

    void
    operator()(Env&) const;
};

}  // namespace xrpl::test::jtx
