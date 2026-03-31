#pragma once

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Set a transfer rate. */
Json::Value
rate(Account const& account, double multiplier);

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
