#pragma once

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <optional>

namespace xrpl::test::jtx {

/** Modify a trust line. */
json::Value
trust(Account const& account, STAmount const& amount, std::uint32_t flags = 0);

/** Change flags on a trust line. */
json::Value
trust(Account const& account, STAmount const& amount, Account const& peer, std::uint32_t flags);

json::Value
claw(
    Account const& account,
    STAmount const& amount,
    std::optional<Account> const& mptHolder = std::nullopt);

}  // namespace xrpl::test::jtx
