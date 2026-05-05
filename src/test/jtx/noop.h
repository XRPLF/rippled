#pragma once

#include <test/jtx/flags.h>

namespace xrpl::test::jtx {

/** The null transaction. */
inline json::Value
noop(Account const& account)
{
    return fset(account, 0);
}

}  // namespace xrpl::test::jtx
