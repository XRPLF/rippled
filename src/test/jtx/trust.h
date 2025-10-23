#ifndef XRPL_TEST_JTX_TRUST_H_INCLUDED
#define XRPL_TEST_JTX_TRUST_H_INCLUDED

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>

namespace ripple {
namespace test {
namespace jtx {

/** Modify a trust line. */
Json::Value
trust(Account const& account, STAmount const& amount, std::uint32_t flags = 0);

/** Change flags on a trust line. */
Json::Value
trust(
    Account const& account,
    STAmount const& amount,
    Account const& peer,
    std::uint32_t flags);

Json::Value
claw(
    Account const& account,
    STAmount const& amount,
    std::optional<Account> const& mptHolder = std::nullopt);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
