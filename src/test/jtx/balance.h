#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/tags.h>

#include <xrpl/protocol/STAmount.h>

#include <utility>

namespace xrpl::test::jtx {

/** A balance matches.

    This allows "none" which means either the account
    doesn't exist (no XRP) or the trust line does not
    exist. If an amount is specified, the SLE must
    exist even if the amount is 0, or else the test
    fails.
*/
class Balance
{
private:
    bool const none_;
    Account const account_;
    STAmount const value_;

public:
    Balance(Account account, NoneT) : none_(true), account_(std::move(account)), value_(XRP)
    {
    }

    Balance(Account account, None const& value)
        : none_(true), account_(std::move(account)), value_(value.asset)
    {
    }

    Balance(Account account, STAmount value)
        : none_(false), account_(std::move(account)), value_(std::move(value))
    {
    }

    void
    operator()(Env&) const;
};

}  // namespace xrpl::test::jtx
