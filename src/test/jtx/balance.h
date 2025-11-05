#ifndef XRPL_TEST_JTX_BALANCE_H_INCLUDED
#define XRPL_TEST_JTX_BALANCE_H_INCLUDED

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
    bool const none_;
    Account const account_;
    STAmount const value_;

public:
    balance(Account const& account, none_t)
        : none_(true), account_(account), value_(XRP)
    {
    }

    balance(Account const& account, None const& value)
        : none_(true), account_(account), value_(value.asset)
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
