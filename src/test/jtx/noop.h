#pragma once

#include <test/jtx/flags.h>

namespace xrpl::test::jtx {

/** The null transaction. */
inline Json::Value
noop(Account const& account)
{
    return fset(account, 0);
}

}  // namespace xrpl::test::jtx
