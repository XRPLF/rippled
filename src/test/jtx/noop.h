#pragma once

#include <test/jtx/flags.h>

namespace xrpl {
namespace test {
namespace jtx {

/** The null transaction. */
inline Json::Value
noop(Account const& account)
{
    return fset(account, 0);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
