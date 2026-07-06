#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/flags.h>

#include <xrpl/json/json_value.h>

namespace xrpl::test::jtx {

/** The null transaction. */
inline json::Value
noop(Account const& account)
{
    return fset(account, 0);
}

}  // namespace xrpl::test::jtx
