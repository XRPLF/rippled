#ifndef XRPL_TEST_JTX_RATE_H_INCLUDED
#define XRPL_TEST_JTX_RATE_H_INCLUDED

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>

namespace ripple {
namespace test {
namespace jtx {

/** Set a transfer rate. */
Json::Value
rate(Account const& account, double multiplier);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
