#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/json/json_value.h>

namespace xrpl::test::jtx {

/** Create a payment. */
Json::Value
pay(AccountID const& account, AccountID const& to, AnyAmount amount);
Json::Value
pay(Account const& account, Account const& to, AnyAmount amount);

}  // namespace xrpl::test::jtx
