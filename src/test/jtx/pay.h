#ifndef XRPL_TEST_JTX_PAY_H_INCLUDED
#define XRPL_TEST_JTX_PAY_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/json/json_value.h>

namespace ripple {
namespace test {
namespace jtx {

/** Create a payment. */
Json::Value
pay(AccountID const& account, AccountID const& to, AnyAmount amount);
Json::Value
pay(Account const& account, Account const& to, AnyAmount amount);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
