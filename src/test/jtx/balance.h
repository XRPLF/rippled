#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/tags.h>

namespace xrpl {
namespace test {
namespace jtx {

/** A balance matches.

    This allows "kNONE" which means either the account
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
    Balance(Account const& account, NoneT) : none_(true), account_(account), value_(XRP)
    {
    }

    Balance(Account const& account, None const& value)
        : none_(true), account_(account), value_(value.asset)
    {
    }

    Balance(Account const& account, STAmount const& value)
        : none_(false), account_(account), value_(value)
    {
    }

    void
    operator()(Env&) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
