#ifndef XRPL_TEST_JTX_REGKEY_H_INCLUDED
#define XRPL_TEST_JTX_REGKEY_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/json/json_value.h>

namespace ripple {
namespace test {
namespace jtx {

/** Disable the regular key. */
Json::Value
regkey(Account const& account, disabled_t);

/** Set a regular key. */
Json::Value
regkey(Account const& account, Account const& signer);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
